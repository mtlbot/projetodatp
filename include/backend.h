#ifndef BACKEND_H
#define BACKEND_H

#include "modelos.h"

// =======================================================
// MÓDULO 1: ALOCAÇÕES E GRADE HORÁRIA (alocacao_crud.c)
// =======================================================

// Lê o .dat com trava de segurança (limite_maximo) para não estourar a memória RAM do PC.
// Retorna 1 (sucesso) ou 0 (erro de leitura/arquivo corrompido).
int carregar_alocacoes(const char *caminho_arquivo, AlocacaoTurma *lista, int *qtd_atual, int limite_maximo);

// Salva o vetor no disco. Retorna 1 (sucesso) ou 0 (erro de permissão/disco cheio).
int salvar_alocacoes(const char *caminho_arquivo, AlocacaoTurma *lista, int total);

// Antes de adicionar, checa se a nova alocação choca com o horário/sala de outra turma.
// Retorna 1 (sucesso), -1 (conflito detectado) ou 0 (limite de memória atingido).
int adicionar_alocacao(AlocacaoTurma nova, AlocacaoTurma *lista, int *qtd, int limite_maximo, const char *caminho_arquivo);

// Altera a sala de uma turma. Valida se o ID existe antes de tentar salvar.
int atualizar_sala_alocacao(const char *caminho_arquivo, int id_alocacao, const char *nova_sala, AlocacaoTurma *lista, int qtd_total);

// Motor interno para varrer o array buscando sobreposição de dia, hora e sala.
int verificar_conflito(AlocacaoTurma nova, AlocacaoTurma *lista, int qtd);


// =======================================================
// MÓDULO 2: INVENTÁRIO DE SALAS (salas_crud.c)
// =======================================================

// Bloqueia o cadastro se o usuário tentar salvar uma sala sem nome ou com caracteres inválidos.
int cadastrar_nova_sala(const char *caminho_arquivo, SalaFisica nova_sala);

// Retorna todas as salas. Possui trava de segurança para o vetor do frontend.
int listar_todas_salas(const char *caminho_arquivo, SalaFisica *lista, int *qtd, int limite_maximo);

// Cruzamento de dados: Pega todas as salas, compara com as alocações do dia/hora, e devolve só as livres.
int buscar_salas_livres(const char *dia_semana, const char *hora_inicio, const char *hora_fim,
                        SalaFisica *salas_disponiveis, int *qtd_livres,
                        AlocacaoTurma *lista, int qtd_total_alocacoes,
                        SalaFisica *todas_salas, int total_salas);

#endif
