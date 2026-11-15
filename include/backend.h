#ifndef BACKEND_H
#define BACKEND_H

#include "modelos.h"

/* =======================================================
 * CONFIGURACAO
 * Caminhos relativos a pasta do projeto (o Code::Blocks executa
 * o programa a partir dela). Nunca use caminho absoluto como C:\...
 * ======================================================= */
#define ARQ_SALAS      "data/salas.dat"
#define ARQ_ALOCACOES  "data/alocacoes.dat"
#define ARQ_CSV_GRADE  "data/inicial.csv"

#define MAX_SALAS      200
#define MAX_ALOCACOES  2000

/* =======================================================
 * CODIGOS DE RETORNO (usados por todas as funcoes abaixo)
 * Use mensagem_erro(codigo) para mostrar o texto no GTK.
 * ======================================================= */
#define RES_OK                   1
#define RES_ERRO_ARQUIVO         0   /* Falha ao ler/gravar arquivo */
#define RES_CONFLITO            -1   /* Sala ja ocupada no dia/horario */
#define RES_LIMITE              -2   /* Vetor cheio (MAX_SALAS / MAX_ALOCACOES) */
#define RES_INVALIDO            -3   /* Campo vazio ou em formato errado */
#define RES_DUPLICADO           -4   /* Sala com esse nome ja existe */
#define RES_NAO_ENCONTRADO      -5   /* Sala ou alocacao nao existe */
#define RES_SALA_MANUTENCAO     -6   /* Sala em reforma nao pode ser reservada */
#define RES_SALA_EM_USO         -7   /* Sala tem reservas ativas, nao pode ser removida */
#define RES_JA_CANCELADA        -8   /* Alocacao ja estava cancelada */

const char *mensagem_erro(int codigo);


/* =======================================================
 * UTILITARIOS (util.c)
 * ======================================================= */

/* Copia com limite de tamanho; sempre termina com '\0'. */
void copiar_texto(char *destino, const char *origem, int tamanho);

/* Remove espacos/quebras de linha do inicio e do fim. */
void aparar_espacos(char *texto);

/* Aceita "7:10" ou "07:10" e grava "07:10" em saida[TAM_HORA]. Retorna 1 se valido. */
int normalizar_hora(const char *entrada, char *saida);

/* Aceita "seg", "Seg", "SEG"... e grava "SEG" em saida[TAM_DIA]. Retorna 1 se valido. */
int normalizar_dia(const char *entrada, char *saida);

/* Nome de sala: nao vazio, cabe no campo, sem ';' e sem caracteres de controle. */
int validar_id_sala(const char *id);

/* Grava o vetor inteiro num arquivo temporario e so depois substitui o original,
 * para nao corromper os dados se o programa fechar no meio da gravacao. */
int gravar_arquivo_seguro(const char *caminho, const void *dados, int tamanho_item, int quantidade);


/* =======================================================
 * MODULO 1: INVENTARIO DE SALAS (salas_crud.c)
 * ======================================================= */

/* Le todas as salas. Arquivo inexistente = lista vazia (RES_OK). */
int listar_todas_salas(const char *caminho_arquivo, SalaFisica *lista, int *qtd, int limite_maximo);

/* Posicao da sala no vetor (comparacao sem diferenciar maiusculas) ou -1. */
int indice_sala(const SalaFisica *lista, int qtd, const char *id_sala);

/* Valida nome/capacidade e impede nomes repetidos. */
int cadastrar_nova_sala(const char *caminho_arquivo, SalaFisica nova_sala);

/* Altera capacidade e status de manutencao de uma sala existente. */
int editar_sala(const char *caminho_arquivo, const char *id_sala, int nova_capacidade, int novo_status_manutencao);

/* Remove a sala do arquivo. Bloqueia (RES_SALA_EM_USO) se houver alocacao ativa nela. */
int remover_sala(const char *caminho_arquivo, const char *id_sala,
                 const AlocacaoTurma *alocacoes, int qtd_alocacoes);

/* Devolve as salas operacionais, com capacidade >= capacidade_minima (0 = qualquer),
 * que nao tem alocacao ativa sobrepondo o dia/horario pedido.
 * salas_disponiveis precisa ter espaco para total_salas itens. */
int buscar_salas_livres(const char *dia_semana, const char *hora_inicio, const char *hora_fim,
                        int capacidade_minima,
                        SalaFisica *salas_disponiveis, int *qtd_livres,
                        const AlocacaoTurma *lista, int qtd_total_alocacoes,
                        const SalaFisica *todas_salas, int total_salas);


/* =======================================================
 * MODULO 2: ALOCACOES / RESERVAS (alocacao_crud.c)
 * ======================================================= */

/* Le todas as alocacoes. Arquivo inexistente = lista vazia (RES_OK). */
int carregar_alocacoes(const char *caminho_arquivo, AlocacaoTurma *lista, int *qtd_atual, int limite_maximo);

/* Regrava o arquivo inteiro com o vetor. */
int salvar_alocacoes(const char *caminho_arquivo, const AlocacaoTurma *lista, int total);

/* Normaliza (dia em maiusculas, hora HH:MM) e valida os campos da alocacao,
 * conferindo se a sala existe e nao esta em manutencao. */
int validar_alocacao(AlocacaoTurma *a, const SalaFisica *salas, int qtd_salas);

/* Retorna 1 se 'nova' choca com alguma alocacao ativa (mesmo dia, sala e horario). */
int verificar_conflito(const AlocacaoTurma *nova, const AlocacaoTurma *lista, int qtd);

/* Maior id existente + 1. */
int proximo_id_alocacao(const AlocacaoTurma *lista, int qtd);

/* RESERVA: valida, checa conflito, gera o id, adiciona e salva.
 * id_gerado pode ser NULL. */
int adicionar_alocacao(AlocacaoTurma nova, AlocacaoTurma *lista, int *qtd, int limite_maximo,
                       const char *caminho_arquivo,
                       const SalaFisica *salas, int qtd_salas, int *id_gerado);

/* Troca a sala de uma alocacao ativa, checando conflito e disponibilidade. */
int atualizar_sala_alocacao(const char *caminho_arquivo, int id_alocacao, const char *nova_sala,
                            AlocacaoTurma *lista, int qtd_total,
                            const SalaFisica *salas, int qtd_salas);

/* LIBERACAO: marca a alocacao como cancelada (ativo = 0) e salva.
 * O registro continua no arquivo como historico. */
int cancelar_alocacao(const char *caminho_arquivo, int id_alocacao, AlocacaoTurma *lista, int qtd_total);

/* Copia para 'saida' as alocacoes que batem com os filtros.
 * dia_semana / id_sala = NULL ou "" para nao filtrar. */
int filtrar_alocacoes(const AlocacaoTurma *lista, int qtd, const char *dia_semana, const char *id_sala,
                      int apenas_ativas, AlocacaoTurma *saida, int *qtd_saida, int limite_saida);


/* =======================================================
 * MODULO 3: IMPORTACAO DA GRADE EM CSV (csv_import.c)
 * Formato (texto puro, UTF-8, separado por ';'), uma alocacao por linha:
 * DEPARTAMENTO;DISCIPLINA;TURMA;PROFESSORES;DIA;INICIO;FIM;SALA
 * Uma linha de cabecalho comecando com "DEPARTAMENTO" e ignorada.
 * ======================================================= */

typedef struct {
    int importadas;
    int rejeitadas;
    int linha_ultimo_erro;     /* numero da linha no arquivo (0 = nenhum erro) */
    int codigo_ultimo_erro;    /* um dos RES_* */
} ResultadoImportacao;

/* Acrescenta ao vetor as linhas validas e sem conflito, depois salva em arq_alocacoes.
 * Linhas com erro sao puladas e contadas em resultado->rejeitadas. */
int importar_grade_csv(const char *arq_csv, const char *arq_alocacoes,
                       AlocacaoTurma *lista, int *qtd, int limite_maximo,
                       const SalaFisica *salas, int qtd_salas,
                       ResultadoImportacao *resultado);

#endif
