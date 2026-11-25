#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "../include/backend.h"

// =======================================================
// IMPORTADOR DA PLANILHA "TURMA - HORÁRIO" (CSV)
// =======================================================
// Formatos aceitos (detectados linha a linha):
//  1) Exportação da planilha do DATP (Google Sheets / Excel, separador "," ou ";"):
//     DEPTO, COMPONENTE, TURMA, DOCENTE, "SEG - 07:10 às 08:50", SALA, [ENTRADA], [SAÍDA]
//  2) Formato simples:
//     DEPTO;COMPONENTE;TURMA;DOCENTE;SEG;07:10;08:50;SALA
// Linhas de cabeçalho, linhas vazias e linhas sem dia/horário válidos são ignoradas.
// Aceita arquivos em UTF-8 (Google Sheets) e em Latin-1/Windows-1252 (Excel antigo):
// o texto é sempre guardado em UTF-8, que é o que o GTK exige.

#define MAX_LINHA 2048
#define MAX_CAMPOS 16

// Verifica se o texto já é UTF-8 válido
static int utf8_valido(const unsigned char *s) {
    while (*s) {
        if (*s < 0x80) { s++; continue; }
        int n = (*s & 0xE0) == 0xC0 ? 1 : (*s & 0xF0) == 0xE0 ? 2 : (*s & 0xF8) == 0xF0 ? 3 : -1;
        if (n < 0) return 0;
        for (int i = 1; i <= n; i++) {
            if ((s[i] & 0xC0) != 0x80) return 0;
        }
        s += n + 1;
    }
    return 1;
}

// Converte uma linha Latin-1 para UTF-8 (cada byte >= 0x80 vira 2 bytes)
static void latin1_para_utf8(const char *entrada, char *saida, size_t tam) {
    size_t j = 0;
    for (const unsigned char *p = (const unsigned char *)entrada; *p && j + 2 < tam; p++) {
        if (*p < 0x80) {
            saida[j++] = (char)*p;
        } else {
            saida[j++] = (char)(0xC0 | (*p >> 6));
            saida[j++] = (char)(0x80 | (*p & 0x3F));
        }
    }
    saida[j] = '\0';
}

// Copia com limite de tamanho sem cortar um caractere acentuado (UTF-8) pela metade
static void copiar_seguro(char *destino, size_t tam, const char *origem) {
    size_t n = strlen(origem);
    if (n >= tam) {
        n = tam - 1;
        while (n > 0 && (((unsigned char)origem[n]) & 0xC0) == 0x80) n--; // recua até o início do caractere
    }
    memcpy(destino, origem, n);
    destino[n] = '\0';
}

// Remove espaços nas pontas e junta espaços repetidos ("ADOLPHO  GUIDO " -> "ADOLPHO GUIDO")
static void limpar_espacos(char *s) {
    char *r = s, *w = s;
    int espaco = 1;
    while (*r) {
        if (isspace((unsigned char)*r)) {
            if (!espaco) *w++ = ' ';
            espaco = 1;
        } else {
            *w++ = *r;
            espaco = 0;
        }
        r++;
    }
    if (w > s && w[-1] == ' ') w--;
    *w = '\0';
}

// Divide a linha em campos, respeitando aspas ("A, B" fica num campo só)
static int dividir_campos(char *linha, char separador, char *campos[], int max) {
    int n = 0;
    char *p = linha;
    while (n < max) {
        char *inicio = p, *w = p;
        int aspas = 0;
        while (*p && (aspas || *p != separador)) {
            if (*p == '"') {
                if (aspas && p[1] == '"') { *w++ = '"'; p += 2; continue; }
                aspas = !aspas;
                p++;
                continue;
            }
            *w++ = *p++;
        }
        int fim = (*p == '\0');
        *w = '\0';
        campos[n++] = inicio;
        if (fim) break;
        p++;
    }
    for (int i = 0; i < n; i++) limpar_espacos(campos[i]);
    return n;
}

// Reconhece o dia da semana no início do texto e grava a sigla (SEG, TER, QUA, QUI, SEX, SAB)
static int ler_dia(const char *texto, char *dia) {
    char letras[4] = {0};
    int k = 0;
    for (const unsigned char *p = (const unsigned char *)texto; *p && k < 3; p++) {
        if (isalpha(*p)) {
            letras[k++] = (char)toupper(*p);
        } else if (*p >= 0x80) {
            // "SÁB" -> qualquer letra acentuada no meio conta como 'A'
            if (k == 1) letras[k++] = 'A';
            while ((p[1] & 0xC0) == 0x80) p++; // pula os bytes de continuação do UTF-8
        } else if (k > 0) {
            break;
        }
    }
    const char *validos[] = {"SEG", "TER", "QUA", "QUI", "SEX", "SAB"};
    for (int i = 0; i < 6; i++) {
        if (strcmp(letras, validos[i]) == 0) {
            strcpy(dia, validos[i]);
            return 1;
        }
    }
    return 0;
}

// Procura até 2 horários HH:MM no texto e grava normalizados ("7:10" -> "07:10")
static int ler_horarios(const char *texto, char horas[][6], int max) {
    int achados = 0;
    const char *p = texto;
    while (*p && achados < max) {
        int h, m, lidos = 0;
        if (isdigit((unsigned char)*p) && sscanf(p, "%d:%d%n", &h, &m, &lidos) == 2 && lidos >= 4) {
            if (h >= 0 && h <= 23 && m >= 0 && m <= 59) {
                snprintf(horas[achados], 6, "%02d:%02d", h, m);
                achados++;
            }
            p += lidos;
        } else {
            p++;
        }
    }
    return achados;
}

// Textos que a planilha usa quando a sala ainda nao foi escolhida
static int sala_nao_definida(const char *sala) {
    const char *vazios[] = {"", "-", "--", "?", "A DEFINIR", "A Definir", "a definir", "SEM SALA", "Sem sala", "sem sala", NULL};
    for (int i = 0; vazios[i]; i++) if (strcmp(sala, vazios[i]) == 0) return 1;
    return 0;
}

static void preencher_texto(AlocacaoTurma *a, char *campos[]) {
    copiar_seguro(a->departamento, sizeof(a->departamento), campos[0]);
    copiar_seguro(a->disciplina, sizeof(a->disciplina), campos[1]);
    copiar_seguro(a->turma, sizeof(a->turma), campos[2]);
    copiar_seguro(a->professores, sizeof(a->professores), campos[3]);
}

int importar_grade_csv_detalhado(const char *caminho_arquivo, AlocacaoTurma *lista, int *qtd, int limite_maximo, int *linhas_ignoradas) {
    FILE *arquivo = fopen(caminho_arquivo, "rb");
    *qtd = 0;
    if (linhas_ignoradas) *linhas_ignoradas = 0;
    if (arquivo == NULL) return 0;

    char bruta[MAX_LINHA], linha[MAX_LINHA * 2];
    int primeira = 1;

    while (fgets(bruta, sizeof(bruta), arquivo) != NULL && *qtd < limite_maximo) {
        // Remove a marca BOM do UTF-8 que o Excel coloca no início do arquivo
        char *texto = bruta;
        if (primeira && (unsigned char)texto[0] == 0xEF && (unsigned char)texto[1] == 0xBB && (unsigned char)texto[2] == 0xBF) {
            texto += 3;
        }
        primeira = 0;
        texto[strcspn(texto, "\r\n")] = '\0';

        if (utf8_valido((unsigned char *)texto)) {
            copiar_seguro(linha, sizeof(linha), texto);
        } else {
            latin1_para_utf8(texto, linha, sizeof(linha));
        }

        // Separador: ';' (Excel em português) ou ',' (Google Sheets)
        char separador = strchr(linha, ';') ? ';' : ',';
        int cabecalho = strstr(linha, "COMPONENTE") != NULL;   // linha de títulos das colunas
        char *campos[MAX_CAMPOS];
        int n = dividir_campos(linha, separador, campos, MAX_CAMPOS);

        AlocacaoTurma nova;
        memset(&nova, 0, sizeof(nova));
        char horas[2][6];
        int ok = 0;

        if (n >= 6 && ler_dia(campos[4], nova.dia_semana) && ler_horarios(campos[4], horas, 2) == 2) {
            // Formato 1: dia e horário juntos na 5ª coluna
            preencher_texto(&nova, campos);
            copiar_seguro(nova.id_sala, sizeof(nova.id_sala), campos[5]);
            ok = 1;
        } else if (n >= 8 && ler_dia(campos[4], nova.dia_semana) &&
                   ler_horarios(campos[5], horas, 1) == 1 && ler_horarios(campos[6], &horas[1], 1) == 1) {
            // Formato 2: dia, início e fim em colunas separadas
            preencher_texto(&nova, campos);
            copiar_seguro(nova.id_sala, sizeof(nova.id_sala), campos[7]);
            ok = 1;
        }

        // Sala ainda nao definida na planilha ("-", "A DEFINIR", "SEM SALA"...): a aula entra sem sala
        // e aparece no aviso "Turmas sem sala" para o DATP resolver depois.
        if (ok && sala_nao_definida(nova.id_sala)) nova.id_sala[0] = '\0';

        if (ok && strlen(nova.disciplina) > 0 && strcmp(horas[0], horas[1]) < 0) {
            strcpy(nova.hora_inicio, horas[0]);
            strcpy(nova.hora_fim, horas[1]);
            nova.id_alocacao = *qtd + 1;
            nova.ativo = 1;
            lista[*qtd] = nova;
            (*qtd)++;
        } else if (linhas_ignoradas && linha[0] != '\0' && strspn(linha, ",; ") != strlen(linha) &&
                   !cabecalho) {
            (*linhas_ignoradas)++; // conta só linhas com dados incompletos (o cabeçalho não conta como erro)
        }
    }

    fclose(arquivo);
    return 1;
}

int importar_grade_csv(const char *caminho_arquivo, AlocacaoTurma *lista, int *qtd, int limite_maximo) {
    return importar_grade_csv_detalhado(caminho_arquivo, lista, qtd, limite_maximo, NULL);
}
