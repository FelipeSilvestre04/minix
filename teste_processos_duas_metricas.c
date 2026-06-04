#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>

#define SEC(tv) (tv.tv_sec + tv.tv_usec/1e6)

int main(int argc, char **argv) {
        struct timeval p_start, p_end, p_time;
        int *pid;
        unsigned long int x=1;
        int num, nproc, io_ops, cpu_ops;
        long int i=0;
        if (argc != 4) {
                printf("Uso comando: %s <num_procs> <IO_ops> <CPU_ops>\n"
                        " <num_procs>: numero total de processos.\n"
                        " <IO_ops>: numero de operacoes de IO por processo\n"
                        " <CPU_ops>: numero de operacoes de CPU por processo\n",
                        argv[0]);
                return 0;
        }
        nproc = atoi(argv[1]);
        io_ops = atoi(argv[2]);
        cpu_ops = atoi(argv[3]);

        pid = (int *)calloc(nproc, sizeof(int));
        if (!pid){
                perror("calloc()");
                return -1;
        }

        for(num=0; num<nproc; num++) {
                pid[num]=fork();
                if(pid[num]==0) {
                        struct rusage usage;
                        double avg_queue_time = 0.0;
                        unsigned long total_context_switches;

                        // Se num for par, filho eh IO bound, senao eh CPU bound
                        if((num % 2) == 0) {
                                gettimeofday(&p_start, NULL);
                                for(i=0; i<io_ops; i++){
                                        fprintf(stderr, "Proc:%d i=%ld\n", num, i);
                                        fflush(stderr);
                                }
                                gettimeofday(&p_end, NULL);
                                timersub(&p_end, &p_start, &p_time);

                                // Obter estatisticas de contabilidade do kernel via getrusage
                                if (getrusage(RUSAGE_SELF, &usage) == 0) {
                                        total_context_switches = usage.ru_nivcsw + usage.ru_nvcsw;
                                        if (total_context_switches > 0) {
                                                // ru_maxrss contem o tempo de fila em ms. Convertemos para segundos.
                                                avg_queue_time = (usage.ru_maxrss / 1000.0) / total_context_switches;
                                        }
                                }
                                printf("IO\t %d\t %g\t %g\n", num, SEC(p_time), avg_queue_time);
                        } else {
                                gettimeofday(&p_start, NULL);
                                for(i=0; i<cpu_ops; i++) {
                                        x = (x << 4) - (x << 4);
                                }
                                gettimeofday(&p_end, NULL);
                                timersub(&p_end, &p_start, &p_time);

                                // Obter estatisticas de contabilidade do kernel via getrusage
                                if (getrusage(RUSAGE_SELF, &usage) == 0) {
                                        total_context_switches = usage.ru_nivcsw + usage.ru_nvcsw;
                                        if (total_context_switches > 0) {
                                                avg_queue_time = (usage.ru_maxrss / 1000.0) / total_context_switches;
                                        }
                                }
                                printf("CPU\t %d\t %g\t %g\n", num, SEC(p_time), avg_queue_time);
                        }
                        exit(0); // todo filho termina aqui ...
                }
        }
        // pai apenas aguarda o termino dos filhos
        for(i=0; i<nproc; i++) {
                wait(NULL);
        }
        free(pid);
        return 0;
}
