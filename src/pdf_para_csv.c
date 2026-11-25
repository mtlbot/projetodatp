#include <glib.h>
#include <poppler.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
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

// =======================================================
// FORMATO DA ESCOLARIDADE: "Relatório Turma/Horário"
// =======================================================
// É o PDF que o DATP recebe da Coordenação de Graduação. Ele não é uma tabela única: cada turma é um bloco
//
//   TURMA   VAGAS   COMPONENTE CURRICULAR
//   CM      50      MATM0001 - ALGEBRA LINEAR
//   PROFESSOR (ES)                          CARGA HORÁRIA MINISTRADA
//   JOSE CLAUDIO MACIEL FREIRE*             60          (pode ter mais de um professor)
//   DIA DA SEMANA   SALA     HORÁRIO
//   QUA             B03      10:30-11:20 11:20-12:10    (um horário de 50 min por aula)
//   QUI             K03      10:30-11:20 11:20-12:10
//
// A sala pode vir vazia (ainda não definida) ou quebrada em duas linhas ("I08 - SALA DE" / "DESENHO").
// Cada linha de dia vira uma linha do CSV: os horários seguidos são juntados (10:30 às 12:10).

#define MAX_HORARIOS_DIA 20

// Uma aula (linha de dia) ainda sendo montada: a sala pode continuar na linha de baixo
typedef struct {
    gboolean ativa;
    char dia[8];
    GString *sala;
    int n;
    char ini[MAX_HORARIOS_DIA][6], fim[MAX_HORARIOS_DIA][6];
} AulaPendente;

// Linhas de cabeçalho/rodapé que se repetem em toda página
static gboolean linha_de_cabecalho(const char *linha) {
    const char *marcas[] = {"FUNDAÇÃO UNIVERSIDADE", "DATA:", "HORA:", "COORD. GERAL", "Relatório Turma",
                            "Professor Responsável", "TOTAL DE TURMAS", NULL};
    for (int i = 0; marcas[i]; i++) if (strstr(linha, marcas[i])) return TRUE;
    return FALSE;
}

// Escreve as linhas do CSV de uma aula: horários seguidos viram um intervalo só;
// se houver um buraco (ex.: 07:10-08:00 e 10:30-11:20) saem duas linhas
static int escrever_aula(GString *csv, const char *depto, const char *disciplina, const char *turma,
                         const char *professores, AulaPendente *aula) {
    int escritas = 0;
    // A sala fica exatamente como veio no PDF (o DATP renomeia no sistema se quiser)
    const char *sala = aula->sala->str;
    for (int i = 0; i < aula->n; ) {
        int j = i;
        while (j + 1 < aula->n && strcmp(aula->fim[j], aula->ini[j + 1]) == 0) j++;
        char dia_hora[40];
        snprintf(dia_hora, sizeof(dia_hora), "%s - %s às %s", aula->dia, aula->ini[i], aula->fim[j]);
        escrever_campo(csv, depto, FALSE);
        escrever_campo(csv, disciplina, FALSE);
        escrever_campo(csv, turma, FALSE);
        escrever_campo(csv, professores, FALSE);
        escrever_campo(csv, dia_hora, FALSE);
        escrever_campo(csv, sala, TRUE);
        escritas++;
        i = j + 1;
    }
    aula->ativa = FALSE;
    return escritas;
}

static gboolean eh_relatorio_turma_horario(const char *texto) {
    return strstr(texto, "DIA DA SEMANA") != NULL && strstr(texto, "PROFESSOR (ES)") != NULL;
}

static int converter_relatorio_turma_horario(char **linhas, GString *csv) {
    static GRegex *re_dia = NULL, *re_hora = NULL;
    if (re_dia == NULL) {
        re_dia = g_regex_new("^\\s*(SEG|TER|QUA|QUI|SEX|S[AÁ]B|DOM)\\b", 0, 0, NULL);
        re_hora = g_regex_new("(\\d{1,2}:\\d{2})\\s*-\\s*(\\d{1,2}:\\d{2})", 0, 0, NULL);
    }
    enum { FORA, TURMA, PROFESSORES, DIAS } parte = FORA;
    char depto[16] = "", disciplina[200] = "", turma[32] = "";
    GString *professores = g_string_new("");
    AulaPendente aula = { .ativa = FALSE, .sala = g_string_new("") };
    int total = 0;

    for (int i = 0; linhas[i]; i++) {
        char *linha = linhas[i];
        if (linha_de_cabecalho(linha)) continue;            // repete em toda página: não muda o bloco atual
        char *limpa = g_strstrip(g_strdup(linha));
        if (limpa[0] == '\0') { g_free(limpa); continue; }

        if (strstr(limpa, "TURMA") && strstr(limpa, "VAGAS")) {          // começo de um bloco novo
            if (aula.ativa) total += escrever_aula(csv, depto, disciplina, turma, professores->str, &aula);
            parte = TURMA;
            depto[0] = disciplina[0] = turma[0] = '\0';
            g_string_truncate(professores, 0);
        } else if (strstr(limpa, "PROFESSOR (ES)")) {
            parte = PROFESSORES;
        } else if (strstr(limpa, "DIA DA SEMANA")) {
            parte = DIAS;
        } else if (parte == TURMA) {
            // "CM   50   MATM0001 - ALGEBRA LINEAR"  (nome longo pode continuar na linha de baixo)
            int n = 0;
            char **p = dividir_colunas(limpa, &n);
            if (turma[0] == '\0' && n >= 2) {
                g_strlcpy(turma, p[0], sizeof(turma));
                const char *comp = p[n - 1];
                const char *traco = strstr(comp, " - ");
                if (traco) {
                    int k = 0;   // departamento = letras do código (MATM0001 -> MATM)
                    while (k < (int)sizeof(depto) - 1 && g_ascii_isalpha(comp[k])) { depto[k] = comp[k]; k++; }
                    depto[k] = '\0';
                    g_strlcpy(disciplina, traco + 3, sizeof(disciplina));
                } else {
                    g_strlcpy(disciplina, comp, sizeof(disciplina));
                }
            } else if (turma[0] != '\0') {
                g_strlcat(disciplina, " ", sizeof(disciplina));
                g_strlcat(disciplina, limpa, sizeof(disciplina));
            }
            g_strfreev(p);
        } else if (parte == PROFESSORES) {
            // "JOSE CLAUDIO MACIEL FREIRE*      60"  -> nome sem o * (o * só marca o responsável)
            int n = 0;
            char **p = dividir_colunas(limpa, &n);
            if (n >= 1) {
                char *nome = g_strstrip(g_strdup(p[0]));
                g_strdelimit(nome, "*", ' ');
                g_strstrip(nome);
                if (nome[0] != '\0' && strstr(professores->str, nome) == NULL) {
                    if (professores->len > 0) g_string_append(professores, " / ");
                    g_string_append(professores, nome);
                }
                g_free(nome);
            }
            g_strfreev(p);
        } else if (parte == DIAS) {
            GMatchInfo *md = NULL;
            if (g_regex_match(re_dia, limpa, 0, &md)) {
                // "QUA   B03   10:30-11:20 11:20-12:10"
                if (aula.ativa) total += escrever_aula(csv, depto, disciplina, turma, professores->str, &aula);
                char *dia = g_match_info_fetch(md, 1);
                int fim_dia = 0;
                g_match_info_fetch_pos(md, 0, NULL, &fim_dia);
                g_strlcpy(aula.dia, g_str_has_prefix(dia, "S") && !g_str_has_prefix(dia, "SE") ? "SAB" : dia, sizeof(aula.dia));
                g_free(dia);
                aula.ativa = TRUE;
                aula.n = 0;
                g_string_truncate(aula.sala, 0);

                GMatchInfo *mh = NULL;
                int inicio_horarios = -1;
                g_regex_match(re_hora, limpa + fim_dia, 0, &mh);
                while (g_match_info_matches(mh) && aula.n < MAX_HORARIOS_DIA) {
                    char *a = g_match_info_fetch(mh, 1), *b = g_match_info_fetch(mh, 2);
                    int h, m;
                    if (sscanf(a, "%d:%d", &h, &m) == 2) snprintf(aula.ini[aula.n], 6, "%02d:%02d", h, m);
                    if (sscanf(b, "%d:%d", &h, &m) == 2) snprintf(aula.fim[aula.n], 6, "%02d:%02d", h, m);
                    if (inicio_horarios < 0) g_match_info_fetch_pos(mh, 0, &inicio_horarios, NULL);
                    aula.n++;
                    g_free(a);
                    g_free(b);
                    g_match_info_next(mh, NULL);
                }
                g_match_info_free(mh);
                // A sala é o que está entre o dia e o primeiro horário (pode ser vazio)
                if (inicio_horarios < 0) inicio_horarios = (int)strlen(limpa + fim_dia);
                char *sala = g_strstrip(g_strndup(limpa + fim_dia, inicio_horarios));
                g_string_append(aula.sala, sala);
                g_free(sala);
                if (aula.n == 0) {
                    // Dia sem horário: vai para o CSV assim mesmo, para aparecer na tela de revisão
                    escrever_campo(csv, depto, FALSE);
                    escrever_campo(csv, disciplina, FALSE);
                    escrever_campo(csv, turma, FALSE);
                    escrever_campo(csv, professores->str, FALSE);
                    escrever_campo(csv, aula.dia, FALSE);
                    escrever_campo(csv, aula.sala->str, TRUE);
                    aula.ativa = FALSE;
                }
            } else if (aula.ativa && !g_regex_match(re_hora, limpa, 0, NULL)) {
                // Continuação do nome da sala (ex.: "I08 - SALA DE" + "DESENHO")
                if (aula.sala->len > 0) g_string_append_c(aula.sala, ' ');
                g_string_append(aula.sala, limpa);
            }
            g_match_info_free(md);
        }
        g_free(limpa);
    }
    if (aula.ativa) total += escrever_aula(csv, depto, disciplina, turma, professores->str, &aula);
    g_string_free(aula.sala, TRUE);
    g_string_free(professores, TRUE);
    return total;
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
    if (eh_relatorio_turma_horario(texto)) {
        // PDF oficial da escolaridade (um bloco por turma)
        *linhas_convertidas = converter_relatorio_turma_horario(linhas, csv);
        g_strfreev(linhas);
        linhas = g_new0(char *, 1);   // nada mais a ler no laço abaixo
    }
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
