#ifndef MODELOS_H
#define MODELOS_H

/*
 * Estruturas gravadas diretamente nos arquivos .dat (fwrite/fread).
 * ATENCAO: alterar a ordem ou o tamanho dos campos torna os .dat antigos
 * ilegiveis. Se mudar algo aqui, apague os arquivos em data/ e recadastre.
 */

#define TAM_ID_SALA      25
#define TAM_DEPARTAMENTO 10
#define TAM_DISCIPLINA   120
#define TAM_TURMA        15
#define TAM_PROFESSORES  150
#define TAM_DIA          4    /* "SEG" + '\0' */
#define TAM_HORA         6    /* "HH:MM" + '\0' */

/* Sala fisica da POLI-UPE */
typedef struct {
    char id_sala[TAM_ID_SALA];   /* Ex: "A-03", "LipECOMP", "LAB. MECT" */
    int capacidade;              /* Numero de lugares (> 0) */
    int status_manutencao;       /* 0 = Operacional, 1 = Em reforma/inutilizada */
} SalaFisica;

/* Alocacao (reserva) de uma sala para uma turma */
typedef struct {
    int id_alocacao;                       /* Gerado automaticamente (1, 2, 3...) */
    char departamento[TAM_DEPARTAMENTO];   /* Ex: DEC, DEE, DSC */
    char disciplina[TAM_DISCIPLINA];
    char turma[TAM_TURMA];
    char professores[TAM_PROFESSORES];     /* Ex: "STELA / ANTONIO" */
    char dia_semana[TAM_DIA];              /* SEG, TER, QUA, QUI, SEX, SAB, DOM */
    char hora_inicio[TAM_HORA];            /* Sempre "HH:MM" (ex: 07:10) */
    char hora_fim[TAM_HORA];
    char id_sala[TAM_ID_SALA];
    int ativo;                             /* 1 = Ativa, 0 = Cancelada (sala liberada) */
} AlocacaoTurma;

#endif
