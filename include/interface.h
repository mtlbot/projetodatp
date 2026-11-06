#ifndef INTERFACE_H
#define INTERFACE_H

#include <gtk/gtk.h>
#include "modelos.h"

// Estrutura para agrupar os ponteiros dos widgets da interface
typedef struct {
    GtkWidget *window;
    GtkWidget *notebook_dias;
    GtkWidget *tree_views[6];   // Uma tabela para cada dia (SEG a SÁB)
    GtkListStore *stores[6];    // Modelo de dados de cada tabela
} InterfaceApp;

// Inicializa e constrói a janela principal
void criar_interface_principal(GtkApplication *app, InterfaceApp *ui);

// Atualiza uma tabela de um determinado dia da semana
void atualizar_tabela_dia(InterfaceApp *ui, int dia_semana, Alocacao *lista, int qtd);

// Janela modal para cadastrar nova alocação
void abrir_dialogo_nova_alocacao(GtkWidget *widget, gpointer data);

#endif