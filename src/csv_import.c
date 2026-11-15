#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "../include/backend.h"

#define CAMPOS_CSV   8
#define TAM_LINHA    1024

/* Divide a linha nos ';' (diferente de strtok, preserva campos vazios).
 * Retorna quantos campos encontrou. */
static int dividir_campos(char *linha, char *campos[], int max_campos) {
    int n = 0;
    char *p = linha;

    campos[n++] = p;
    while (*p && n < max_campos) {
        if (*p == ';') {
            *p = '\0';
            campos[n++] = p + 1;
        }
        p++;
    }
    /* Se ainda houver ';' depois do ultimo campo esperado, a linha tem campos demais */
    if (strchr(p, ';') != NULL) return max_campos + 1;
    return n;
}

static int comeca_com_sem_caixa(const char *texto, const char *prefixo) {
    while (*prefixo) {
        if (toupper((unsigned char)*texto) != toupper((unsigned char)*prefixo)) return 0;
        texto++; prefixo++;
    }
    return 1;
}

static void registrar_erro(ResultadoImportacao *r, int linha, int codigo) {
    r->rejeitadas++;
    r->linha_ultimo_erro = linha;
    r->codigo_ultimo_erro = codigo;
}

int importar_grade_csv(const char *arq_csv, const char *arq_alocacoes,
                       AlocacaoTurma *lista, int *qtd, int limite_maximo,
                       const SalaFisica *salas, int qtd_salas,
                       ResultadoImportacao *resultado) {
    ResultadoImportacao local;
    if (resultado == NULL) resultado = &local;
    memset(resultado, 0, sizeof(*resultado));

    FILE *arquivo = fopen(arq_csv, "r");
    if (arquivo == NULL) return RES_ERRO_ARQUIVO;

    int qtd_original = *qtd;
    char linha[TAM_LINHA];
    int num_linha = 0;

    while (fgets(linha, sizeof(linha), arquivo) != NULL) {
        num_linha++;

        /* Linha maior que o buffer: descarta o resto dela e rejeita */
        if (strchr(linha, '\n') == NULL && !feof(arquivo)) {
            int c;
            while ((c = fgetc(arquivo)) != '\n' && c != EOF) {}
            registrar_erro(resultado, num_linha, RES_INVALIDO);
            continue;
        }

        char *texto = linha;
        /* Pula o BOM que o Excel/LibreOffice coloca no inicio de arquivos UTF-8 */
        if (num_linha == 1 && (unsigned char)texto[0] == 0xEF &&
            (unsigned char)texto[1] == 0xBB && (unsigned char)texto[2] == 0xBF) {
            texto += 3;
        }

        aparar_espacos(texto);
        if (texto[0] == '\0') continue;                                   /* linha em branco */
        if (num_linha == 1 && comeca_com_sem_caixa(texto, "DEPARTAMENTO")) continue; /* cabecalho */

        char *campos[CAMPOS_CSV];
        if (dividir_campos(texto, campos, CAMPOS_CSV) != CAMPOS_CSV) {
            registrar_erro(resultado, num_linha, RES_INVALIDO);
            continue;
        }
        for (int i = 0; i < CAMPOS_CSV; i++) aparar_espacos(campos[i]);

        if (*qtd >= limite_maximo) {
            registrar_erro(resultado, num_linha, RES_LIMITE);
            break;
        }

        AlocacaoTurma nova;
        memset(&nova, 0, sizeof(nova));
        copiar_texto(nova.departamento, campos[0], TAM_DEPARTAMENTO);
        copiar_texto(nova.disciplina,   campos[1], TAM_DISCIPLINA);
        copiar_texto(nova.turma,        campos[2], TAM_TURMA);
        copiar_texto(nova.professores,  campos[3], TAM_PROFESSORES);
        copiar_texto(nova.dia_semana,   campos[4], TAM_DIA);
        copiar_texto(nova.hora_inicio,  campos[5], TAM_HORA);
        copiar_texto(nova.hora_fim,     campos[6], TAM_HORA);
        copiar_texto(nova.id_sala,      campos[7], TAM_ID_SALA);

        int r = validar_alocacao(&nova, salas, qtd_salas);
        if (r != RES_OK) { registrar_erro(resultado, num_linha, r); continue; }

        nova.id_alocacao = proximo_id_alocacao(lista, *qtd);
        nova.ativo = 1;

        if (verificar_conflito(&nova, lista, *qtd)) {
            registrar_erro(resultado, num_linha, RES_CONFLITO);
            continue;
        }

        lista[(*qtd)++] = nova;
        resultado->importadas++;
    }

    fclose(arquivo);

    if (resultado->importadas > 0 &&
        salvar_alocacoes(arq_alocacoes, lista, *qtd) != RES_OK) {
        *qtd = qtd_original; /* nada foi salvo: desfaz na memoria tambem */
        resultado->importadas = 0;
        return RES_ERRO_ARQUIVO;
    }

    return RES_OK;
}
