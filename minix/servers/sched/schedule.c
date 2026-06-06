/* This file contains the scheduling policy for SCHED
 *
 * ALGORITMO: Primeiro a Chegar, Primeiro a Ser Servido (FCFS/FIFO)
 *
 * Conforme Tanenbaum (4a ed.), secao 2.4.2:
 *   - Nao-preemptivo: um processo roda ate bloquear ou terminar
 *   - Fila unica: processos entram no fim e saem do inicio
 *   - Simples e justo em termos de ordem de chegada
 *
 * Correcoes aplicadas em relacao a versao anterior:
 *
 *   [C1] do_noquantum: removido bloco else com logica invertida.
 *        O codigo anterior fazia `priority += 1` quando
 *        `priority < MIN_USER_Q`, condicao que nunca e verdadeira
 *        para processos de usuario (MIN_USER_Q e o piso da faixa
 *        de usuario, nao o teto do sistema). O bloco inteiro foi
 *        substituido por uma atribuicao direta: USER_Q.
 *
 *   [C2] do_start_scheduling: heranca de prioridade do pai tornada
 *        explicita antes do switch, evitando dependencia fragil da
 *        ordem dos blocos para corrigir herancas de processos de
 *        sistema.
 *
 * The entry points are:
 *   do_noquantum:        Called on behalf of process' that run out of quantum
 *   do_start_scheduling  Request to start scheduling a proc
 *   do_stop_scheduling   Request to stop scheduling a proc
 *   do_nice              Request to change the nice level on a proc
 *   init_scheduling      Called from main.c to set up/prepare scheduling
 */
#include "sched.h"
#include "schedproc.h"
#include <assert.h>
#include <minix/com.h>
#include <machine/archtypes.h>

static unsigned balance_timeout;

#define BALANCE_TIMEOUT     5       /* how often to balance queues in seconds */

/*
 * FIFO: quantum "infinito" — grande o suficiente para que o processo
 * nao seja preemptado pelo timer em condicoes normais.
 * O processo so deixa a CPU ao bloquear (E/S) ou terminar.
 */
#define FCFS_TIME_SLICE     100000

/*
 * Fila unica para todos os processos de usuario.
 * USER_Q e definido em kernel/proc.h.
 * Fixar todos em USER_Q garante insercao no tail da mesma fila
 * (comportamento FIFO por construcao do kernel).
 */
#define FCFS_PRIORITY       USER_Q

static int schedule_process(struct schedproc *rmp, unsigned flags);

#define SCHEDULE_CHANGE_PRIO    0x1
#define SCHEDULE_CHANGE_QUANTUM 0x2
#define SCHEDULE_CHANGE_CPU     0x4

#define SCHEDULE_CHANGE_ALL (        \
        SCHEDULE_CHANGE_PRIO    |    \
        SCHEDULE_CHANGE_QUANTUM |    \
        SCHEDULE_CHANGE_CPU          \
        )

#define schedule_process_local(p) \
    schedule_process(p, SCHEDULE_CHANGE_PRIO | SCHEDULE_CHANGE_QUANTUM)
#define schedule_process_migrate(p) \
    schedule_process(p, SCHEDULE_CHANGE_CPU)

#define CPU_DEAD    -1

#define cpu_is_available(c) (cpu_proc[c] >= 0)

/* processes created by RS are system processes */
#define is_system_proc(p)   ((p)->parent == RS_PROC_NR)

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

    /* schedule system processes only on the boot cpu */
    if (is_system_proc(proc)) {
        proc->cpu = machine.bsp_id;
        return;
    }

    /* if no other cpu available, try BSP */
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
 * FIFO: com FCFS_TIME_SLICE = 100000 isso raramente ocorre, mas se
 * ocorrer (processo muito longo), o processo volta para o FIM da fila
 * com a MESMA prioridade USER_Q — preservando a ordem FIFO.
 *
 * CORRECAO [C1]: versao anterior tinha bloco else com condicao
 * `priority < MIN_USER_Q` que nunca e atingida por processos de
 * usuario, tornando o codigo morto e enganoso. Removido. A semantica
 * correta e simples: todo processo que chegou aqui e de usuario,
 * mantem USER_Q sem penalizacao.
 */
int do_noquantum(message *m_ptr)
{
    register struct schedproc *rmp;
    int rv, proc_nr_n;

    if (sched_isokendpt(m_ptr->m_source, &proc_nr_n) != OK) {
        printf("SCHED: WARNING: got an invalid endpoint in OOQ msg %u.\n",
               m_ptr->m_source);
        return EBADEPT;
    }

    rmp = &schedproc[proc_nr_n];

    /*
     * [C1] FIFO: atribuicao direta, sem condicional.
     * Processos de usuario ficam sempre em FCFS_PRIORITY (== USER_Q).
     * Processos de sistema nao chegam aqui normalmente; se chegarem,
     * manter a prioridade atual e o comportamento mais seguro.
     */
    if (rmp->max_priority >= USER_Q) {
        /* processo de usuario: fixa em FCFS_PRIORITY */
        rmp->priority = FCFS_PRIORITY;
    }
    /* processos de sistema: nao alteramos a prioridade aqui;
     * balance_queues cuida deles */

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
        printf("SCHED: WARNING: got an invalid endpoint in OOQ msg %d\n",
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
 * CORRECAO [C2]: a versao anterior dependia de um bloco
 * `if (rmp->priority >= USER_Q)` APOS o switch para corrigir herancas
 * indevidas de processos de sistema. Isso e fragil: se a ordem dos
 * blocos mudar, o comportamento quebra silenciosamente.
 *
 * Agora: a classificacao usuario/sistema e feita DENTRO de cada case,
 * de forma explicita e independente da ordem.
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
            &proc_nr_n)) != OK)
        return rv;

    rmp = &schedproc[proc_nr_n];

    rmp->endpoint     = m_ptr->m_lsys_sched_scheduling_start.endpoint;
    rmp->parent       = m_ptr->m_lsys_sched_scheduling_start.parent;
    rmp->max_priority = m_ptr->m_lsys_sched_scheduling_start.maxprio;
    if (rmp->max_priority >= NR_SCHED_QUEUES)
        return EINVAL;

    /* caso especial: init e pai de si mesmo */
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
         * Processos de sistema recebem a prioridade e quantum
         * explicitamente definidos pelo RS — nao alteramos.
         * Processos de usuario: FCFS_PRIORITY + FCFS_TIME_SLICE.
         */
        if (is_system_proc(rmp)) {
            rmp->priority   = rmp->max_priority;
            rmp->time_slice = m_ptr->m_lsys_sched_scheduling_start.quantum;
        } else {
            /* [C2] classificacao explicita aqui, nao apos o switch */
            rmp->priority   = FCFS_PRIORITY;
            rmp->time_slice = FCFS_TIME_SLICE;
        }
        break;

    case SCHEDULING_INHERIT:
        if ((rv = sched_isokendpt(
                m_ptr->m_lsys_sched_scheduling_start.parent,
                &parent_nr_n)) != OK)
            return rv;

        if (is_system_proc(rmp)) {
            /* herda do pai normalmente */
            rmp->priority   = schedproc[parent_nr_n].priority;
            rmp->time_slice = schedproc[parent_nr_n].time_slice;
        } else {
            /* [C2] FIFO: ignora heranca de prioridade do pai;
             * todo filho de usuario entra na mesma fila FIFO */
            rmp->priority   = FCFS_PRIORITY;
            rmp->time_slice = FCFS_TIME_SLICE;
        }
        break;

    default:
        assert(0);
    }

    if ((rv = sys_schedctl(0, rmp->endpoint, 0, 0, 0)) != OK) {
        printf("Sched: Error taking over scheduling for %d, kernel said %d\n",
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
        printf("Sched: Error while scheduling process, kernel replied %d\n", rv);
        return rv;
    }

    m_ptr->m_sched_lsys_scheduling_start.scheduler = SCHED_PROC_NR;

    return OK;
}

/*===========================================================================*
 *                          do_nice                                          *
 *===========================================================================*
 * FIFO: nice e ignorado para processos de usuario — todos tem mesma
 * prioridade FCFS_PRIORITY. Para processos de sistema, comportamento
 * original e mantido.
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
        printf("SCHED: WARNING: got an invalid endpoint in OoQ msg %d\n",
               m_ptr->m_pm_sched_scheduling_set_nice.endpoint);
        return EBADEPT;
    }

    rmp = &schedproc[proc_nr_n];
    new_q = m_ptr->m_pm_sched_scheduling_set_nice.maxprio;
    if (new_q >= NR_SCHED_QUEUES)
        return EINVAL;

    old_q     = rmp->priority;
    old_max_q = rmp->max_priority;

    if (is_system_proc(rmp)) {
        rmp->max_priority = rmp->priority = new_q;
    } else {
        /* FIFO: usuarios sempre em FCFS_PRIORITY, nice nao tem efeito */
        rmp->max_priority = FCFS_PRIORITY;
        rmp->priority     = FCFS_PRIORITY;
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
        printf("PM: An error occurred when trying to schedule %d: %d\n",
               rmp->endpoint, err);
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
        panic("sys_setalarm failed: %d", r);
}

/*===========================================================================*
 *                      balance_queues                                       *
 *===========================================================================*
 * FIFO: processos de usuario NAO sao rebalanceados — todos ficam fixos
 * em FCFS_PRIORITY sem aging. Apenas processos de sistema que tenham
 * sido rebaixados sao restaurados aqui.
 */
void balance_queues(void)
{
    struct schedproc *rmp;
    int r, proc_nr;

    for (proc_nr = 0, rmp = schedproc; proc_nr < NR_PROCS; proc_nr++, rmp++) {
        if (rmp->flags & IN_USE) {
            if (!is_system_proc(rmp)) {
                /* FIFO: garante que usuario nao saiu de FCFS_PRIORITY
                 * por nenhum caminho inesperado */
                if (rmp->priority != FCFS_PRIORITY) {
                    rmp->priority = FCFS_PRIORITY;
                    schedule_process_local(rmp);
                }
            } else {
                /* processos de sistema: restaura prioridade se rebaixada */
                if (rmp->priority > rmp->max_priority) {
                    rmp->priority -= 1;
                    schedule_process_local(rmp);
                }
            }
        }
    }

    if ((r = sys_setalarm(balance_timeout, 0)) != OK)
        panic("sys_setalarm failed: %d", r);
}