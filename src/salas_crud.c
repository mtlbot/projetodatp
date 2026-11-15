#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "../include/backend.h"

/* Comparacao sem diferenciar maiusculas/minusculas ("i-06" == "I-06"). */
static int iguais_sem_caixa(const char *a, const char *b) {
    while (*a && *b) {
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) return 0;
        a++; b++;
    }
    return *a == *b;
}

int listar_todas_salas(const char *caminho_arquivo, SalaFisica *lista, int *qtd, int limite_maximo) {
    *qtd = 0;
    FILE *arquivo = fopen(caminho_arquivo, "rb");
    if (arquivo == NULL) return RES_OK; /* Primeira execucao: ainda nao ha salas */

    /* Testa o limite ANTES de ler, para nunca escrever fora do vetor */
    while (*qtd < limite_maximo && fread(&lista[*qtd], sizeof(SalaFisica), 1, arquivo) == 1) {
        lista[*qtd].id_sala[TAM_ID_SALA - 1] = '\0'; /* protege contra arquivo adulterado */
        (*qtd)++;
    }

    fclose(arquivo);
    return RES_OK;
}

int indice_sala(const SalaFisica *lista, int qtd, const char *id_sala) {
    if (id_sala == NULL) return -1;
    for (int i = 0; i < qtd; i++) {
        if (iguais_sem_caixa(lista[i].id_sala, id_sala)) return i;
    }
    return -1;
}

static int salvar_salas(const char *caminho_arquivo, const SalaFisica *lista, int qtd) {
    return gravar_arquivo_seguro(caminho_arquivo, lista, sizeof(SalaFisica), qtd);
}

int cadastrar_nova_sala(const char *caminho_arquivo, SalaFisica nova_sala) {
    SalaFisica salas[MAX_SALAS];
    int qtd;

    /* Monta uma copia "limpa" (zerada) para nao gravar lixo de memoria no arquivo */
    SalaFisica limpa;
    memset(&limpa, 0, sizeof(limpa));
    copiar_texto(limpa.id_sala, nova_sala.id_sala, TAM_ID_SALA);
    aparar_espacos(limpa.id_sala);
    limpa.capacidade = nova_sala.capacidade;
    limpa.status_manutencao = nova_sala.status_manutencao ? 1 : 0;

    if (!validar_id_sala(limpa.id_sala) || limpa.capacidade <= 0) return RES_INVALIDO;

    if (listar_todas_salas(caminho_arquivo, salas, &qtd, MAX_SALAS) != RES_OK) return RES_ERRO_ARQUIVO;
    if (indice_sala(salas, qtd, limpa.id_sala) != -1) return RES_DUPLICADO;
    if (qtd >= MAX_SALAS) return RES_LIMITE;

    FILE *arquivo = fopen(caminho_arquivo, "ab");
    if (arquivo == NULL) return RES_ERRO_ARQUIVO;

    size_t escrito = fwrite(&limpa, sizeof(SalaFisica), 1, arquivo);
    if (fclose(arquivo) != 0) escrito = 0;

    return (escrito == 1) ? RES_OK : RES_ERRO_ARQUIVO;
}

int editar_sala(const char *caminho_arquivo, const char *id_sala, int nova_capacidade, int novo_status_manutencao) {
    SalaFisica salas[MAX_SALAS];
    int qtd;

    if (nova_capacidade <= 0) return RES_INVALIDO;
    if (listar_todas_salas(caminho_arquivo, salas, &qtd, MAX_SALAS) != RES_OK) return RES_ERRO_ARQUIVO;

    int i = indice_sala(salas, qtd, id_sala);
    if (i == -1) return RES_NAO_ENCONTRADO;

    salas[i].capacidade = nova_capacidade;
    salas[i].status_manutencao = novo_status_manutencao ? 1 : 0;

    return salvar_salas(caminho_arquivo, salas, qtd);
}

int remover_sala(const char *caminho_arquivo, const char *id_sala,
                 const AlocacaoTurma *alocacoes, int qtd_alocacoes) {
    SalaFisica salas[MAX_SALAS];
    int qtd;

    if (listar_todas_salas(caminho_arquivo, salas, &qtd, MAX_SALAS) != RES_OK) return RES_ERRO_ARQUIVO;

    int i = indice_sala(salas, qtd, id_sala);
    if (i == -1) return RES_NAO_ENCONTRADO;

    /* Nao deixa "sumir" uma sala que ainda tem turmas reservadas nela */
    for (int j = 0; j < qtd_alocacoes; j++) {
        if (alocacoes[j].ativo == 1 && iguais_sem_caixa(alocacoes[j].id_sala, salas[i].id_sala)) {
            return RES_SALA_EM_USO;
        }
    }

    /* Desloca os itens seguintes uma posicao para tras */
    for (int k = i; k < qtd - 1; k++) salas[k] = salas[k + 1];
    qtd--;

    return salvar_salas(caminho_arquivo, salas, qtd);
}

int buscar_salas_livres(const char *dia_semana, const char *hora_inicio, const char *hora_fim,
                        int capacidade_minima,
                        SalaFisica *salas_disponiveis, int *qtd_livres,
                        const AlocacaoTurma *lista, int qtd_total_alocacoes,
                        const SalaFisica *todas_salas, int total_salas) {
    char dia[TAM_DIA], ini[TAM_HORA], fim[TAM_HORA];
    *qtd_livres = 0;

    /* Normaliza para que "seg" e "7:10" funcionem na comparacao de strings */
    if (!normalizar_dia(dia_semana, dia) ||
        !normalizar_hora(hora_inicio, ini) ||
        !normalizar_hora(hora_fim, fim) ||
        strcmp(ini, fim) >= 0) {
        return RES_INVALIDO;
    }

    for (int i = 0; i < total_salas; i++) {
        if (todas_salas[i].status_manutencao == 1) continue;
        if (todas_salas[i].capacidade < capacidade_minima) continue;

        int sala_ocupada = 0;
        for (int j = 0; j < qtd_total_alocacoes; j++) {
            if (lista[j].ativo == 1 &&
                iguais_sem_caixa(lista[j].id_sala, todas_salas[i].id_sala) &&
                strcmp(lista[j].dia_semana, dia) == 0 &&
                /* Sobreposicao: comeca antes do outro terminar E termina depois do outro comecar.
                 * Funciona com strcmp porque as horas estao sempre no formato HH:MM. */
                strcmp(lista[j].hora_inicio, fim) < 0 &&
                strcmp(lista[j].hora_fim, ini) > 0) {
                sala_ocupada = 1;
                break;
            }
        }

        if (!sala_ocupada) {
            salas_disponiveis[*qtd_livres] = todas_salas[i];
            (*qtd_livres)++;
        }
    }
    return RES_OK;
}
