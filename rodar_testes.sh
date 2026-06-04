#!/bin/sh

# Script para automatizar a execucao dos testes com diferentes quantidades de processos
# e salvar os resultados em arquivos txt separados.

if [ -z "$1" ]; then
    echo "Uso: $0 <nome_do_escalonador> [IO_ops] [CPU_ops]"
    echo "Exemplo: $0 padrao 1000 10000000"
    exit 1
fi

ESCALONADOR=$1
IO_OPS=${2:-1000}
CPU_OPS=${3:-10000000}
DIR_TESTE="/home/felip/minix"

# Caso nao esteja rodando no WSL e sim no Minix VM, ajusta o diretorio
if [ ! -d "$DIR_TESTE" ]; then
    DIR_TESTE=$(pwd)
fi

BIN_TESTE="$DIR_TESTE/teste_processos_duas_metricas"
SRC_TESTE="$DIR_TESTE/teste_processos_duas_metricas.c"

# Garante que o teste esta compilado
if [ ! -f "$BIN_TESTE" ]; then
    echo "Compilando teste_processos_duas_metricas.c..."
    clang -o "$BIN_TESTE" "$SRC_TESTE" 2>/dev/null || \
    gcc -o "$BIN_TESTE" "$SRC_TESTE" 2>/dev/null || \
    cc -o "$BIN_TESTE" "$SRC_TESTE"
    
    if [ $? -ne 0 ]; then
        echo "Erro: Nao foi possivel compilar $SRC_TESTE"
        exit 1
    fi
fi

CENARIOS="10 50 100 200"

echo "============================================================="
echo " Iniciando Bateria de Testes para o Escalonador: $ESCALONADOR"
echo " Parâmetros: IO_ops=$IO_OPS | CPU_ops=$CPU_OPS"
echo "============================================================="

for NPROCS in $CENARIOS; do
    ARQ_SAIDA="$DIR_TESTE/resultado_${ESCALONADOR}_${NPROCS}.txt"
    echo "Executando cenario com $NPROCS processos -> $ARQ_SAIDA"
    
    # Executa o benchmark silenciando as mensagens de erro (stderr das impressoes do filho de IO)
    # e direcionando a saida formatada (stdout) para o arquivo txt.
    "$BIN_TESTE" "$NPROCS" "$IO_OPS" "$CPU_OPS" > "$ARQ_SAIDA" 2>/dev/null
    
    echo "Cenario com $NPROCS concluido com sucesso!"
done

echo "============================================================="
echo " Todos os testes de $ESCALONADOR concluidos!"
echo "============================================================="
