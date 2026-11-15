#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "../include/backend.h"

const char *mensagem_erro(int codigo) {
    switch (codigo) {
        /* Textos em UTF-8 (acentos), como o GTK exige */
        case RES_OK:              return "Operação realizada com sucesso.";
        case RES_ERRO_ARQUIVO:    return "Erro ao ler ou gravar o arquivo de dados.";
        case RES_CONFLITO:        return "Conflito: a sala já está reservada nesse dia e horário.";
        case RES_LIMITE:          return "Limite máximo de registros atingido.";
        case RES_INVALIDO:        return "Dados inválidos: verifique se há campos vazios ou em formato errado "
                                         "(horários no formato HH:MM, início antes do fim, capacidade maior que zero).";
        case RES_DUPLICADO:       return "Já existe uma sala cadastrada com esse nome.";
        case RES_NAO_ENCONTRADO:  return "Registro não encontrado (verifique se a sala está cadastrada).";
        case RES_SALA_MANUTENCAO: return "A sala está em manutenção e não pode ser reservada.";
        case RES_SALA_EM_USO:     return "A sala possui reservas ativas. Cancele-as ou troque de sala antes de remover.";
        case RES_JA_CANCELADA:    return "Essa reserva já estava cancelada.";
        default:                  return "Erro desconhecido.";
    }
}

void copiar_texto(char *destino, const char *origem, int tamanho) {
    if (destino == NULL || tamanho <= 0) return;
    if (origem == NULL) { destino[0] = '\0'; return; }

    int corte = (int)strlen(origem);
    if (corte >= tamanho) {
        corte = tamanho - 1;
        /* Texto do GTK e UTF-8: letras acentuadas ocupam 2+ bytes. Se o corte cair
         * no meio de uma letra (bytes 10xxxxxx), recua para nao gerar texto invalido. */
        while (corte > 0 && ((unsigned char)origem[corte] & 0xC0) == 0x80) corte--;
    }
    memcpy(destino, origem, (size_t)corte);
    destino[corte] = '\0';
}

void aparar_espacos(char *texto) {
    if (texto == NULL) return;

    char *inicio = texto;
    while (*inicio && isspace((unsigned char)*inicio)) inicio++;
    if (inicio != texto) memmove(texto, inicio, strlen(inicio) + 1);

    int fim = (int)strlen(texto) - 1;
    while (fim >= 0 && isspace((unsigned char)texto[fim])) texto[fim--] = '\0';
}

int normalizar_hora(const char *entrada, char *saida) {
    int h, m;
    char resto;

    if (entrada == NULL) return 0;
    /* %c extra detecta lixo depois dos minutos (ex: "07:10x") */
    if (sscanf(entrada, " %d:%d %c", &h, &m, &resto) != 2) return 0;
    if (h < 0 || h > 23 || m < 0 || m > 59) return 0;

    snprintf(saida, TAM_HORA, "%02d:%02d", h, m);
    return 1;
}

int normalizar_dia(const char *entrada, char *saida) {
    static const char *dias[] = { "SEG", "TER", "QUA", "QUI", "SEX", "SAB", "DOM" };
    char tmp[TAM_DIA + 4];

    if (entrada == NULL) return 0;
    copiar_texto(tmp, entrada, sizeof(tmp));
    aparar_espacos(tmp);
    if (strlen(tmp) != 3) return 0;

    for (int i = 0; tmp[i]; i++) tmp[i] = (char)toupper((unsigned char)tmp[i]);

    for (int i = 0; i < 7; i++) {
        if (strcmp(tmp, dias[i]) == 0) {
            copiar_texto(saida, tmp, TAM_DIA);
            return 1;
        }
    }
    return 0;
}

int validar_id_sala(const char *id) {
    if (id == NULL) return 0;
    size_t n = strlen(id);
    if (n == 0 || n >= TAM_ID_SALA) return 0;

    int tem_visivel = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)id[i];
        if (c < 32 || c == 127 || c == ';') return 0;
        if (!isspace(c)) tem_visivel = 1;
    }
    return tem_visivel;
}

int gravar_arquivo_seguro(const char *caminho, const void *dados, int tamanho_item, int quantidade) {
    char temporario[512];
    snprintf(temporario, sizeof(temporario), "%s.tmp", caminho);

    FILE *arquivo = fopen(temporario, "wb");
    if (arquivo == NULL) return RES_ERRO_ARQUIVO;

    size_t escritos = 0;
    if (quantidade > 0) escritos = fwrite(dados, (size_t)tamanho_item, (size_t)quantidade, arquivo);

    int falhou = (escritos != (size_t)quantidade) || (fflush(arquivo) != 0);
    if (fclose(arquivo) != 0) falhou = 1;

    if (falhou) {
        remove(temporario);
        return RES_ERRO_ARQUIVO;
    }

    /* No Windows, rename() falha se o destino ja existe; por isso remove antes. */
    remove(caminho);
    if (rename(temporario, caminho) != 0) return RES_ERRO_ARQUIVO;

    return RES_OK;
}
