#ifndef MODELOS_H
#define MODELOS_H

// Limites de memória usados pela interface (vetores carregados em RAM)
#define MAX_ALOCACOES 3000
#define MAX_SALAS 300

// Caminhos dos arquivos de dados (relativos à pasta do programa)
#define ARQ_ALOCACOES "data/alocacoes.dat"
#define ARQ_SALAS     "data/salas.dat"
#define ARQ_HISTORICO "data/historico.txt"
#define ARQ_SEMESTRE  "data/semestre.txt"   // semestre escolhido pelo DATP (sem ele, vale a data)
#define ARQ_ULTIMO_USUARIO "data/ultimo_usuario.txt" // nome do último login (já vem preenchido na próxima vez)

// Login: senha única para entrar no sistema e tamanho máximo do nome
#define SENHA_SISTEMA "datp360"
#define MAX_LETRAS_NOME 40      // letras que cabem no campo "Seu nome"
#define TAM_NOME_USUARIO 81     // bytes: letra com acento ocupa 2 no UTF-8

// Tamanho maximo de uma linha do historico
#define TAM_LINHA_HISTORICO 512

// Definição da Sala Física da POLI-UPE
typedef struct {
    char id_sala[25];        // Ex: "A-03", "LipECOMP", "LAB. MECT"
    int capacidade;
    int status_manutencao;   // 0 = Operacional, 1 = Em reforma/inutilizada
} SalaFisica;

// Definição da Alocação (Baseada no Excel atual)
typedef struct {
    int id_alocacao;
    char departamento[10];   // Ex: DEC, DEE, DSC
    char disciplina[120];    // Acomoda nomes extensos de disciplinas
    char turma[15];
    char professores[150];   // Acomoda nomes com barra, ex: STELA / ANTONIO
    char dia_semana[4];      // Ex: SEG, TER, QUA
    char hora_inicio[6];     // Ex: 07:10
    char hora_fim[6];        // Ex: 08:50
    char id_sala[25];        // A sala escolhida para esta alocação
    int ativo;               // 1 = Ativa, 0 = Cancelada
} AlocacaoTurma;

#endif
