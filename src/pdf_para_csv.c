#include <glib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "pdf_para_csv.h"

// =======================================================
// CONVERSOR PDF -> CSV
// =======================================================
// O texto do PDF é extraído pelo "pdftotext -layout" (Poppler), que mantém as colunas
// alinhadas com espaços. Cada linha com um dia + horário (ex.: "SEG - 07:10 às 08:50")
// vira uma linha do CSV. Linhas de título, rodapé ("Página 1 de 5") e cabeçalho são ignoradas.
//
// Onde fica o pdftotext:
//  - Linux:  pacote poppler-utils (sudo apt install poppler-utils)
//  - MSYS2:  pacman -S mingw-w64-x86_64-poppler  (pdftotext.exe fica em /mingw64/bin;
//            para entregar ao DATP, copie pdftotext.exe e as DLLs para a pasta bin/ do programa)

// Procura o pdftotext ao lado do programa e depois no PATH
static char *achar_pdftotext(void) {
    const char *nomes[] = {"bin/pdftotext.exe", "bin/pdftotext", "pdftotext.exe", NULL};
    for (int i = 0; nomes[i]; i++) {
        if (g_file_test(nomes[i], G_FILE_TEST_IS_EXECUTABLE)) return g_strdup(nomes[i]);
    }
    return g_find_program_in_path("pdftotext");
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

    char *programa = achar_pdftotext();
    if (programa == NULL) {
        snprintf(erro, tam_erro, "O leitor de PDF (pdftotext) não foi encontrado. Ele precisa estar na pasta bin do "
                 "programa. Enquanto isso, converta o PDF para CSV pelo Excel (Dados > Obter Dados > Do PDF) e importe o CSV.");
        return 0;
    }

    char *argv[] = {programa, "-layout", "-enc", "UTF-8", (char *)caminho_pdf, "-", NULL};
    char *texto = NULL, *saida_erro = NULL;
    int status = 0;
    GError *gerro = NULL;
    gboolean ok = g_spawn_sync(NULL, argv, NULL, G_SPAWN_DEFAULT, NULL, NULL, &texto, &saida_erro, &status, &gerro);
    g_free(programa);

    if (!ok || !g_spawn_check_wait_status(status, NULL) || texto == NULL) {
        snprintf(erro, tam_erro, "Não foi possível ler o PDF. Confira se o arquivo abre normalmente e não tem senha.%s%s",
                 gerro ? "\nDetalhe: " : "", gerro ? gerro->message : "");
        if (gerro) g_error_free(gerro);
        g_free(texto);
        g_free(saida_erro);
        return 0;
    }
    g_free(saida_erro);

    if (!g_utf8_validate(texto, -1, NULL)) {
        char *conv = g_convert(texto, -1, "UTF-8", "WINDOWS-1252", NULL, NULL, NULL);
        if (conv) { g_free(texto); texto = conv; }
    }

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
