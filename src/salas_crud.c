#include <stdio.h>
#include <string.h>
#include "../include/backend.h"

int cadastrar_nova_sala(const char *caminho_arquivo, SalaFisica nova_sala) {
    // Prevenção de erro humano: não cadastrar sala sem nome
    if (strlen(nova_sala.id_sala) == 0) return 0;

    FILE *arquivo = fopen(caminho_arquivo, "ab");
    if (arquivo == NULL) return 0; // Prevenção: falha de disco/permissão

    size_t escrito = fwrite(&nova_sala, sizeof(SalaFisica), 1, arquivo);
    fclose(arquivo);

    return (escrito == 1) ? 1 : 0;
}

int listar_todas_salas(const char *caminho_arquivo, SalaFisica *lista, int *qtd, int limite_maximo) {
    FILE *arquivo = fopen(caminho_arquivo, "rb");
    *qtd = 0;
    if (arquivo == NULL) return 0; // Retorna 0 lidos se o arquivo ainda não existir

    while(fread(&lista[*qtd], sizeof(SalaFisica), 1, arquivo) == 1) {
        (*qtd)++;
        // Prevenção de máquina: impede que a RAM estoure se o arquivo for adulterado
        if (*qtd >= limite_maximo) break;
    }

    fclose(arquivo);
    return 1;
}

int buscar_salas_livres(const char *dia_semana, const char *hora_inicio, const char *hora_fim,
                        SalaFisica *salas_disponiveis, int *qtd_livres,
                        AlocacaoTurma *lista, int qtd_total_alocacoes,
                        SalaFisica *todas_salas, int total_salas) {
    *qtd_livres = 0;

    for (int i = 0; i < total_salas; i++) {
        int sala_ocupada = 0;

        // Compara a sala atual do loop com todas as turmas carregadas na memória
        for (int j = 0; j < qtd_total_alocacoes; j++) {
            if (lista[j].ativo == 1 &&
                strcmp(lista[j].id_sala, todas_salas[i].id_sala) == 0 &&
                strcmp(lista[j].dia_semana, dia_semana) == 0) {

                // Checagem de sobreposição de horário (lógica de string HH:MM)
                if (strcmp(lista[j].hora_fim, hora_inicio) > 0 && strcmp(lista[j].hora_inicio, hora_fim) < 0) {
                    sala_ocupada = 1;
                    break; // Já sabemos que choca horário, passa para a próxima sala
                }
            }
        }

        if (!sala_ocupada) {
            salas_disponiveis[*qtd_livres] = todas_salas[i];
            (*qtd_livres)++;
        }
    }
    return 1;
}
