#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../include/backend.h"

// =======================================================
// HISTORICO DE MUDANCAS (data/historico.txt)
// =======================================================
// Cada acao do sistema vira uma linha de texto:  24/11/2026 19:52 | Aula cancelada: ...
// Arquivo de texto (e nao binario) de proposito: da para abrir no Bloco de Notas.

int registrar_historico(const char *caminho_arquivo, const char *texto) {
    FILE *f = fopen(caminho_arquivo, "ab"); // "a" = acrescenta no fim, sem apagar o que ja tem
    if (f == NULL) return 0;

    char quando[20];
    time_t agora = time(NULL);
    struct tm *t = localtime(&agora);
    strftime(quando, sizeof(quando), "%d/%m/%Y %H:%M", t);

    fprintf(f, "%s | %s\n", quando, texto);
    fclose(f);
    return 1;
}

int ler_historico(const char *caminho_arquivo, char linhas[][TAM_LINHA_HISTORICO], int max) {
    FILE *f = fopen(caminho_arquivo, "rb");
    if (f == NULL) return 0; // ainda nao tem historico

    // 1a passada: conta quantas linhas o arquivo tem
    char buffer[TAM_LINHA_HISTORICO];
    int total = 0;
    while (fgets(buffer, sizeof(buffer), f) != NULL) if (buffer[0] != '\n' && buffer[0] != '\r') total++;

    // 2a passada: pula as antigas e guarda so as ultimas "max"
    rewind(f);
    int pular = total > max ? total - max : 0;
    int n = 0;
    while (fgets(buffer, sizeof(buffer), f) != NULL && n < max) {
        if (buffer[0] == '\n' || buffer[0] == '\r') continue;
        if (pular > 0) { pular--; continue; }
        buffer[strcspn(buffer, "\r\n")] = '\0';
        strcpy(linhas[n], buffer);
        n++;
    }
    fclose(f);

    // Inverte para a mais recente ficar em primeiro
    char temp[TAM_LINHA_HISTORICO];
    for (int i = 0; i < n / 2; i++) {
        strcpy(temp, linhas[i]);
        strcpy(linhas[i], linhas[n - 1 - i]);
        strcpy(linhas[n - 1 - i], temp);
    }
    return n;
}

int contar_historico(const char *caminho_arquivo) {
    FILE *f = fopen(caminho_arquivo, "rb");
    if (f == NULL) return 0;
    char buffer[TAM_LINHA_HISTORICO];
    int total = 0;
    while (fgets(buffer, sizeof(buffer), f) != NULL) if (buffer[0] != '\n' && buffer[0] != '\r') total++;
    fclose(f);
    return total;
}

int limpar_historico(const char *caminho_arquivo) {
    FILE *f = fopen(caminho_arquivo, "wb"); // "w" = abre zerado: tudo o que tinha no arquivo some
    if (f == NULL) return 0;
    fclose(f);
    return 1;
}
