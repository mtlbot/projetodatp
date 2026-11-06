#ifndef MODELOS_H
#define MODELOS_H

#define MAX_STR 100

typedef struct {
    char id[15];            // Ex: "A-03", "LIP-07"
    char tipo[20];          // Ex: "Sala", "Laboratorio"
    int capacidade;
} Sala;

typedef struct {
    int dia_semana;         // 1=SEG, 2=TER, 3=QUA, 4=QUI, 5=SEX, 6=SÁB
    char hora_inicio[20];   // Ex: "07:10 - 08:00" ou "07:10 às 08:50"
} Horario;

typedef struct {
    char departamento[10];  // Ex: "DSC", "DEC", "DEB"
    char disciplina[MAX_STR];
    char turma[10];         // Ex: "2C", "AM"
    char docente[MAX_STR];
} Turma;

typedef struct {
    int id;
    Turma turma;
    Sala sala;
    Horario horario;
    int ativo;              // 1 para ativo, 0 para cancelado
} Alocacao;

#endif