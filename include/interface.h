#ifndef INTERFACE_H
#define INTERFACE_H

#include <gtk/gtk.h>
#include "modelos.h"

#define NUM_DIAS 6
#define NUM_PAGINAS 3

// Estado da aplicação: widgets da janela + dados carregados em RAM pelo backend
typedef struct {
    GtkWidget *window;
    GtkWidget *stack;
    GtkWidget *nav_btns[NUM_PAGINAS];

    // Página "Painel de salas"
    GtkWidget *painel_flow;
    GtkWidget *painel_pills[NUM_DIAS];
    GtkWidget *painel_combo_hora;
    GtkWidget *painel_kpi_livres;
    GtkWidget *painel_kpi_ocupadas;
    GtkWidget *painel_kpi_aulas;
    int painel_dia;

    // Página "Planilha: Turma – Horário"
    GtkWidget *grade_tree;
    GtkListStore *grade_store;
    GtkWidget *grade_pills[NUM_DIAS];
    GtkWidget *grade_busca;
    GtkWidget *grade_contador;
    int grade_dia;

    // Página "Salas livres"
    GtkWidget *livres_combo_dia;
    GtkWidget *livres_combo_ini;
    GtkWidget *livres_combo_fim;
    GtkListStore *livres_store;
    GtkWidget *livres_status;

    // Dados (backend)
    AlocacaoTurma alocacoes[MAX_ALOCACOES];
    int qtd_alocacoes;
    SalaFisica salas[MAX_SALAS];
    int qtd_salas;
} InterfaceApp;

// Lê data/alocacoes.dat e data/salas.dat para a memória (chamado no activate)
void carregar_dados(InterfaceApp *ui);

// Inicializa e constrói a janela principal
void criar_interface_principal(GtkApplication *app, InterfaceApp *ui);

// Redesenha todas as telas a partir dos dados em memória
void atualizar_telas(InterfaceApp *ui);

#endif
