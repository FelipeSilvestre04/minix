# MINIX 3 — Escalonadores de Processos

> Trabalho prático da disciplina de Sistemas Operacionais — UNIFESP São José dos Campos.
> Equipe 9: Abner Diniz, Felipe Silvestre, João Vitor de Moura.

Fork do [MINIX 3.4.0rc6](https://github.com/minix3/minix) com implementação e análise comparativa de três algoritmos de escalonamento em espaço de usuário, além de customizações do kernel.

---

## O que foi feito

**Etapa 1 — Customização do kernel**
- Alteração dos banners de boot, login (MOTD) e desligamento com identificação da equipe
- Modificação do `exec.c` para registrar no terminal cada binário executado pelo sistema

**Etapa 2 — Algoritmos de escalonamento**

Todos implementados no arquivo `minix/servers/sched/schedule.c`, sem modificação do kernel:

| Algoritmo | Estratégia |
|---|---|
| **FCFS** | Fila única, quantum de 100.000 ticks, sem penalização por quantum esgotado |
| **Round Robin** | Fila única `USER_Q`, quantum fixo de 200 ticks, sem decaimento de prioridade |
| **Garantido** | Prioriza o processo com menor razão CPU consumida / tempo de vida (C/T), reavaliado a cada segundo via `balance_queues` |

---

## Resultados

Testes executados com `./teste_processos <N> 1000000 100000000`, sendo metade dos processos CPU-bound e metade IO-bound.

### Tempo médio de retorno — IO-bound (s)

| N processos | FCFS | Padrão (MLFQ) | Round Robin | Garantido |
|:-----------:|-----:|-----:|-----:|-----:|
| 10 | 19.01 | **15.77** | 18.47 | 23.69 |
| 50 | 94.37 | **95.82** | 156.00 | 115.54 |
| 100 | 358.70 | **297.48** | 447.43 | 240.91 |
| 200 | 761.22 | **360.50** | 590.00 | 410.20 |

### Tempo médio de retorno — CPU-bound (s)

| N processos | FCFS | Padrão (MLFQ) | Round Robin | Garantido |
|:-----------:|-----:|-----:|-----:|-----:|
| 10 | **0.11** | 0.10 | 0.11 | 0.12 |
| 50 | **0.14** | 0.11 | 0.22 | 0.17 |
| 100 | **0.16** | 0.24 | 0.23 | 0.14 |
| 200 | **0.12** | 0.11 | 0.18 | 0.11 |

O escalonador padrão do MINIX (MLFQ) apresentou o menor tempo de retorno para IO-bound em quase todos os cenários. O FCFS confirmou o **efeito comboio**: tempos IO crescem acentuadamente com N porque processos CPU-bound monopolizam a fila única. O Round Robin foi o pior para IO sob alta carga. O Garantido melhorou progressivamente com N maior, quando o mecanismo de C/T tem mais ciclos para atuar.

---

## Como compilar e executar

> Dentro da máquina virtual MINIX 3.

```sh
# Clonar e trocar de branch
git clone https://github.com/FelipeSilvestre04/minix.git /usr/src
cd /usr/src && git checkout escalonador-fifo

# Recompilar o servidor de escalonamento
cd minix/servers/sched
make clean && make && make install

# Reiniciar para ativar o escalonador
reboot

# Compilar e rodar o teste
cc -O2 -o teste_processos teste_processos.c
./teste_processos 100 1000000 100000000
```

---

## Referências

- TANENBAUM, A. S.; BOS, H. **Sistemas Operacionais Modernos**. 4ª ed. Pearson, 2015.
- [Código-fonte MINIX 3 oficial](https://github.com/minix3/minix)
- [Vídeo demonstrativo](https://youtu.be/H1M9k9bCIeU)
