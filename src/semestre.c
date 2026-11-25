#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "../include/backend.h"

// =======================================================
// SEMESTRE LETIVO (data/semestre.txt)
// =======================================================
// Sem o arquivo, o semestre sai da data do computador: janeiro a julho = .1, agosto a dezembro = .2.
// Se o DATP escolher um semestre na mao (botao "Semestre"), ele fica gravado no arquivo, numa linha
// de texto como "2027.1", e vale ate alguem voltar para o automatico (o que apaga o arquivo).

void semestre_pela_data(char *saida, int tamanho) {
    time_t agora = time(NULL);
    struct tm *t = localtime(&agora);
    int ano = t->tm_year + 1900;
    int metade = (t->tm_mon < 7) ? 1 : 2; // tm_mon: 0 = janeiro ... 6 = julho
    snprintf(saida, tamanho, "%d.%d", ano, metade);
}

int semestre_valido(const char *texto) {
    // Formato AAAA.S, com S = 1 ou 2 (ex.: 2026.2)
    if (texto == NULL || strlen(texto) != 6) return 0;
    for (int i = 0; i < 4; i++) if (!isdigit((unsigned char)texto[i])) return 0;
    if (texto[4] != '.') return 0;
    if (texto[5] != '1' && texto[5] != '2') return 0;
    int ano = (texto[0] - '0') * 1000 + (texto[1] - '0') * 100 + (texto[2] - '0') * 10 + (texto[3] - '0');
    return ano >= 2000 && ano <= 2099;
}

int semestre_atual(const char *caminho_arquivo, char *saida, int tamanho) {
    FILE *f = fopen(caminho_arquivo, "rb");
    if (f != NULL) {
        char linha[32] = "";
        if (fgets(linha, sizeof(linha), f) != NULL) {
            linha[strcspn(linha, "\r\n ")] = '\0';
            if (semestre_valido(linha)) {
                fclose(f);
                snprintf(saida, tamanho, "%s", linha);
                return 1; // escolhido pelo DATP
            }
        }
        fclose(f); // arquivo vazio ou estragado: ignora e usa a data
    }
    semestre_pela_data(saida, tamanho);
    return 0; // automatico
}

int salvar_semestre(const char *caminho_arquivo, const char *semestre) {
    if (semestre == NULL || semestre[0] == '\0') {
        // Voltar para o automatico: basta tirar o arquivo
        FILE *existe = fopen(caminho_arquivo, "rb");
        if (existe == NULL) return 1;
        fclose(existe);
        return remove(caminho_arquivo) == 0;
    }
    if (!semestre_valido(semestre)) return 0;

    FILE *f = fopen(caminho_arquivo, "wb");
    if (f == NULL) return 0;
    fprintf(f, "%s\n", semestre);
    fclose(f);
    return 1;
}
