#include <gtk/gtk.h>
#include <string.h>
#include <stdio.h>
#include "interface.h"

// Função para carregar o ficheiro de estilos CSS
void carregar_css(void) {
    GtkCssProvider *provider = gtk_css_provider_new();
    GdkDisplay *display = gdk_display_get_default();
    GdkScreen *screen = gdk_display_get_default_screen(display);

    gtk_css_provider_load_from_path(provider, "style.css", NULL);
    gtk_style_context_add_provider_for_screen(
        screen,
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    g_object_unref(provider);
}

// Cria um Card individual de Sala para a Grelha
static GtkWidget* criar_card_sala(const char *codigo, const char *status_ou_disciplina, gboolean livre) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkStyleContext *context = gtk_widget_get_style_context(box);
    
    gtk_style_context_add_class(context, "room-card");
    if (livre) {
        gtk_style_context_add_class(context, "available");
    } else {
        gtk_style_context_add_class(context, "occupied");
    }

    GtkWidget *lbl_code = gtk_label_new(codigo);
    GtkStyleContext *ctx_code = gtk_widget_get_style_context(lbl_code);
    gtk_style_context_add_class(ctx_code, "room-code");
    gtk_label_set_xalign(GTK_LABEL(lbl_code), 0.0);

    GtkWidget *lbl_desc = gtk_label_new(status_ou_disciplina);
    GtkStyleContext *ctx_desc = gtk_widget_get_style_context(lbl_desc);
    gtk_style_context_add_class(ctx_desc, "room-desc");
    gtk_label_set_xalign(GTK_LABEL(lbl_desc), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(lbl_desc), TRUE);

    gtk_box_pack_start(GTK_BOX(box), lbl_code, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), lbl_desc, FALSE, FALSE, 0);

    return box;
}

// Constrói a página do "Painel de Salas"
static GtkWidget* criar_pagina_painel_salas(void) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 25);

    // Cabeçalho da Página
    GtkWidget *lbl_title = gtk_label_new("Painel de salas");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_title), "page-title");
    gtk_label_set_xalign(GTK_LABEL(lbl_title), 0.0);

    GtkWidget *lbl_sub = gtk_label_new("Clique numa sala para ver detalhes, reservar ou modificar reservas.");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_sub), "page-subtitle");
    gtk_label_set_xalign(GTK_LABEL(lbl_sub), 0.0);

    gtk_box_pack_start(GTK_BOX(vbox), lbl_title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), lbl_sub, FALSE, FALSE, 0);

    // Barra de Dias (Pills)
    GtkWidget *box_dias = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    const char *dias[] = {"Segunda", "Terça", "Quarta", "Quinta", "Sexta", "Sábado"};
    for (int i = 0; i < 6; i++) {
        GtkWidget *btn = gtk_button_new_with_label(dias[i]);
        gtk_style_context_add_class(gtk_widget_get_style_context(btn), "pill-btn");
        if (i == 0) gtk_style_context_add_class(gtk_widget_get_style_context(btn), "active");
        gtk_box_pack_start(GTK_BOX(box_dias), btn, FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(vbox), box_dias, FALSE, FALSE, 0);

    // Grelha de Cards de Salas (GtkFlowBox)
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *flow = gtk_flow_box_new();
    gtk_widget_set_valign(GTK_WIDGET(flow), GTK_ALIGN_START);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flow), 6);
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow), GTK_SELECTION_NONE);

    // Exemplo de preenchimento de salas
    gtk_flow_box_insert(GTK_FLOW_BOX(flow), criar_card_sala("A-03", "METODOLOGIA DA PESQUISA", FALSE), -1);
    gtk_flow_box_insert(GTK_FLOW_BOX(flow), criar_card_sala("B-01", "QUÍMICA", FALSE), -1);
    gtk_flow_box_insert(GTK_FLOW_BOX(flow), criar_card_sala("B-02", "FUNDAMENTOS DA ONDU.", FALSE), -1);
    gtk_flow_box_insert(GTK_FLOW_BOX(flow), criar_card_sala("C-03", "Disponível", TRUE), -1);
    gtk_flow_box_insert(GTK_FLOW_BOX(flow), criar_card_sala("C-04", "Disponível", TRUE), -1);
    gtk_flow_box_insert(GTK_FLOW_BOX(flow), criar_card_sala("I-11", "ANÁLISE E PROJETO DE SW", FALSE), -1);

    gtk_container_add(GTK_CONTAINER(scroll), flow);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    return vbox;
}

// Construtor Geral da Janela Principal
void criar_interface_principal(GtkApplication *app, InterfaceApp *ui) {
    carregar_css();

    ui->window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(ui->window), "Controle de Salas - POLI-UPE");
    gtk_window_set_default_size(GTK_WINDOW(ui->window), 1200, 720);

    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(main_box), "main-bg");
    gtk_container_add(GTK_CONTAINER(ui->window), main_box);

    // --- BARRA LATERAL (SIDEBAR) ---
    GtkWidget *sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(sidebar, 230, -1);
    gtk_style_context_add_class(gtk_widget_get_style_context(sidebar), "sidebar");

    GtkWidget *lbl_brand = gtk_label_new("Controle\nde Salas");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_brand), "sidebar-title");
    gtk_label_set_xalign(GTK_LABEL(lbl_brand), 0.0);

    GtkWidget *lbl_semestre = gtk_label_new("SEMESTRE 2026.2");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_semestre), "sidebar-subtitle");
    gtk_label_set_xalign(GTK_LABEL(lbl_semestre), 0.0);

    gtk_box_pack_start(GTK_BOX(sidebar), lbl_brand, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(sidebar), lbl_semestre, FALSE, FALSE, 0);

    // Botões de Navegação da Sidebar
    GtkWidget *btn_painel = gtk_button_new_with_label("田  Painel de salas");
    GtkWidget *btn_turmas = gtk_button_new_with_label("📄  Planilha: Turma – Horário");
    GtkWidget *btn_livres = gtk_button_new_with_label("🏛  Planilha: Salas Livres");
    GtkWidget *btn_reservas = gtk_button_new_with_label("📅  Reservas");

    gtk_style_context_add_class(gtk_widget_get_style_context(btn_painel), "nav-btn");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_painel), "active");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_turmas), "nav-btn");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_livres), "nav-btn");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_reservas), "nav-btn");

    gtk_box_pack_start(GTK_BOX(sidebar), btn_painel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(sidebar), btn_turmas, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(sidebar), btn_livres, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(sidebar), btn_reservas, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(main_box), sidebar, FALSE, FALSE, 0);

    // --- ÁREA DE CONTEÚDO PRINCIPAL (GTK STACK) ---
    GtkWidget *stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);

    GtkWidget *pagina_painel = criar_pagina_painel_salas();
    gtk_stack_add_named(GTK_STACK(stack), pagina_painel, "painel");

    gtk_box_pack_start(GTK_BOX(main_box), stack, TRUE, TRUE, 0);

    gtk_widget_show_all(ui->window);
}