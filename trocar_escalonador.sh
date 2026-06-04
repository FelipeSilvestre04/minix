#!/bin/sh

# Script para automatizar a troca do escalonador ativo no Minix,
# recompilar o servidor sched e regerar a imagem de boot.

if [ -z "$1" ]; then
    echo "Uso: $0 <padrao|fifo|rr|garantido>"
    exit 1
fi

TIPO=$1

# Caminhos padrão do Minix (se rodando dentro da VM)
DIR_SRC="/usr/src"
DIR_SCHED="$DIR_SRC/minix/servers/sched"
DIR_RELEASetools="$DIR_SRC/releasetools"

# Caso esteja rodando fora do local padrão (ex: testando no WSL)
if [ ! -d "$DIR_SCHED" ]; then
    DIR_SCHED="./minix/servers/sched"
    DIR_RELEASetools="./releasetools"
fi

case "$TIPO" in
    padrao)
        SRC_FILE="$DIR_SCHED/schedule_padrao.c"
        ;;
    fifo)
        SRC_FILE="$DIR_SCHED/schedule_fifo.c"
        ;;
    rr)
        SRC_FILE="$DIR_SCHED/schedule_rr.c"
        ;;
    garantido)
        SRC_FILE="$DIR_SCHED/schedule_garantido.c"
        ;;
    *)
        echo "Opção inválida: $TIPO"
        echo "Opções permitidas: padrao, fifo, rr, garantido"
        exit 1
        ;;
esac

if [ ! -f "$SRC_FILE" ]; then
    echo "Erro: Arquivo $SRC_FILE não encontrado!"
    exit 1
fi

echo "============================================================="
echo " Trocando escalonador para: $TIPO"
echo " Copiando: $SRC_FILE -> $DIR_SCHED/schedule.c"
echo "============================================================="

cp "$SRC_FILE" "$DIR_SCHED/schedule.c"
if [ $? -ne 0 ]; then
    echo "Erro ao copiar o arquivo. Você está rodando como root/sudo?"
    exit 1
fi

echo "Cópia concluída com sucesso."
echo ""
echo "Deseja recompilar o 'sched' e atualizar a imagem de boot (make hdboot)? [s/N]"
read RESP

if [ "$RESP" = "s" ] || [ "$RESP" = "S" ] || [ "$RESP" = "y" ] || [ "$RESP" = "Y" ]; then
    echo "1. Compilando e instalando o servidor sched..."
    (cd "$DIR_SCHED" && make && make install)
    if [ $? -ne 0 ]; then
        echo "Erro ao compilar o sched!"
        exit 1
    fi
    
    echo "2. Gerando nova imagem de boot (make hdboot)..."
    (cd "$DIR_RELEASetools" && make hdboot)
    if [ $? -ne 0 ]; then
        echo "Erro ao gerar hdboot!"
        exit 1
    fi
    
    echo "============================================================="
    echo " Sucesso! Reinicie o Minix para aplicar o novo escalonador."
    echo "============================================================="
else
    echo "Operação de compilação abortada pelo usuário."
fi
