/* This file contains the scheduling policy for SCHED
 *
 * ALGORITMO: Primeiro a Chegar, Primeiro a Ser Servido (FCFS)
 *
 * Conforme Tanenbaum (4ª ed.), seção 2.4.2:
 *   - Não-preemptivo: um processo roda até bloquear ou terminar
 *   - Fila única: processos entram no fim e saem do início
 *   - Simples e justo em termos de ordem de chegada
 *
 * Estratégia de implementação no MINIX:
 *   - Todos os processos de usuário recebem a mesma prioridade fixa (USER_Q)
 *   - Quantum muito alto (~infinito) para simular não-preempção
 *   - do_noquantum NÃO penaliza o processo (não baixa prioridade)
 *   - balance_queues não altera prioridades de usuário
 *   - A fila do kernel já é FIFO por construção (inserção no tail)
 */

#include "sched.h"
#include "schedproc.h"
#include <assert.h>
#include <minix/com.h>
#include <machine/archtypes.h>

static unsigned balance_timeout;

#define BALANCE_TIMEOUT     5   /* intervalo de rebalanceamento (segundos) */

/*
 * FCFS: quantum "infinito" — grande o suficiente para que o processo
 * não seja preemptado pelo timer em condições normais.
 * Tanenbaum explica que no FCFS puro um processo só deixa a CPU ao
 * bloquear (E/S) ou terminar — nunca por estouro de quantum.
 */
#define FCFS_TIME_SLICE     100000

/*
 * Fila única para todos os processos de usuário.
 * USER_Q é definido in kernel/proc.h — geralmente vale 7 em NR_SCHED_QUEUES=16.
 * Usar sempre USER_Q garante que TODOS entrem na mesma fila (FIFO).
 */
#define FCFS_PRIORITY       USER_Q

static int schedule_process(struct schedproc *rmp, unsigned flags);

#define SCHEDULE_CHANGE_PRIO    0x1
#define SCHEDULE_CHANGE_QUANTUM 0x2
#define SCHEDULE_CHANGE_CPU     0x4

#define SCHEDULE_CHANGE_ALL ( \
        SCHEDULE_CHANGE_PRIO   | \
        SCHEDULE_CHANGE_QUANTUM | \
        SCHEDULE_CHANGE_CPU     \
        )

#define schedule_process_local(p) \
    schedule_process(p, SCHEDULE_CHANGE_PRIO | SCHEDULE_CHANGE_QUANTUM)
#define schedule_process_migrate(p) \
    schedule_process(p, SCHEDULE_CHANGE_CPU)

#define CPU_DEAD    -1
#define cpu_is_available(c) (cpu_proc[c] >= 0)

/* processos criados pelo RS são processos de sistema */
#define is_system_proc(p) ((p)->parent == RS_PROC_NR)

static unsigned cpu_proc[CONFIG_MAX_CPUS];

static void pick_cpu(struct schedproc *proc)
{
#ifdef CONFIG_SMP
    unsigned cpu, c;
    unsigned cpu_load = (unsigned) -1;

    if (machine.processors_count == 1) {
        proc->cpu = machine.bsp_id;
        return;
    }
    if (is_system_proc(proc)) {
        proc->cpu = machine.bsp_id;
        return;
    }
    cpu = machine.bsp_id;
    for (c = 0; c < machine.processors_count; c++) {
        if (!cpu_is_available(c))
            continue;
        if (c != machine.bsp_id && cpu_load > cpu_proc[c]) {
            cpu_load = cpu_proc[c];
            cpu = c;
        }
    }
    proc->cpu = cpu;
    cpu_proc[cpu]++;
#else
    proc->cpu = 0;
#endif
}

/*===========================================================================*
 *                          do_noquantum                                     *
 *===========================================================================*
 * Chamado pelo kernel quando o processo esgota seu quantum.
 *
 * NO FCFS: isso NÃO deveria ocorrer frequentemente (quantum alto), mas
 * se ocorrer (ex: processo muito longo em CPU), NÃO penalizamos com
 * rebaixamento de prioridade. O processo volta para o FIM da fila com
 * a MESMA prioridade — garantindo a ordem FIFO.
 */
int do_noquantum(message *m_ptr)
{
    register struct schedproc *rmp;
    int rv, proc_nr_n;

    if (sched_isokendpt(m_ptr->m_source, &proc_nr_n) != OK) {
        printf("SCHED: WARNING: endpoint inválido em msg OOQ %u.\n",
               m_ptr->m_source);
        return EBADEPT;
    }

    rmp = &schedproc[proc_nr_n];

    /*
     * FCFS: NÃO alterar a prioridade.
     * No escalonador padrão havia: rmp->priority += 1 (rebaixamento).
     * Removemos isso — todos ficam em FCFS_PRIORITY.
     * O processo será reinserido no TAIL da fila pelo kernel,
     * preservando a ordem de chegada relativa aos demais prontos.
     */

    if ((rv = schedule_process_local(rmp)) != OK)
        return rv;

    return OK;
}

/*===========================================================================*
 *                      do_stop_scheduling                                   *
 *===========================================================================*/
int do_stop_scheduling(message *m_ptr)
{
    register struct schedproc *rmp;
    int proc_nr_n;

    if (!accept_message(m_ptr))
        return EPERM;

    if (sched_isokendpt(m_ptr->m_lsys_sched_scheduling_stop.endpoint,
                &proc_nr_n) != OK) {
        printf("SCHED: WARNING: endpoint inválido em msg OOQ %d\n",
               m_ptr->m_lsys_sched_scheduling_stop.endpoint);
        return EBADEPT;
    }

    rmp = &schedproc[proc_nr_n];
#ifdef CONFIG_SMP
    cpu_proc[rmp->cpu]--;
#endif
    rmp->flags = 0;

    return OK;
}

/*===========================================================================*
 *                      do_start_scheduling                                  *
 *===========================================================================*
 * Chamado quando um novo processo começa a ser escalonado.
 *
 * FCFS: todo processo de usuário entra com prioridade FCFS_PRIORITY
 * e quantum FCFS_TIME_SLICE, independente do pai.
 */
int do_start_scheduling(message *m_ptr)
{
    register struct schedproc *rmp;
    int rv, proc_nr_n, parent_nr_n;

    assert(m_ptr->m_type == SCHEDULING_START ||
           m_ptr->m_type == SCHEDULING_INHERIT);

    if (!accept_message(m_ptr))
        return EPERM;

    if ((rv = sched_isemtyendpt(
            m_ptr->m_lsys_sched_scheduling_start.endpoint,
            &proc_nr_n)) != OK) {
        return rv;
    }
    rmp = &schedproc[proc_nr_n];

    rmp->endpoint     = m_ptr->m_lsys_sched_scheduling_start.endpoint;
    rmp->parent       = m_ptr->m_lsys_sched_scheduling_start.parent;
    rmp->max_priority = m_ptr->m_lsys_sched_scheduling_start.maxprio;
    if (rmp->max_priority >= NR_SCHED_QUEUES)
        return EINVAL;

    /* Caso especial: init (processo pai de si mesmo) */
    if (rmp->endpoint == rmp->parent) {
        rmp->priority   = FCFS_PRIORITY;
        rmp->time_slice = FCFS_TIME_SLICE;
#ifdef CONFIG_SMP
        rmp->cpu = machine.bsp_id;
#endif
    }

    switch (m_ptr->m_type) {

    case SCHEDULING_START:
        /*
         * Processos de sistema (RS, PM, VFS...) mantêm sua prioridade
         * real (max_priority), pois são processos de sistema com
         * prioridade alta — não devem ser tratados como FCFS de usuário.
         *
         * Processos de usuário recebem FCFS_PRIORITY.
         */
        if (is_system_proc(rmp)) {
            rmp->priority   = rmp->max_priority;
            rmp->time_slice = m_ptr->m_lsys_sched_scheduling_start.quantum;
        } else {
            rmp->priority   = FCFS_PRIORITY;
            rmp->time_slice = FCFS_TIME_SLICE;
        }
        break;

    case SCHEDULING_INHERIT:
        /*
         * FCFS: ignoramos a herança de prioridade do pai.
         * Todo processo filho de usuário começa na mesma fila FCFS.
         */
        if ((rv = sched_isokendpt(
                m_ptr->m_lsys_sched_scheduling_start.parent,
                &parent_nr_n)) != OK)
            return rv;

        if (is_system_proc(rmp)) {
            rmp->priority   = schedproc[parent_nr_n].priority;
            rmp->time_slice = schedproc[parent_nr_n].time_slice;
        } else {
            rmp->priority   = FCFS_PRIORITY;
            rmp->time_slice = FCFS_TIME_SLICE;
        }
        break;

    default:
        assert(0);
    }

    /* Registra tempo de chegada para fins de rastreio/debug */
    rmp->arrival_time = (unsigned long) sys_hz();

    /* Passa o controle de escalonamento para este servidor SCHED */
    if ((rv = sys_schedctl(0, rmp->endpoint, 0, 0, 0)) != OK) {
        printf("Sched: Erro ao assumir escalonamento de %d: %d\n",
               rmp->endpoint, rv);
        return rv;
    }
    rmp->flags = IN_USE;

    pick_cpu(rmp);
    while ((rv = schedule_process(rmp, SCHEDULE_CHANGE_ALL)) == EBADCPU) {
        cpu_proc[rmp->cpu] = CPU_DEAD;
        pick_cpu(rmp);
    }

    if (rv != OK) {
        printf("Sched: Erro ao escalonar processo, kernel retornou %d\n", rv);
        return rv;
    }

    m_ptr->m_sched_lsys_scheduling_start.scheduler = SCHED_PROC_NR;

    return OK;
}

/*===========================================================================*
 *                          do_nice                                          *
 *===========================================================================*
 * FCFS: nice é ignorado para processos de usuário — todos têm mesma prio.
 * Para processos de sistema, mantemos o comportamento original.
 */
int do_nice(message *m_ptr)
{
    struct schedproc *rmp;
    int rv;
    int proc_nr_n;
    unsigned new_q, old_q, old_max_q;

    if (!accept_message(m_ptr))
        return EPERM;

    if (sched_isokendpt(
            m_ptr->m_pm_sched_scheduling_set_nice.endpoint,
            &proc_nr_n) != OK) {
        printf("SCHED: WARNING: endpoint inválido em msg OoQ %d\n",
               m_ptr->m_pm_sched_scheduling_set_nice.endpoint);
        return EBADEPT;
    }

    rmp = &schedproc[proc_nr_n];
    new_q = m_ptr->m_pm_sched_scheduling_set_nice.maxprio;
    if (new_q >= NR_SCHED_QUEUES)
        return EINVAL;

    old_q     = rmp->priority;
    old_max_q = rmp->max_priority;

    /* FCFS: processos de usuário ignoram nice — ficam em FCFS_PRIORITY */
    if (!is_system_proc(rmp)) {
        rmp->max_priority = FCFS_PRIORITY;
        rmp->priority     = FCFS_PRIORITY;
    } else {
        rmp->max_priority = rmp->priority = new_q;
    }

    if ((rv = schedule_process_local(rmp)) != OK) {
        rmp->priority     = old_q;
        rmp->max_priority = old_max_q;
    }

    return rv;
}

/*===========================================================================*
 *                      schedule_process                                     *
 *===========================================================================*/
static int schedule_process(struct schedproc *rmp, unsigned flags)
{
    int err;
    int new_prio, new_quantum, new_cpu, niced;

    pick_cpu(rmp);

    if (flags & SCHEDULE_CHANGE_PRIO)
        new_prio = rmp->priority;
    else
        new_prio = -1;

    if (flags & SCHEDULE_CHANGE_QUANTUM)
        new_quantum = rmp->time_slice;
    else
        new_quantum = -1;

    if (flags & SCHEDULE_CHANGE_CPU)
        new_cpu = rmp->cpu;
    else
        new_cpu = -1;

    niced = (rmp->max_priority > USER_Q);

    if ((err = sys_schedule(rmp->endpoint, new_prio,
            new_quantum, new_cpu, niced)) != OK) {
        printf("PM: Erro ao escalonar %d: %d\n", rmp->endpoint, err);
    }

    return err;
}

/*===========================================================================*
 *                      init_scheduling                                      *
 *===========================================================================*/
void init_scheduling(void)
{
    int r;

    balance_timeout = BALANCE_TIMEOUT * sys_hz();

    if ((r = sys_setalarm(balance_timeout, 0)) != OK)
        panic("sys_setalarm falhou: %d", r);
}

/*===========================================================================*
 *                      balance_queues                                       *
 *===========================================================================*
 * FCFS: esta função NÃO deve alterar a prioridade de processos de usuário.
 *
 * No escalonador padrão ela restaura prioridades rebaixadas pelo aging.
 * No FCFS não existe aging — todos os processos de usuário ficam fixos
 * em FCFS_PRIORITY. Mantemos apenas o realarm do timer.
 */
void balance_queues(void)
{
    struct schedproc *rmp;
    int r, proc_nr;

    for (proc_nr = 0, rmp = schedproc; proc_nr < NR_PROCS; proc_nr++, rmp++) {
        if (rmp->flags & IN_USE) {
            /*
             * FCFS: só corrigimos processos de sistema que eventualmente
             * tenham saído da prioridade correta. Processos de usuário
             * são deixados em FCFS_PRIORITY sem alteração.
             */
            if (is_system_proc(rmp)) {
                if (rmp->priority > rmp->max_priority) {
                    rmp->priority -= 1;
                    schedule_process_local(rmp);
                }
            }
            /* processos de usuário: nenhuma ação — FCFS puro */
        }
    }

    if ((r = sys_setalarm(balance_timeout, 0)) != OK)
        panic("sys_setalarm falhou: %d", r);
}
