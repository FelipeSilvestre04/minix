#!/bin/sh

# Script: executar_tudo_verificado.sh
# Finalidade: Automatizar a compilação, atualização do boot, verificação do escalonador ativo
#            (evitando rodar testes no escalonador errado) e consolidação dos resultados de 4 colunas.

STATE_FILE="estado_verificado.txt"
RESULTS_FILE="resultados_consolidados.txt"
DIR_SRC="/usr/src"
DIR_SCHED="$DIR_SRC/minix/servers/sched"
DIR_RELEASetools="$DIR_SRC/releasetools"

# Caso esteja rodando fora do local padrão (ex: testando no WSL)
if [ ! -d "$DIR_SCHED" ]; then
    DIR_SCHED="./minix/servers/sched"
    DIR_RELEASetools="./releasetools"
fi

IO_OPS=${1:-1000000}
CPU_OPS=${2:-100000000}

# Função para mudar, compilar e atualizar boot
compilar_e_preparar() {
    local alg=$1
    local src_file="$DIR_SCHED/schedule_${alg}.c"
    
    echo "-------------------------------------------------------------"
    echo " 1. Copiando codigo de schedule_${alg}.c -> schedule.c"
    echo "-------------------------------------------------------------"
    cp "$src_file" "$DIR_SCHED/schedule.c"
    if [ $? -ne 0 ]; then
        echo "Erro ao copiar arquivo para $alg. Voce esta como root?"
        exit 1
    fi
    
    echo "-------------------------------------------------------------"
    echo " 2. Compilando e instalando o servidor sched"
    echo "-------------------------------------------------------------"
    (cd "$DIR_SCHED" && make && make install)
    if [ $? -ne 0 ]; then
        echo "ERRO de compilacao no sched!"
        exit 1
    fi
    
    echo "-------------------------------------------------------------"
    echo " 3. Atualizando imagem de boot (make hdboot)"
    echo "-------------------------------------------------------------"
    (cd "$DIR_RELEASetools" && make hdboot)
    if [ $? -ne 0 ]; then
        echo "ERRO ao gerar hdboot!"
        exit 1
    fi
    
    echo "-------------------------------------------------------------"
    echo " Sucesso ao preparar o escalonador: $alg"
    echo "-------------------------------------------------------------"
}

# Função para validar qual escalonador está realmente rodando no boot do Minix
verificar_escalonador_ativo() {
    local esperado=$1
    local log_msg=""
    
    # Tenta obter do dmesg (filtrando a mensagem genérica do main.c)
    log_msg=$(dmesg | grep "ESCALONADOR" | grep -v "CUSTOMIZADO" | tail -n 1)
    
    # Se vazio, tenta /var/log/messages
    if [ -z "$log_msg" ] && [ -f "/var/log/messages" ]; then
        log_msg=$(grep "ESCALONADOR" /var/log/messages | grep -v "CUSTOMIZADO" | tail -n 1)
    fi
    
    if [ -z "$log_msg" ]; then
        echo "Aviso: Nao foi possivel ler as mensagens de boot para verificar o escalonador."
        echo "Certifique-se de que a VM foi de fato REINICIADA apos o make hdboot."
        echo "Prosseguindo sob sua responsabilidade..."
        return 0
    fi
    
    echo "Detectado no Log de Boot: $log_msg"
    case "$esperado" in
        padrao)
            echo "$log_msg" | grep -q -i "PADRAO" || return 1
            ;;
        fifo)
            echo "$log_msg" | grep -q -i "FIFO" || return 1
            ;;
        rr)
            echo "$log_msg" | grep -q -i "ROUND ROBIN" || return 1
            ;;
        garantido)
            echo "$log_msg" | grep -q -i "GARANTIDO" || return 1
            ;;
    esac
    return 0
}

# Executar bateria de testes para o escalonador ativo
rodar_bateria_testes() {
    local alg=$1
    local bin_teste="./teste_processos_duas_metricas"
    
    # Compilar utilitário de teste se necessário
    if [ ! -f "$bin_teste" ]; then
        echo "Compilando teste_processos_duas_metricas.c..."
        clang -o "$bin_teste" "teste_processos_duas_metricas.c" || \
        gcc -o "$bin_teste" "teste_processos_duas_metricas.c" || \
        cc -o "$bin_teste" "teste_processos_duas_metricas.c"
    fi
    
    echo "============================================================="
    echo " INICIANDO TESTES PARA O ESCALONADOR: $alg (Metricas: Turnaround + Fila)"
    echo " Parametros: IO=$IO_OPS | CPU=$CPU_OPS"
    echo "============================================================="
    
    echo "### ESCALONADOR: $alg ###" >> "$RESULTS_FILE"
    
    for n in 10 50 100 200; do
        echo "Executando cenario com $n processos..."
        echo "Cenario $n processos:" >> "$RESULTS_FILE"
        
        # Executa silenciando stderr (prints dos filhos de IO) e salvando stdout no arquivo final
        "$bin_teste" "$n" "$IO_OPS" "$CPU_OPS" >> "$RESULTS_FILE" 2>/dev/null
        
        echo "" >> "$RESULTS_FILE"
        echo "Cenario $n concluido!"
    done
    
    echo "Testes para $alg gravados em $RESULTS_FILE com sucesso."
    echo "============================================================="
}

# Fluxo principal de estados
if [ ! -f "$STATE_FILE" ]; then
    ESTADO="inicio"
else
    ESTADO=$(cat "$STATE_FILE")
fi

case "$ESTADO" in
    inicio)
        echo "Preparando o escalonador PADRAO (MLFQ)..."
        compilar_e_preparar padrao
        echo "esperando_padrao" > "$STATE_FILE"
        echo "============================================================="
        echo " Passo 1: ESCALONADOR PADRAO INSTALADO!"
        echo " Digite: reboot"
        echo " Ao ligar a VM de novo, execute: ./executar_tudo_verificado.sh"
        echo "============================================================="
        ;;
        
    esperando_padrao)
        echo "Verificando se o escalonador ativo e o PADRAO..."
        verificar_escalonador_ativo padrao
        if [ $? -ne 0 ]; then
            echo "ERRO CRITICO: O escalonador ativo no boot nao e o PADRAO!"
            echo "Por favor, garanta que reiniciou a VM apos a instalacao."
            exit 1
        fi
        
        rodar_bateria_testes padrao
        
        echo "Preparando o escalonador FIFO..."
        compilar_e_preparar fifo
        echo "esperando_fifo" > "$STATE_FILE"
        echo "============================================================="
        echo " Passo 2: ESCALONADOR FIFO INSTALADO!"
        echo " Digite: reboot"
        echo " Ao ligar a VM de novo, execute: ./executar_tudo_verificado.sh"
        echo "============================================================="
        ;;

    esperando_fifo)
        echo "Verificando se o escalonador ativo e o FIFO..."
        verificar_escalonador_ativo fifo
        if [ $? -ne 0 ]; then
            echo "ERRO CRITICO: O escalonador ativo no boot nao e o FIFO!"
            echo "Por favor, garanta que reiniciou a VM apos a instalacao."
            exit 1
        fi
        
        rodar_bateria_testes fifo
        
        echo "Preparando o escalonador ROUND ROBIN..."
        compilar_e_preparar rr
        echo "esperando_rr" > "$STATE_FILE"
        echo "============================================================="
        echo " Passo 3: ESCALONADOR ROUND ROBIN INSTALADO!"
        echo " Digite: reboot"
        echo " Ao ligar a VM de novo, execute: ./executar_tudo_verificado.sh"
        echo "============================================================="
        ;;

    esperando_rr)
        echo "Verificando se o escalonador ativo e o ROUND ROBIN..."
        verificar_escalonador_ativo rr
        if [ $? -ne 0 ]; then
            echo "ERRO CRITICO: O escalonador ativo no boot nao e o ROUND ROBIN!"
            echo "Por favor, garanta que reiniciou a VM apos a instalacao."
            exit 1
        fi
        
        rodar_bateria_testes rr
        
        echo "Preparando o escalonador GARANTIDO..."
        compilar_e_preparar garantido
        echo "esperando_garantido" > "$STATE_FILE"
        echo "============================================================="
        echo " Passo 4: ESCALONADOR GARANTIDO INSTALADO!"
        echo " Digite: reboot"
        echo " Ao ligar a VM de novo, execute: ./executar_tudo_verificado.sh"
        echo "============================================================="
        ;;

    esperando_garantido)
        echo "Verificando se o escalonador ativo e o GARANTIDO..."
        verificar_escalonador_ativo garantido
        if [ $? -ne 0 ]; then
            echo "ERRO CRITICO: O escalonador ativo no boot nao e o GARANTIDO!"
            echo "Por favor, garanta que reiniciou a VM apos a instalacao."
            exit 1
        fi
        
        rodar_bateria_testes garantido
        
        echo "finalizado" > "$STATE_FILE"
        echo "============================================================="
        echo " BATERIA COMPLETA DE TESTES CONCLUIDA COM SUCESSO!"
        echo " Todos os resultados (4 colunas) consolidados em: $RESULTS_FILE"
        echo "============================================================="
        rm -f "$STATE_FILE"
        ;;
        
    *)
        echo "Estado desconhecido: $ESTADO. Limpando..."
        rm -f "$STATE_FILE"
        ;;
esac
