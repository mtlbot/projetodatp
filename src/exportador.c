#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/backend.h"

// =======================================================
// EXPORTADOR DA GRADE (CSV) - gera a planilha "TURMA - HORARIO"
// =======================================================
// Gera o mesmo formato que o importador le, entao o arquivo exportado pode ser
// aberto no Excel/Google Planilhas e importado de volta sem perder nada.
// Separador ";" (padrao do Excel em portugues) e BOM no inicio (acentos certos no Excel).

static int ordem_do_dia(const char *sigla) {
    const char *dias[] = {"SEG", "TER", "QUA", "QUI", "SEX", "SAB"};
    for (int i = 0; i < 6; i++) if (strcmp(sigla, dias[i]) == 0) return i;
    return 6;
}

// Ordena por dia da semana, depois horario de inicio, depois sala
static int comparar_aulas(const void *a, const void *b) {
    const AlocacaoTurma *x = *(AlocacaoTurma *const *)a;
    const AlocacaoTurma *y = *(AlocacaoTurma *const *)b;
    int d = ordem_do_dia(x->dia_semana) - ordem_do_dia(y->dia_semana);
    if (d != 0) return d;
    int h = strcmp(x->hora_inicio, y->hora_inicio);
    if (h != 0) return h;
    return strcmp(x->id_sala, y->id_sala);
}

// Escreve um campo; coloca entre aspas se tiver ";" ou aspas dentro
static void escrever_campo(FILE *f, const char *texto, int ultimo) {
    if (strpbrk(texto, ";\"\n") != NULL) {
        fputc('"', f);
        for (const char *p = texto; *p; p++) {
            if (*p == '"') fputc('"', f);
            fputc(*p, f);
        }
        fputc('"', f);
    } else {
        fputs(texto, f);
    }
    fputs(ultimo ? "\r\n" : ";", f);
}

int exportar_grade_csv(const char *caminho_arquivo, AlocacaoTurma *lista, int qtd) {
    // Vetor de ponteiros: ordena sem mexer na ordem original da lista
    AlocacaoTurma **ordem = malloc(sizeof(AlocacaoTurma *) * (qtd > 0 ? qtd : 1));
    if (ordem == NULL) return 0;
    int n = 0;
    for (int i = 0; i < qtd; i++) if (lista[i].ativo == 1) ordem[n++] = &lista[i];
    qsort(ordem, n, sizeof(AlocacaoTurma *), comparar_aulas);

    FILE *f = fopen(caminho_arquivo, "wb");
    if (f == NULL) { free(ordem); return 0; }

    fputs("\xEF\xBB\xBF", f); // BOM do UTF-8
    fputs("DEPTO;COMPONENTE CURRICULAR;TURMA;DOCENTE;DIA / HOR\xC3\x81RIO;SALA\r\n", f);
    for (int i = 0; i < n; i++) {
        AlocacaoTurma *a = ordem[i];
        char dia_hora[40];
        snprintf(dia_hora, sizeof(dia_hora), "%s - %s \xC3\xA0s %s", a->dia_semana, a->hora_inicio, a->hora_fim); // "SEG - 07:10 às 08:50"
        escrever_campo(f, a->departamento, 0);
        escrever_campo(f, a->disciplina, 0);
        escrever_campo(f, a->turma, 0);
        escrever_campo(f, a->professores, 0);
        escrever_campo(f, dia_hora, 0);
        escrever_campo(f, a->id_sala, 1);
    }

    int ok = (ferror(f) == 0);
    if (fclose(f) != 0) ok = 0;
    free(ordem);
    return ok ? 1 : 0;
}
