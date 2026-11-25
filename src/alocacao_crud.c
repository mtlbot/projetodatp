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

int buscar_conflito(AlocacaoTurma nova, AlocacaoTurma *lista, int qtd) {
    if (nova.id_sala[0] == '\0') return -1; // Aula ainda sem sala: nao ocupa nenhuma sala
    for (int i = 0; i < qtd; i++) {
        if (lista[i].ativo == 1 &&
            lista[i].id_alocacao != nova.id_alocacao && // Evita comparar a turma com ela mesma numa edição
            strcmp(lista[i].dia_semana, nova.dia_semana) == 0 &&
            strcmp(lista[i].id_sala, nova.id_sala) == 0) {

            if (strcmp(lista[i].hora_fim, nova.hora_inicio) > 0 && strcmp(lista[i].hora_inicio, nova.hora_fim) < 0) {
                return i; // Devolve a posição da turma que já ocupa a sala nesse dia/horário.
            }
        }
    }
    return -1;
}

int verificar_conflito(AlocacaoTurma nova, AlocacaoTurma *lista, int qtd) {
    return (buscar_conflito(nova, lista, qtd) >= 0) ? 1 : 0; // 1 se bater no mesmo dia, sala e horário.
}

int proximo_id_alocacao(AlocacaoTurma *lista, int qtd) {
    int maior = 0;
    for (int i = 0; i < qtd; i++) {
        if (lista[i].id_alocacao > maior) maior = lista[i].id_alocacao;
    }
    return maior + 1;
}

int adicionar_alocacao(AlocacaoTurma nova, AlocacaoTurma *lista, int *qtd, int limite_maximo, const char *caminho_arquivo) {
    if (*qtd >= limite_maximo) return 0;

    if (verificar_conflito(nova, lista, *qtd) == 1) {
        return -1; // Conflito identificado. GTK do Matheus deve emitir um popup de erro!
    }

    lista[*qtd] = nova;
    (*qtd)++;

    if (!salvar_alocacoes(caminho_arquivo, lista, *qtd)) {
        (*qtd)--; // Não conseguiu gravar no disco: desfaz na memória para RAM e arquivo continuarem iguais.
        return 0;
    }
    return 1;
}

int atualizar_sala_alocacao(const char *caminho_arquivo, int id_alocacao, const char *nova_sala, AlocacaoTurma *lista, int qtd_total) {
    int index_encontrado = -1;

    for (int i = 0; i < qtd_total; i++) {
        if (lista[i].id_alocacao == id_alocacao) {
            index_encontrado = i;
            break;
        }
    }

    if (index_encontrado == -1) return -2; // ID não existe

    // Cria um clone na memória para checar o conflito antes de salvar a alteração real
    AlocacaoTurma temporaria = lista[index_encontrado];
    strncpy(temporaria.id_sala, nova_sala, sizeof(temporaria.id_sala) - 1);
    temporaria.id_sala[sizeof(temporaria.id_sala) - 1] = '\0';

    if (verificar_conflito(temporaria, lista, qtd_total) == 1) {
        return -1; // Conflito: a nova sala que a secretaria quer já está ocupada.
    }

    AlocacaoTurma original = lista[index_encontrado];
    lista[index_encontrado] = temporaria;
    if (!salvar_alocacoes(caminho_arquivo, lista, qtd_total)) {
        lista[index_encontrado] = original; // Falha ao gravar: volta ao estado anterior.
        return 0;
    }
    return 1;
}

int cancelar_alocacao(const char *caminho_arquivo, int id_alocacao, AlocacaoTurma *lista, int qtd_total) {
    for (int i = 0; i < qtd_total; i++) {
        if (lista[i].id_alocacao == id_alocacao) {
            lista[i].ativo = 0;
            if (!salvar_alocacoes(caminho_arquivo, lista, qtd_total)) {
                lista[i].ativo = 1; // Falha ao gravar: a aula continua ativa.
                return 0;
            }
            return 1;
        }
    }
    return -2; // ID não existe
}

int renomear_sala_nas_alocacoes(const char *caminho_arquivo, const char *id_antigo, const char *id_novo,
                                AlocacaoTurma *lista, int qtd_total) {
    int alteradas = 0;
    for (int i = 0; i < qtd_total; i++) {
        if (strcmp(lista[i].id_sala, id_antigo) == 0) {
            memset(lista[i].id_sala, 0, sizeof(lista[i].id_sala));
            strncpy(lista[i].id_sala, id_novo, sizeof(lista[i].id_sala) - 1);
            alteradas++;
        }
    }
    if (alteradas == 0) return 0; // Nenhuma aula usava essa sala: nada para salvar

    if (!salvar_alocacoes(caminho_arquivo, lista, qtd_total)) return -1;
    return alteradas;
}

int inverter_salas(const char *caminho_arquivo, int id_a, int id_b, AlocacaoTurma *lista, int qtd_total) {
    int ia = -1, ib = -1;
    for (int i = 0; i < qtd_total; i++) {
        if (lista[i].id_alocacao == id_a) ia = i;
        if (lista[i].id_alocacao == id_b) ib = i;
    }
    if (ia == -1 || ib == -1) return -2;

    // Guarda como estava, para voltar se der problema
    AlocacaoTurma original_a = lista[ia], original_b = lista[ib];

    // Troca as salas
    strcpy(lista[ia].id_sala, original_b.id_sala);
    strcpy(lista[ib].id_sala, original_a.id_sala);

    // Se os horarios nao forem iguais, uma delas pode bater com outra aula na sala nova
    if (buscar_conflito(lista[ia], lista, qtd_total) >= 0 || buscar_conflito(lista[ib], lista, qtd_total) >= 0) {
        lista[ia] = original_a;
        lista[ib] = original_b;
        return -1;
    }

    if (!salvar_alocacoes(caminho_arquivo, lista, qtd_total)) {
        lista[ia] = original_a;
        lista[ib] = original_b;
        return 0;
    }
    return 1;
}
