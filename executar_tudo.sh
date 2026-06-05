#!/bin/sh

# Script para automatizar a bateria de testes de todos os 4 escalonadores (MLFQ/Padrao, FIFO, RR, Garantido)
# com reboots semi-assistidos.

STATE_FILE="estado_testes.txt"
RESULTS_FILE="todos_resultados.txt"
DIR_BASE=$(pwd)

# Ajuste dos parâmetros padrão de benchmark do roteiro
IO_OPS=${1:-1000000}
CPU_OPS=${2:-100000000}

# Função para mudar de escalonador automaticamente (sem interações do usuário)
mudar_escalonador() {
    local alg=$1
    echo "Configurando escalonador: $alg..."
    if [ -f "./trocar_escalonador.sh" ]; then
        echo "s" | ./trocar_escalonador.sh "$alg"
    else
        echo "Erro: trocar_escalonador.sh não encontrado em $DIR_BASE"
        exit 1
    fi
}

# Determinar estado
if [ ! -f "$STATE_FILE" ]; then
    ESTADO="inicio"
else
    ESTADO=$(cat "$STATE_FILE")
fi

case "$ESTADO" in
    inicio)
        echo "============================================================="
        echo " INICIANDO BATERIA COMPLETA DE TESTES (4 ESCALONADORES)"
        echo "============================================================="
        echo "Passo 1: Configurar Escalonador PADRAO (MLFQ)"
        mudar_escalonador padrao
        echo "esperando_padrao" > "$STATE_FILE"
        echo "============================================================="
        echo " ESCALONADOR PADRAO INSTALADO!"
        echo " Por favor, reinicie a VM agora digitando: reboot"
        echo " Ao ligar o Minix novamente, execute: ./executar_tudo.sh"
        echo "============================================================="
        ;;

    esperando_padrao)
        echo "============================================================="
        echo " RODANDO TESTES PARA: PADRAO (MLFQ)"
        echo "============================================================="
        if [ -f "./rodar_testes.sh" ]; then
            ./rodar_testes.sh padrao "$IO_OPS" "$CPU_OPS"
            echo "--- ESCALONADOR PADRAO ---" >> "$RESULTS_FILE"
            for n in 10 50 100 200; do
                echo "Cenario $n processos:" >> "$RESULTS_FILE"
                cat "resultado_padrao_${n}.txt" >> "$RESULTS_FILE"
                echo "" >> "$RESULTS_FILE"
            done
        else
            echo "Erro: rodar_testes.sh não encontrado!"
            exit 1
        fi
        
        echo "Passo 2: Configurar Escalonador FIFO"
        mudar_escalonador fifo
        echo "esperando_fifo" > "$STATE_FILE"
        echo "============================================================="
        echo " ESCALONADOR FIFO INSTALADO!"
        echo " Por favor, reinicie a VM agora digitando: reboot"
        echo " Ao ligar o Minix novamente, execute: ./executar_tudo.sh"
        echo "============================================================="
        ;;

    esperando_fifo)
        echo "============================================================="
        echo " RODANDO TESTES PARA: FIFO (FCFS)"
        echo "============================================================="
        if [ -f "./rodar_testes.sh" ]; then
            ./rodar_testes.sh fifo "$IO_OPS" "$CPU_OPS"
            echo "--- ESCALONADOR FIFO ---" >> "$RESULTS_FILE"
            for n in 10 50 100 200; do
                echo "Cenario $n processos:" >> "$RESULTS_FILE"
                cat "resultado_fifo_${n}.txt" >> "$RESULTS_FILE"
                echo "" >> "$RESULTS_FILE"
            done
        else
            echo "Erro: rodar_testes.sh não encontrado!"
            exit 1
        fi
        
        echo "Passo 3: Configurar Escalonador ROUND ROBIN (RR)"
        mudar_escalonador rr
        echo "esperando_rr" > "$STATE_FILE"
        echo "============================================================="
        echo " ESCALONADOR ROUND ROBIN INSTALADO!"
        echo " Por favor, reinicie a VM agora digitando: reboot"
        echo " Ao ligar o Minix novamente, execute: ./executar_tudo.sh"
        echo "============================================================="
        ;;

    esperando_rr)
        echo "============================================================="
        echo " RODANDO TESTES PARA: ROUND ROBIN (RR)"
        echo "============================================================="
        if [ -f "./rodar_testes.sh" ]; then
            ./rodar_testes.sh rr "$IO_OPS" "$CPU_OPS"
            echo "--- ESCALONADOR ROUND ROBIN ---" >> "$RESULTS_FILE"
            for n in 10 50 100 200; do
                echo "Cenario $n processos:" >> "$RESULTS_FILE"
                cat "resultado_rr_${n}.txt" >> "$RESULTS_FILE"
                echo "" >> "$RESULTS_FILE"
            done
        else
            echo "Erro: rodar_testes.sh não encontrado!"
            exit 1
        fi
        
        echo "Passo 4: Configurar Escalonador GARANTIDO (FAIR-SHARE)"
        mudar_escalonador garantido
        echo "esperando_garantido" > "$STATE_FILE"
        echo "============================================================="
        echo " ESCALONADOR GARANTIDO INSTALADO!"
        echo " Por favor, reinicie a VM agora digitando: reboot"
        echo " Ao ligar o Minix novamente, execute: ./executar_tudo.sh"
        echo "============================================================="
        ;;

    esperando_garantido)
        echo "============================================================="
        echo " RODANDO TESTES PARA: GARANTIDO (FAIR-SHARE)"
        echo "============================================================="
        if [ -f "./rodar_testes.sh" ]; then
            ./rodar_testes.sh garantido "$IO_OPS" "$CPU_OPS"
            echo "--- ESCALONADOR GARANTIDO ---" >> "$RESULTS_FILE"
            for n in 10 50 100 200; do
                echo "Cenario $n processos:" >> "$RESULTS_FILE"
                cat "resultado_garantido_${n}.txt" >> "$RESULTS_FILE"
                echo "" >> "$RESULTS_FILE"
            done
        else
            echo "Erro: rodar_testes.sh não encontrado!"
            exit 1
        fi
        
        echo "finalizado" > "$STATE_FILE"
        echo "============================================================="
        echo " BATERIA COMPLETA DE TESTES CONCLUÍDA COM SUCESSO!"
        echo " Todos os resultados consolidados foram salvos em:"
        echo "   $DIR_BASE/$RESULTS_FILE"
        echo " Você já pode fazer o upload desse arquivo ou gerar os gráficos!"
        echo "============================================================="
        rm -f "$STATE_FILE" # Limpa o arquivo de estado para uma futura execução
        ;;

    *)
        echo "Estado desconhecido: $ESTADO. Reiniciando fluxo..."
        rm -f "$STATE_FILE"
        exec $0 "$IO_OPS" "$CPU_OPS"
        ;;
esac
