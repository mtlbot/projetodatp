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
// Retorna 1 (sucesso), -1 (conflito), -2 (ID não encontrado) ou 0 (erro ao salvar).
int atualizar_sala_alocacao(const char *caminho_arquivo, int id_alocacao, const char *nova_sala, AlocacaoTurma *lista, int qtd_total);

// Motor interno para varrer o array buscando sobreposição de dia, hora e sala.
int verificar_conflito(AlocacaoTurma nova, AlocacaoTurma *lista, int qtd);


// =======================================================
// MÓDULO 2: INVENTÁRIO DE SALAS (salas_crud.c)
// =======================================================

// Bloqueia o cadastro se o usuário tentar salvar uma sala sem nome ou com caracteres inválidos.
// Retorna 1 (sucesso), 0 (nome vazio/erro de disco) ou -1 (sala já cadastrada).
int cadastrar_nova_sala(const char *caminho_arquivo, SalaFisica nova_sala);

// Retorna todas as salas. Possui trava de segurança para o vetor do frontend.
int listar_todas_salas(const char *caminho_arquivo, SalaFisica *lista, int *qtd, int limite_maximo);

// Cruzamento de dados: Pega todas as salas, compara com as alocações do dia/hora, e devolve só as livres.
int buscar_salas_livres(const char *dia_semana, const char *hora_inicio, const char *hora_fim,
                        SalaFisica *salas_disponiveis, int *qtd_livres,
                        AlocacaoTurma *lista, int qtd_total_alocacoes,
                        SalaFisica *todas_salas, int total_salas);

// =======================================================
// MODULO 3: IMPORTACAO E FUNCOES DE APOIO (importador.c / alocacao_crud.c)
// =======================================================

// Importa a planilha exportada em CSV (Google Sheets/Excel).
// Aceita o formato da planilha "TURMA - HORARIO" (DEPTO,COMPONENTE,TURMA,DOCENTE,"SEG - 07:10 as 08:50",SALA,...)
// e o formato simples DEPTO;COMPONENTE;TURMA;DOCENTE;DIA;INICIO;FIM;SALA. Retorna 1 (sucesso) ou 0 (erro ao abrir).
int importar_grade_csv(const char *caminho_arquivo, AlocacaoTurma *lista, int *qtd, int limite_maximo);

// Igual a importar_grade_csv, mas tambem informa quantas linhas foram ignoradas (cabecalho, linhas vazias ou invalidas).
int importar_grade_csv_detalhado(const char *caminho_arquivo, AlocacaoTurma *lista, int *qtd, int limite_maximo, int *linhas_ignoradas);

// Igual a verificar_conflito, mas devolve a posicao (indice) da turma que ocupa a sala, ou -1 se nao houver conflito.
// Usado pela interface para dizer ao funcionario QUEM esta ocupando a sala.
int buscar_conflito(AlocacaoTurma nova, AlocacaoTurma *lista, int qtd);

// Marca uma alocacao como cancelada (ativo = 0) e salva. Retorna 1 (sucesso), 0 (erro ao salvar) ou -2 (ID nao encontrado).
int cancelar_alocacao(const char *caminho_arquivo, int id_alocacao, AlocacaoTurma *lista, int qtd_total);

// Devolve o proximo ID livre (maior ID existente + 1).
int proximo_id_alocacao(AlocacaoTurma *lista, int qtd);

// Apaga uma sala do arquivo de salas (reescreve o arquivo sem ela).
// Retorna 1 (sucesso), 0 (erro de disco) ou -2 (sala nao encontrada).
int excluir_sala(const char *caminho_arquivo, const char *id_sala);

// Troca o nome (codigo) de uma sala, mantendo capacidade e status.
// Retorna 1 (sucesso), 0 (nome vazio/erro de disco) ou -1 (ja existe sala com o novo nome).
int renomear_sala(const char *caminho_arquivo, const char *id_antigo, const char *id_novo);

// Troca o nome da sala em TODAS as aulas que usam ela e salva o arquivo de alocacoes.
// Retorna quantas aulas mudaram (0 = nenhuma) ou -1 (erro ao salvar).
int renomear_sala_nas_alocacoes(const char *caminho_arquivo, const char *id_antigo, const char *id_novo,
                                AlocacaoTurma *lista, int qtd_total);

// Grava o vetor inteiro de salas no arquivo (substitui o conteudo). Retorna 1 (sucesso) ou 0 (erro de disco).
int gravar_todas_salas(const char *caminho_arquivo, SalaFisica *lista, int qtd);

#endif
