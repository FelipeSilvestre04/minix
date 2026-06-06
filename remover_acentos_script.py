import re

path = '/home/felip/minix/executar_tudo_verificado.sh'

with open(path, 'r', encoding='utf-8') as f:
    content = f.read()

# Replace typical accented characters with ASCII equivalents in echo lines
replacements = {
    'compilação': 'compilacao',
    'ERRO de compilação': 'ERRO de compilacao',
    'Você está': 'Voce esta',
    'Não foi possível': 'Nao foi possivel',
    'Certifique-se': 'Certifique-se',
    'após': 'apos',
    'instalação': 'instalacao',
    'Prosseguindo': 'Prosseguindo',
    'Parâmetros': 'Parametros',
    'Cenário': 'Cenario',
    'concluído': 'concluido',
    'CONCLUÍDA': 'CONCLUIDA',
    'concluída': 'concluida',
    'ERRO CRÍTICO': 'ERRO CRITICO',
    'não é': 'nao eh',
    'é o': 'eh o',
    'espetáculo': 'espetaculo',
    'padrão': 'padrao',
    'PADRÃO': 'PADRAO',
    'Garantido': 'Garantido',
    'garantido': 'garantido',
    'concluídos': 'concluidos',
    'CONCLUÍDOS': 'CONCLUIDOS',
    'CONCLUÍDA': 'CONCLUIDA',
}

# We can also do a general char replacement for the printed strings
# Let's find all echo statements and clean their characters
def clean_echo(match):
    line = match.group(0)
    # replace chars
    line = line.replace('á', 'a').replace('à', 'a').replace('ã', 'a').replace('â', 'a')
    line = line.replace('é', 'e').replace('ê', 'e').replace('í', 'i')
    line = line.replace('ó', 'o').replace('õ', 'o').replace('ô', 'o')
    line = line.replace('ú', 'u').replace('ç', 'c')
    line = line.replace('Á', 'A').replace('À', 'A').replace('Ã', 'A').replace('Â', 'A')
    line = line.replace('É', 'E').replace('Ê', 'E').replace('Í', 'I')
    line = line.replace('Ó', 'O').replace('Õ', 'O').replace('Ô', 'O')
    line = line.replace('Ú', 'U').replace('Ç', 'C')
    return line

# Match lines starting with echo
new_content = re.sub(r'echo\s+\".*?\"', clean_echo, content)
new_content = re.sub(r'echo\s+\'.*?\'', clean_echo, new_content)

with open(path, 'w', encoding='utf-8') as f:
    f.write(new_content)

print("Accents stripped successfully!")
