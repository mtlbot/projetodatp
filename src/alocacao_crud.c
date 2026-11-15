#include <stdio.h>
#include <string.h>
#include "../include/backend.h"

/* Garante '\0' no fim de cada texto lido do disco (protege contra arquivo adulterado). */
static void fechar_textos(AlocacaoTurma *a) {
    a->departamento[TAM_DEPARTAMENTO - 1] = '\0';
    a->disciplina[TAM_DISCIPLINA - 1] = '\0';
    a->turma[TAM_TURMA - 1] = '\0';
    a->professores[TAM_PROFESSORES - 1] = '\0';
    a->dia_semana[TAM_DIA - 1] = '\0';
    a->hora_inicio[TAM_HORA - 1] = '\0';
    a->hora_fim[TAM_HORA - 1] = '\0';
    a->id_sala[TAM_ID_SALA - 1] = '\0';
}

static int indice_alocacao(const AlocacaoTurma *lista, int qtd, int id_alocacao) {
    for (int i = 0; i < qtd; i++) {
        if (lista[i].id_alocacao == id_alocacao) return i;
    }
    return -1;
}

int carregar_alocacoes(const char *caminho_arquivo, AlocacaoTurma *lista, int *qtd_atual, int limite_maximo) {
    *qtd_atual = 0;
    FILE *arquivo = fopen(caminho_arquivo, "rb");
    if (arquivo == NULL) return RES_OK; /* Primeira execucao: ainda nao ha alocacoes */

    while (*qtd_atual < limite_maximo &&
           fread(&lista[*qtd_atual], sizeof(AlocacaoTurma), 1, arquivo) == 1) {
        fechar_textos(&lista[*qtd_atual]);
        (*qtd_atual)++;
    }

    fclose(arquivo);
    return RES_OK;
}

int salvar_alocacoes(const char *caminho_arquivo, const AlocacaoTurma *lista, int total) {
    return gravar_arquivo_seguro(caminho_arquivo, lista, sizeof(AlocacaoTurma), total);
}

int validar_alocacao(AlocacaoTurma *a, const SalaFisica *salas, int qtd_salas) {
    char dia[TAM_DIA], ini[TAM_HORA], fim[TAM_HORA];

    fechar_textos(a);
    aparar_espacos(a->departamento);
    aparar_espacos(a->disciplina);
    aparar_espacos(a->turma);
    aparar_espacos(a->professores);
    aparar_espacos(a->id_sala);

    if (a->departamento[0] == '\0' || a->disciplina[0] == '\0' ||
        a->turma[0] == '\0' || a->professores[0] == '\0') {
        return RES_INVALIDO;
    }

    if (!normalizar_dia(a->dia_semana, dia) ||
        !normalizar_hora(a->hora_inicio, ini) ||
        !normalizar_hora(a->hora_fim, fim)) {
        return RES_INVALIDO;
    }
    if (strcmp(ini, fim) >= 0) return RES_INVALIDO; /* inicio precisa ser antes do fim */

    copiar_texto(a->dia_semana, dia, TAM_DIA);
    copiar_texto(a->hora_inicio, ini, TAM_HORA);
    copiar_texto(a->hora_fim, fim, TAM_HORA);

    int s = indice_sala(salas, qtd_salas, a->id_sala);
    if (s == -1) return RES_NAO_ENCONTRADO;
    if (salas[s].status_manutencao == 1) return RES_SALA_MANUTENCAO;

    /* Usa o nome exatamente como esta cadastrado ("i-06" vira "I-06") */
    copiar_texto(a->id_sala, salas[s].id_sala, TAM_ID_SALA);
    return RES_OK;
}

int verificar_conflito(const AlocacaoTurma *nova, const AlocacaoTurma *lista, int qtd) {
    for (int i = 0; i < qtd; i++) {
        if (lista[i].ativo == 1 &&
            lista[i].id_alocacao != nova->id_alocacao && /* numa edicao, nao compara consigo mesma */
            strcmp(lista[i].dia_semana, nova->dia_semana) == 0 &&
            strcmp(lista[i].id_sala, nova->id_sala) == 0 &&
            strcmp(lista[i].hora_inicio, nova->hora_fim) < 0 &&
            strcmp(lista[i].hora_fim, nova->hora_inicio) > 0) {
            return 1;
        }
    }
    return 0;
}

int proximo_id_alocacao(const AlocacaoTurma *lista, int qtd) {
    int maior = 0;
    for (int i = 0; i < qtd; i++) {
        if (lista[i].id_alocacao > maior) maior = lista[i].id_alocacao;
    }
    return maior + 1;
}

int adicionar_alocacao(AlocacaoTurma nova, AlocacaoTurma *lista, int *qtd, int limite_maximo,
                       const char *caminho_arquivo,
                       const SalaFisica *salas, int qtd_salas, int *id_gerado) {
    if (*qtd >= limite_maximo) return RES_LIMITE;

    int r = validar_alocacao(&nova, salas, qtd_salas);
    if (r != RES_OK) return r;

    nova.id_alocacao = proximo_id_alocacao(lista, *qtd);
    nova.ativo = 1;

    if (verificar_conflito(&nova, lista, *qtd)) return RES_CONFLITO;

    lista[*qtd] = nova;
    (*qtd)++;

    if (salvar_alocacoes(caminho_arquivo, lista, *qtd) != RES_OK) {
        (*qtd)--; /* desfaz na memoria para manter igual ao arquivo */
        return RES_ERRO_ARQUIVO;
    }

    if (id_gerado != NULL) *id_gerado = nova.id_alocacao;
    return RES_OK;
}

int atualizar_sala_alocacao(const char *caminho_arquivo, int id_alocacao, const char *nova_sala,
                            AlocacaoTurma *lista, int qtd_total,
                            const SalaFisica *salas, int qtd_salas) {
    int i = indice_alocacao(lista, qtd_total, id_alocacao);
    if (i == -1) return RES_NAO_ENCONTRADO;
    if (lista[i].ativo != 1) return RES_JA_CANCELADA;

    /* Clone para testar antes de alterar o registro verdadeiro */
    AlocacaoTurma temporaria = lista[i];
    copiar_texto(temporaria.id_sala, nova_sala, TAM_ID_SALA);

    int r = validar_alocacao(&temporaria, salas, qtd_salas);
    if (r != RES_OK) return r;
    if (verificar_conflito(&temporaria, lista, qtd_total)) return RES_CONFLITO;

    AlocacaoTurma original = lista[i];
    lista[i] = temporaria;

    if (salvar_alocacoes(caminho_arquivo, lista, qtd_total) != RES_OK) {
        lista[i] = original;
        return RES_ERRO_ARQUIVO;
    }
    return RES_OK;
}

int cancelar_alocacao(const char *caminho_arquivo, int id_alocacao, AlocacaoTurma *lista, int qtd_total) {
    int i = indice_alocacao(lista, qtd_total, id_alocacao);
    if (i == -1) return RES_NAO_ENCONTRADO;
    if (lista[i].ativo == 0) return RES_JA_CANCELADA;

    lista[i].ativo = 0;

    if (salvar_alocacoes(caminho_arquivo, lista, qtd_total) != RES_OK) {
        lista[i].ativo = 1;
        return RES_ERRO_ARQUIVO;
    }
    return RES_OK;
}

int filtrar_alocacoes(const AlocacaoTurma *lista, int qtd, const char *dia_semana, const char *id_sala,
                      int apenas_ativas, AlocacaoTurma *saida, int *qtd_saida, int limite_saida) {
    char dia[TAM_DIA] = "";
    int filtra_dia = (dia_semana != NULL && dia_semana[0] != '\0');
    int filtra_sala = (id_sala != NULL && id_sala[0] != '\0');

    *qtd_saida = 0;
    if (filtra_dia && !normalizar_dia(dia_semana, dia)) return RES_INVALIDO;

    for (int i = 0; i < qtd && *qtd_saida < limite_saida; i++) {
        if (apenas_ativas && lista[i].ativo != 1) continue;
        if (filtra_dia && strcmp(lista[i].dia_semana, dia) != 0) continue;
        if (filtra_sala && strcmp(lista[i].id_sala, id_sala) != 0) continue;
        saida[(*qtd_saida)++] = lista[i];
    }
    return RES_OK;
}
