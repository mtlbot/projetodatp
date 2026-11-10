#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/backend.h"

int main() {
    printf("--- TESTE DE IMPORTACAO DE CSV ---\n\n");

    AlocacaoTurma lista_teste[100];
    int quantidade = 0;

    if (importar_grade_csv("C:\\ProjetoDATP\\DATP_backend\\data\\inicial.csv", lista_teste, &quantidade, 100)) {
        printf("Importacao concluida! %d registro(s) lido(s).\n\n", quantidade);

        for (int i = 0; i < quantidade; i++) {
            printf("ID Alocacao: %d\n", lista_teste[i].id_alocacao);
            printf("Disciplina: %s\n", lista_teste[i].disciplina);
            printf("Docente(s): %s\n", lista_teste[i].professores);
            printf("Horario: %s (%s as %s)\n", lista_teste[i].dia_semana, lista_teste[i].hora_inicio, lista_teste[i].hora_fim);
            printf("Sala: %s\n", lista_teste[i].id_sala);
            printf("-------------------------\n");
        }
    }

    return 0;
}
