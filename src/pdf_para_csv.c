#include <glib.h>
#include <poppler.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "pdf_para_csv.h"

// =======================================================
// CONVERSOR PDF -> CSV
// =======================================================
// O texto do PDF é lido pela biblioteca Poppler-GLib (API em C, ligada ao programa como o GTK).
// Nosso código pega cada letra com a posição dela na página e remonta as linhas, deixando
// as colunas separadas por vários espaços. Cada linha com um dia + horário
// (ex.: "SEG - 07:10 às 08:50") vira uma linha do CSV. Linhas de título, rodapé
// ("Página 1 de 5") e cabeçalho são ignoradas.
//
// Biblioteca necessária:
//  - Linux:  sudo apt install libpoppler-glib-dev
//  - MSYS2:  pacman -S mingw-w64-x86_64-poppler

// Um caractere do PDF com a posição dele na página
typedef struct {
    gunichar c;
    double x1, x2, y;
} Letra;

static int comparar_letras(const void *a, const void *b) {
    const Letra *la = a, *lb = b;
    if (la->y < lb->y - 2.0) return -1;          // linha de cima primeiro
    if (la->y > lb->y + 2.0) return 1;
    return (la->x1 > lb->x1) - (la->x1 < lb->x1); // mesma linha: da esquerda para a direita
}

// Lê o texto de uma página mantendo as colunas alinhadas com espaços
// (o mesmo resultado do "pdftotext -layout", mas feito aqui no nosso código C)
static void extrair_pagina(PopplerPage *pagina, GString *saida) {
    char *texto = poppler_page_get_text(pagina);
    PopplerRectangle *ret = NULL;
    guint n_ret = 0;
    if (texto == NULL || !poppler_page_get_text_layout(pagina, &ret, &n_ret)) {
        g_free(texto);
        return;
    }

    // Junta cada letra (sem os espaços) com o retângulo dela
    Letra *letras = g_new(Letra, n_ret);
    int n = 0;
    double soma_larguras = 0;
    const char *p = texto;
    for (guint i = 0; i < n_ret && *p; i++, p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        if (g_unichar_isspace(c)) continue;
        letras[n].c = c;
        letras[n].x1 = ret[i].x1;
        letras[n].x2 = ret[i].x2;
        letras[n].y = (ret[i].y1 + ret[i].y2) / 2.0;
        soma_larguras += ret[i].x2 - ret[i].x1;
        n++;
    }
    qsort(letras, n, sizeof(Letra), comparar_letras);
    double largura = n > 0 ? soma_larguras / n : 5.0;   // largura média de uma letra
    if (largura < 1.0) largura = 1.0;

    // Monta as linhas: espaço pequeno = 1 espaço; espaço grande = vários (separa colunas)
    for (int i = 0; i < n; i++) {
        if (i > 0) {
            if (comparar_letras(&letras[i - 1], &letras[i]) != 0 && fabs(letras[i].y - letras[i - 1].y) > 2.0) {
                g_string_append_c(saida, '\n');
            } else {
                double vao = letras[i].x1 - letras[i - 1].x2;
                if (vao > largura * 0.4) {
                    int espacos = (int)(vao / largura + 0.5);
                    if (espacos < 1) espacos = 1;
                    if (vao > largura * 1.6 && espacos < 2) espacos = 2;
                    for (int k = 0; k < espacos; k++) g_string_append_c(saida, ' ');
                }
            }
        }
        g_string_append_unichar(saida, letras[i].c);
    }
    g_string_append_c(saida, '\n');

    g_free(letras);
    g_free(ret);
    g_free(texto);
}

// Abre o PDF com a biblioteca Poppler (API em C) e devolve o texto de todas as páginas
static char *extrair_texto_pdf(const char *caminho_pdf, char *erro, int tam_erro) {
    GError *gerro = NULL;
    char *uri = g_filename_to_uri(caminho_pdf, NULL, &gerro);
    PopplerDocument *doc = uri ? poppler_document_new_from_file(uri, NULL, &gerro) : NULL;
    g_free(uri);
    if (doc == NULL) {
        snprintf(erro, tam_erro, "Não foi possível ler o PDF. Confira se o arquivo abre normalmente e não tem senha.%s%s",
                 gerro ? "\nDetalhe: " : "", gerro ? gerro->message : "");
        if (gerro) g_error_free(gerro);
        return NULL;
    }
    GString *texto = g_string_new("");
    int paginas = poppler_document_get_n_pages(doc);
    for (int i = 0; i < paginas; i++) {
        PopplerPage *pagina = poppler_document_get_page(doc, i);
        if (pagina) {
            extrair_pagina(pagina, texto);
            g_object_unref(pagina);
        }
    }
    g_object_unref(doc);
    return g_string_free(texto, FALSE);
}

// Procura "SEG - 07:10 às 08:50" (ou SEG 07:10 - 08:50, SÁB..., 7:10 a 8:50) e devolve início/fim do trecho
static gboolean achar_dia_horario(const char *linha, int *ini, int *fim) {
    static GRegex *re = NULL;
    if (re == NULL) {
        re = g_regex_new("\\b(SEG|TER|QUA|QUI|SEX|S[AÁ]B)[A-ZÇÁ\\-]*\\s*[-–]?\\s*\\d{1,2}[:h]\\d{2}\\s*(às|as|a|-|–|até)\\s*\\d{1,2}[:h]\\d{2}",
                         G_REGEX_CASELESS | G_REGEX_OPTIMIZE, 0, NULL);
    }
    GMatchInfo *m = NULL;
    gboolean achou = g_regex_match(re, linha, 0, &m);
    if (achou) g_match_info_fetch_pos(m, 0, ini, fim);
    g_match_info_free(m);
    return achou;
}

// Divide um trecho em colunas: duas ou mais espaços seguidos separam colunas
static char **dividir_colunas(const char *trecho, int *n) {
    static GRegex *re = NULL;
    if (re == NULL) re = g_regex_new("\\s{2,}", G_REGEX_OPTIMIZE, 0, NULL);
    char *limpo = g_strstrip(g_strdup(trecho));
    char **partes = g_regex_split(re, limpo, 0);
    g_free(limpo);
    *n = 0;
    while (partes[*n]) (*n)++;
    if (*n == 1 && partes[0][0] == '\0') *n = 0;
    return partes;
}

// Escreve um campo entre aspas, dobrando aspas internas (padrão CSV)
static void escrever_campo(GString *saida, const char *texto, gboolean ultimo) {
    g_string_append_c(saida, '"');
    for (const char *p = texto; *p; p++) {
        if (*p == '"') g_string_append_c(saida, '"');
        g_string_append_c(saida, *p);
    }
    g_string_append_c(saida, '"');
    g_string_append_c(saida, ultimo ? '\n' : ';');
}

int converter_pdf_para_csv(const char *caminho_pdf, const char *caminho_csv,
                           int *linhas_convertidas, char *erro, int tam_erro) {
    *linhas_convertidas = 0;
    erro[0] = '\0';

    char *texto = extrair_texto_pdf(caminho_pdf, erro, tam_erro);
    if (texto == NULL) return 0;

    GString *csv = g_string_new("");
    escrever_campo(csv, "DEPTO", FALSE);
    escrever_campo(csv, "COMPONENTE CURRICULAR", FALSE);
    escrever_campo(csv, "TURMA", FALSE);
    escrever_campo(csv, "DOCENTE", FALSE);
    escrever_campo(csv, "DIA / HORARIO", FALSE);
    escrever_campo(csv, "SALA", TRUE);

    char **linhas = g_strsplit(texto, "\n", -1);
    for (int i = 0; linhas[i]; i++) {
        char *linha = linhas[i];
        int ini, fim;
        if (!achar_dia_horario(linha, &ini, &fim)) {
            // Linha sem horário. Se tiver várias colunas e não for cabeçalho, pode ser uma aula com o
            // horário escrito errado: vai para o CSV para aparecer na tela de revisão.
            int n = 0;
            char **p = dividir_colunas(linha, &n);
            char *maiusc = g_utf8_strup(linha, -1);
            gboolean cabecalho = strstr(maiusc, "COMPONENTE") || strstr(maiusc, "DOCENTE") || strstr(maiusc, "PÁGINA");
            if (n >= 4 && !cabecalho) {
                for (int k = 0; k < 6; k++) escrever_campo(csv, k < n ? p[k] : "", k == 5);
            }
            g_free(maiusc);
            g_strfreev(p);
            continue;
        }

        char *antes = g_strndup(linha, ini);
        char *dia_hora = g_strndup(linha + ini, fim - ini);
        char *sala = g_strstrip(g_strdup(linha + fim));

        int n = 0;
        char **p = dividir_colunas(antes, &n);
        // Esperado: DEPTO, COMPONENTE, TURMA, DOCENTE. Se vier faltando coluna, preenche o que der;
        // a tela de revisão mostra o que ficou vazio.
        const char *c[4] = {"", "", "", ""};
        if (n >= 4) { c[0] = p[0]; c[1] = p[1]; c[2] = p[2]; c[3] = p[n - 1]; }
        else for (int k = 0; k < n; k++) c[k] = p[k];

        escrever_campo(csv, c[0], FALSE);
        escrever_campo(csv, c[1], FALSE);
        escrever_campo(csv, c[2], FALSE);
        escrever_campo(csv, c[3], FALSE);
        escrever_campo(csv, dia_hora, FALSE);
        escrever_campo(csv, sala, TRUE);
        (*linhas_convertidas)++;

        g_strfreev(p);
        g_free(antes);
        g_free(dia_hora);
        g_free(sala);
    }
    g_strfreev(linhas);
    g_free(texto);

    GError *gravar = NULL;
    // BOM no início: assim o Excel abre o CSV com os acentos certos
    char *conteudo = g_strconcat("\xEF\xBB\xBF", csv->str, NULL);
    gboolean gravou = g_file_set_contents(caminho_csv, conteudo, -1, &gravar);
    g_free(conteudo);
    g_string_free(csv, TRUE);
    if (!gravou) {
        snprintf(erro, tam_erro, "Não foi possível criar o arquivo CSV: %s", gravar ? gravar->message : "");
        if (gravar) g_error_free(gravar);
        return 0;
    }
    if (*linhas_convertidas == 0) {
        snprintf(erro, tam_erro, "O PDF foi lido, mas nenhuma aula com dia e horário (ex.: SEG - 07:10 às 08:50) foi "
                 "encontrada. Se o PDF for uma imagem escaneada, ele não tem texto para ser lido.");
        return 0;
    }
    return 1;
}
