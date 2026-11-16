#include <stdio.h>
#include <string.h>
#include "../include/backend.h"

int carregar_alocacoes(const char *caminho_arquivo, AlocacaoTurma *lista, int *qtd_atual, int limite_maximo) {
    FILE *arquivo = fopen(caminho_arquivo, "rb");
    *qtd_atual = 0;
    if (arquivo == NULL) return 0;

    while(fread(&lista[*qtd_atual], sizeof(AlocacaoTurma), 1, arquivo) == 1) {
        (*qtd_atual)++;
        if (*qtd_atual >= limite_maximo) break;
    }

    fclose(arquivo);
    return 1;
}

int salvar_alocacoes(const char *caminho_arquivo, AlocacaoTurma *lista, int total) {
    FILE *arquivo = fopen(caminho_arquivo, "wb");
    if (arquivo == NULL) return 0;

    size_t escrito = fwrite(lista, sizeof(AlocacaoTurma), total, arquivo);
    fclose(arquivo);

    return (escrito == (size_t)total) ? 1 : 0;
}

int verificar_conflito(AlocacaoTurma nova, AlocacaoTurma *lista, int qtd) {
    for (int i = 0; i < qtd; i++) {
        if (lista[i].ativo == 1 &&
            lista[i].id_alocacao != nova.id_alocacao && // Evita comparar a turma com ela mesma numa edição
            strcmp(lista[i].dia_semana, nova.dia_semana) == 0 &&
            strcmp(lista[i].id_sala, nova.id_sala) == 0) {

            if (strcmp(lista[i].hora_fim, nova.hora_inicio) > 0 && strcmp(lista[i].hora_inicio, nova.hora_fim) < 0) {
                return 1; // Retorna 1 se bater no mesmo dia, sala e horário.
            }
        }
    }
    return 0;
}

int adicionar_alocacao(AlocacaoTurma nova, AlocacaoTurma *lista, int *qtd, int limite_maximo, const char *caminho_arquivo) {
    if (*qtd >= limite_maximo) return 0;

    if (verificar_conflito(nova, lista, *qtd) == 1) {
        return -1; // Conflito identificado. GTK do Matheus deve emitir um popup de erro!
    }

    lista[*qtd] = nova;
    (*qtd)++;

    return salvar_alocacoes(caminho_arquivo, lista, *qtd);
}

int atualizar_sala_alocacao(const char *caminho_arquivo, int id_alocacao, const char *nova_sala, AlocacaoTurma *lista, int qtd_total) {
    int index_encontrado = -1;

    for (int i = 0; i < qtd_total; i++) {
        if (lista[i].id_alocacao == id_alocacao) {
            index_encontrado = i;
            break;
        }
    }

    if (index_encontrado == -1) return 0;

    // Cria um clone na memória para checar o conflito antes de salvar a alteração real
    AlocacaoTurma temporaria = lista[index_encontrado];
    strcpy(temporaria.id_sala, nova_sala);

    if (verificar_conflito(temporaria, lista, qtd_total) == 1) {
        return -1; // Conflito: a nova sala que a secretaria quer já está ocupada.
    }

    strcpy(lista[index_encontrado].id_sala, nova_sala);
    return salvar_alocacoes(caminho_arquivo, lista, qtd_total);
}
