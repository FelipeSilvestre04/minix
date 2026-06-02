#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ========================================================================== *
 *                     CONSTANTES E DEFINIÇÕES DO MINIX                       *
 * ========================================================================== */
#define NR_PROCS                 1024  /* Suporta simulações de até 1024 procs */
#define USER_Q                     7   /* Prioridade padrão (máxima de usuário) */
#define MIN_USER_Q                14   /* Prioridade mínima de usuário */
#define NR_SCHED_QUEUES           16
#define DEFAULT_USER_TIME_SLICE    8   /* Quantum padrão em ticks */

#define IN_USE                0x0001   /* Flag indicando slot em uso */

/* Modos do Escalonador para o Playground */
#define ESCOLA_PADRAO              0   /* Padrão do Minix (Multifilas com Feedback) */
#define ESCOLA_ESTATICO            1   /* Algoritmo 1: Prioridade Estática (Sem Feedback) */
#define ESCOLA_LOTERIA             2   /* Algoritmo 2: Escalonamento por Loteria */

/* CHAVE DE ALGORITMO: Altere aqui ou passe por argumento */
int modo_escalonador = ESCOLA_PADRAO;

typedef int endpoint_t;

/* Estrutura de processo */
struct schedproc {
    endpoint_t endpoint;     
    unsigned flags;          

    unsigned max_priority;   
    unsigned priority;       
    unsigned time_slice;     

    /* --- Campos de Simulação de Carga (equivalente a teste_processos) --- */
    char name[32];           
    int type;                /* 1 = CPU-Bound, 2 = IO-Bound */
    int state;               /* 0 = PRONTO, 1 = BLOQUEADO, 2 = TERMINADO */
    int blocked_ticks_left;  
    
    long long work_remaining; /* Operações restantes a processar */
    int start_tick;          
    int end_tick;            
} schedproc[NR_PROCS];

/* ========================================================================== *
 *                LÓGICAS DOS ALGORITMOS DE ESCALONAMENTO                     *
 * ========================================================================== */

/* Simula a chamada do_noquantum() do Minix quando o tempo de CPU expira */
void do_noquantum(int proc_nr)
{
    struct schedproc *rmp = &schedproc[proc_nr];

    if (modo_escalonador == ESCOLA_PADRAO) {
        /* Padrão: Punir decaindo de prioridade */
        if (rmp->priority < MIN_USER_Q) {
            rmp->priority += 1; 
        }
    } 
    /* Nos modos Estático e Loteria, a prioridade não decai dinamicamente */
}

/* Simula a readequação periódica de filas (balanceamento) do Minix */
void balance_queues(void)
{
    struct schedproc *rmp;
    int proc_nr;

    if (modo_escalonador == ESCOLA_PADRAO) {
        for (proc_nr = 0, rmp = schedproc; proc_nr < NR_PROCS; proc_nr++, rmp++) {
            if (rmp->flags & IN_USE) {
                if (rmp->priority > rmp->max_priority) {
                    rmp->priority -= 1; /* Promove (sobe de prioridade) */
                }
            }
        }
    }
    else if (modo_escalonador == ESCOLA_LOTERIA) {
        /* No modo Loteria, não usamos filas de prioridade para escalonar */
        printf("\n[INFO] balance_queues() ignorado no modo Loteria.\n");
    }
}

/* ========================================================================== *
 *                         MOCK HARNESS DE SIMULAÇÃO                         *
 * ========================================================================== */

/* Inicializa nproc processos simulando a distribuição do teste_processos */
void init_simulation_workload(int nproc, long long io_ops, long long cpu_ops, int start_tick)
{
    memset(schedproc, 0, sizeof(schedproc));

    for (int num = 0; num < nproc; num++) {
        struct schedproc *rmp = &schedproc[num];
        rmp->endpoint = 1000 + num;
        rmp->flags = IN_USE;
        rmp->max_priority = USER_Q;
        rmp->priority = USER_Q;
        rmp->time_slice = DEFAULT_USER_TIME_SLICE;
        rmp->state = 0;
        rmp->start_tick = start_tick;

        if ((num % 2) == 0) {
            /* IO-Bound */
            sprintf(rmp->name, "IO_Proc_%d", num);
            rmp->type = 2;
            rmp->work_remaining = io_ops;
        } else {
            /* CPU-Bound */
            sprintf(rmp->name, "CPU_Proc_%d", num);
            rmp->type = 1;
            rmp->work_remaining = cpu_ops;
        }
    }
}

/* Encontra o processo com maior prioridade que está pronto para rodar */
int pick_process(int nproc)
{
    if (modo_escalonador == ESCOLA_LOTERIA) {
        int total_tickets = 0;
        for (int i = 0; i < nproc; i++) {
            if (schedproc[i].flags & IN_USE && schedproc[i].state == 0) {
                /* Processos de IO ganham mais bilhetes (50) do que CPU (10) para favorecer E/S */
                total_tickets += (schedproc[i].type == 2) ? 50 : 10;
            }
        }
        if (total_tickets == 0) return -1;

        int winner = rand() % total_tickets;
        int current_sum = 0;
        for (int i = 0; i < nproc; i++) {
            if (schedproc[i].flags & IN_USE && schedproc[i].state == 0) {
                current_sum += (schedproc[i].type == 2) ? 50 : 10;
                if (current_sum > winner) {
                    return i;
                }
            }
        }
        return -1;
    }

    /* Modos Padrão e Estático: Seleciona da fila de prontos de maior prioridade */
    int best_prio = 999;
    int best_idx = -1;

    for (int i = 0; i < nproc; i++) {
        if (schedproc[i].flags & IN_USE && schedproc[i].state == 0) {
            /* No Minix, menor número significa maior prioridade */
            if ((int)schedproc[i].priority < best_prio) {
                best_prio = schedproc[i].priority;
                best_idx = i;
            }
        }
    }
    return best_idx;
}

/* Desbloqueia processos que estavam esperando IO */
void update_blocked_processes(int nproc)
{
    for (int i = 0; i < nproc; i++) {
        if (schedproc[i].flags & IN_USE && schedproc[i].state == 1) {
            schedproc[i].blocked_ticks_left--;
            if (schedproc[i].blocked_ticks_left <= 0) {
                schedproc[i].state = 0; /* Fica PRONTO novamente */
            }
        }
    }
}

/* Verifica se todos os processos terminaram o trabalho */
int all_terminated(int nproc)
{
    for (int i = 0; i < nproc; i++) {
        if (schedproc[i].flags & IN_USE && schedproc[i].state != 2) {
            return 0; /* Ainda há processos rodando */
        }
    }
    return 1; /* Todos terminaram */
}

int main(int argc, char **argv)
{
    int nproc = 100;
    long long io_ops = 1000000;
    long long cpu_ops = 100000000;

    if (argc >= 4) {
        nproc = atoi(argv[1]);
        io_ops = atoll(argv[2]);
        cpu_ops = atoll(argv[3]);
    }
    if (argc >= 5) {
        modo_escalonador = atoi(argv[4]);
    }

    if (nproc > NR_PROCS) {
        printf("Erro: número de processos excede o limite do simulador (%d).\n", NR_PROCS);
        return -1;
    }

    srand(time(NULL));
    init_simulation_workload(nproc, io_ops, cpu_ops, 1);

    printf("================================================================\n");
    printf("     PLAYGROUND: SIMULANDO CARGA DE 'teste_processos'\n");
    printf("     Algoritmo: %s\n", (modo_escalonador == ESCOLA_ESTATICO) ? "PRIORIDADE ESTATICA" : 
                             ((modo_escalonador == ESCOLA_LOTERIA) ? "ESCALONAMENTO POR LOTERIA" : "PADRAO MINIX"));
    printf("     Config: %d Processos (50%% CPU, 50%% IO)\n", nproc);
    printf("================================================================\n");

    int tick = 0;
    int balance_interval = 20;

    /* Loop principal da CPU virtual */
    while (!all_terminated(nproc)) {
        tick++;

        /* Desbloqueia processos cujo IO terminou */
        update_blocked_processes(nproc);

        /* Escolhe processo da maior fila de prontos */
        int run_idx = pick_process(nproc);

        if (run_idx != -1) {
            struct schedproc *curr = &schedproc[run_idx];
            curr->time_slice--;

            /* Simula a execução do trabalho do processo neste tick */
            if (curr->type == 1) {
                /* CPU-Bound processa 5.000.000 de operações de CPU por tick */
                curr->work_remaining -= 5000000; 
            } else {
                /* IO-Bound processa 50.000 operações de IO por tick (IO é mais lento) */
                curr->work_remaining -= 50000;

                /* 30% de chance de bloquear para simular a chamada de sistema do IO */
                if (curr->work_remaining > 0 && (rand() % 100 < 30)) {
                    curr->state = 1;
                    curr->blocked_ticks_left = 2 + (rand() % 3); /* Bloqueia por 2 a 4 ticks */
                }
            }

            /* Se terminou seu trabalho, finaliza o processo */
            if (curr->work_remaining <= 0) {
                curr->state = 2; /* TERMINADO */
                curr->end_tick = tick;
            }

            /* Se esgotou o quantum sem terminar, aplica a punição/readequação */
            if (curr->state == 0 && curr->time_slice == 0) {
                do_noquantum(run_idx);
                curr->time_slice = DEFAULT_USER_TIME_SLICE;
            }
        }

        /* Aplica o rebalanceamento dinâmico periódico */
        if (tick % balance_interval == 0) {
            balance_queues();
        }

        /* Proteção contra loop infinito */
        if (tick > 50000) {
            printf("[ERRO] Simulação excedeu 50000 ticks. Abortando.\n");
            break;
        }
    }

    /* Calcula os tempos médios de retorno (turnaround time em ticks) */
    double sum_cpu_turnaround = 0;
    double sum_io_turnaround = 0;
    int count_cpu = 0;
    int count_io = 0;

    for (int i = 0; i < nproc; i++) {
        double turnaround = schedproc[i].end_tick - schedproc[i].start_tick;
        if (schedproc[i].type == 1) {
            sum_cpu_turnaround += turnaround;
            count_cpu++;
        } else {
            sum_io_turnaround += turnaround;
            count_io++;
        }
    }

    printf("\n=== RESULTADOS SIMULADOS DA EXECUÇÃO ===\n");
    printf("Total de ticks simulados: %d\n", tick);
    printf("Tempo médio de retorno (CPU-Bound): %.2f ticks\n", sum_cpu_turnaround / count_cpu);
    printf("Tempo médio de retorno (IO-Bound):  %.2f ticks\n", sum_io_turnaround / count_io);
    printf("================================================================\n");

    return 0;
}
