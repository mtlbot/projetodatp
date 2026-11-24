#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "interface.h"
#include "../include/backend.h"

// =======================================================
// TABELAS FIXAS
// =======================================================

static const char *DIAS_SIGLA[NUM_DIAS] = {"SEG", "TER", "QUA", "QUI", "SEX", "SAB"};
static const char *DIAS_NOME[NUM_DIAS]  = {"Segunda", "Terça", "Quarta", "Quinta", "Sexta", "Sábado"};
static const char *DIAS_EXTENSO[NUM_DIAS] = {"segunda-feira", "terça-feira", "quarta-feira",
                                              "quinta-feira", "sexta-feira", "sábado"};

// Horários de aula usados na planilha do DATP (início de cada bloco)
#define NUM_HORARIOS 10
static const char *HORARIOS[NUM_HORARIOS] = {"07:10", "08:50", "10:30", "12:10", "13:50",
                                             "15:30", "17:10", "18:50", "20:30", "22:10"};

static const char *DEPARTAMENTOS[] = {"DEC", "DEB", "DEM", "DSC", "DEE", "FISM", "MECT", NULL};

// Colunas da tabela da planilha
enum { COL_ID, COL_DEPTO, COL_COMPONENTE, COL_TURMA, COL_DOCENTE, COL_HORARIO, COL_SALA, NUM_COLS };

// Buffers grandes ficam fora da pilha
static AlocacaoTurma buffer_importacao[MAX_ALOCACOES];
static AlocacaoTurma buffer_mesclagem[MAX_ALOCACOES];

// =======================================================
// FUNÇÕES DE APOIO
// =======================================================

static int indice_dia(const char *sigla) {
    for (int i = 0; i < NUM_DIAS; i++) {
        if (strcmp(sigla, DIAS_SIGLA[i]) == 0) return i;
    }
    return -1;
}

static const char *dia_extenso(const char *sigla) {
    int i = indice_dia(sigla);
    return i >= 0 ? DIAS_EXTENSO[i] : sigla;
}

// Copia texto UTF-8 respeitando o tamanho do campo, sem cortar um acento pela metade
static void copiar_texto(char *destino, size_t tam, const char *origem) {
    size_t n = strlen(origem);
    if (n >= tam) {
        n = tam - 1;
        while (n > 0 && (((unsigned char)origem[n]) & 0xC0) == 0x80) n--;
    }
    memcpy(destino, origem, n);
    destino[n] = '\0';
}

// Texto digitado: tira espaços das pontas e passa para MAIÚSCULAS (padrão da planilha)
static void copiar_campo_maiusculo(char *destino, size_t tam, const char *origem) {
    char *limpo = g_strstrip(g_strdup(origem ? origem : ""));
    char *maiusculo = g_utf8_strup(limpo, -1);
    copiar_texto(destino, tam, maiusculo);
    g_free(maiusculo);
    g_free(limpo);
}

// Aceita "7:10", "07:10", "7h10" -> grava "07:10". Retorna 0 se o horário for inválido.
static int normalizar_hora(const char *texto, char saida[6]) {
    int h, m;
    if (texto == NULL) return 0;
    if (sscanf(texto, " %d:%d", &h, &m) != 2 && sscanf(texto, " %dh%d", &h, &m) != 2) return 0;
    if (h < 0 || h > 23 || m < 0 || m > 59) return 0;
    snprintf(saida, 6, "%02d:%02d", h, m);
    return 1;
}

static void mostrar_mensagem(InterfaceApp *ui, GtkMessageType tipo, const char *titulo, const char *detalhe) {
    GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(ui->window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                          tipo, GTK_BUTTONS_OK, "%s", titulo);
    if (detalhe) gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d), "%s", detalhe);
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
}

static gboolean confirmar(InterfaceApp *ui, const char *titulo, const char *detalhe, const char *texto_sim) {
    GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(ui->window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                          GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "%s", titulo);
    if (detalhe) gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d), "%s", detalhe);
    gtk_dialog_add_buttons(GTK_DIALOG(d), "Voltar", GTK_RESPONSE_CANCEL, texto_sim, GTK_RESPONSE_YES, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_CANCEL);
    int r = gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
    return r == GTK_RESPONSE_YES;
}

static void erro_ao_salvar(InterfaceApp *ui) {
    mostrar_mensagem(ui, GTK_MESSAGE_ERROR, "Não foi possível salvar a alteração.",
                     "O programa não conseguiu gravar o arquivo " ARQ_ALOCACOES ". "
                     "Verifique se a pasta \"data\" existe ao lado do programa e se ela não está somente leitura. "
                     "Nada foi alterado.");
}

// Mensagem de conflito dizendo QUEM ocupa a sala
static void avisar_conflito(InterfaceApp *ui, AlocacaoTurma *nova, int indice_ocupante) {
    AlocacaoTurma *o = &ui->alocacoes[indice_ocupante];
    char detalhe[700];
    snprintf(detalhe, sizeof(detalhe),
             "A sala %s já está ocupada na %s das %s às %s por:\n\n"
             "%s (turma %s)\nDocente: %s\n\n"
             "Escolha outra sala ou outro horário. Dica: a tela \"Salas livres\" mostra as opções disponíveis.",
             nova->id_sala, dia_extenso(nova->dia_semana), o->hora_inicio, o->hora_fim,
             o->disciplina, o->turma, o->professores);
    mostrar_mensagem(ui, GTK_MESSAGE_WARNING, "Choque de horário: a sala já está ocupada.", detalhe);
}

static int comparar_texto(const void *a, const void *b) {
    return strcmp((const char *)a, (const char *)b);
}

// Junta as salas cadastradas (salas.dat) com as salas que aparecem na grade, sem repetir
static int montar_lista_salas(InterfaceApp *ui, SalaFisica *saida, int max) {
    int n = 0;
    for (int i = 0; i < ui->qtd_salas && n < max; i++) {
        int repetida = 0;
        for (int k = 0; k < n; k++) if (strcmp(saida[k].id_sala, ui->salas[i].id_sala) == 0) { repetida = 1; break; }
        if (!repetida && ui->salas[i].id_sala[0] != '\0') saida[n++] = ui->salas[i];
    }
    for (int i = 0; i < ui->qtd_alocacoes && n < max; i++) {
        if (!ui->alocacoes[i].ativo || ui->alocacoes[i].id_sala[0] == '\0') continue;
        int repetida = 0;
        for (int k = 0; k < n; k++) if (strcmp(saida[k].id_sala, ui->alocacoes[i].id_sala) == 0) { repetida = 1; break; }
        if (!repetida) {
            memset(&saida[n], 0, sizeof(SalaFisica));
            copiar_texto(saida[n].id_sala, sizeof(saida[n].id_sala), ui->alocacoes[i].id_sala);
            n++;
        }
    }
    qsort(saida, n, sizeof(SalaFisica), comparar_texto); // id_sala é o primeiro campo
    return n;
}

static int sala_conhecida(InterfaceApp *ui, const char *id) {
    SalaFisica todas[MAX_SALAS];
    int n = montar_lista_salas(ui, todas, MAX_SALAS);
    for (int i = 0; i < n; i++) if (g_ascii_strcasecmp(todas[i].id_sala, id) == 0) return 1;
    return 0;
}

static void recarregar_salas(InterfaceApp *ui) {
    if (!listar_todas_salas(ARQ_SALAS, ui->salas, &ui->qtd_salas, MAX_SALAS)) ui->qtd_salas = 0;
}

static void cadastrar_sala_se_nova(const char *id) {
    SalaFisica s;
    memset(&s, 0, sizeof(s));
    copiar_texto(s.id_sala, sizeof(s.id_sala), id);
    cadastrar_nova_sala(ARQ_SALAS, s); // -1 = já cadastrada, tudo bem
}

static void preencher_combo(GtkWidget *combo, const char **itens, int qtd) {
    for (int i = 0; (qtd < 0 ? itens[i] != NULL : i < qtd); i++) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), itens[i]);
    }
}

static void preencher_combo_salas(InterfaceApp *ui, GtkWidget *combo) {
    SalaFisica todas[MAX_SALAS];
    int n = montar_lista_salas(ui, todas, MAX_SALAS);
    for (int i = 0; i < n; i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), todas[i].id_sala);
}

static GtkWidget *entrada_do_combo(GtkWidget *combo) {
    return gtk_bin_get_child(GTK_BIN(combo));
}

// =======================================================
// CARREGAMENTO DOS DADOS (chamado no activate)
// =======================================================

void carregar_dados(InterfaceApp *ui) {
    // Na primeira execução o arquivo ainda não existe: começa com a grade vazia.
    if (!carregar_alocacoes(ARQ_ALOCACOES, ui->alocacoes, &ui->qtd_alocacoes, MAX_ALOCACOES)) {
        ui->qtd_alocacoes = 0;
    }
    recarregar_salas(ui);
}

// =======================================================
// ESTILO (CSS)
// =======================================================

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

static void adicionar_classe(GtkWidget *w, const char *classe) {
    gtk_style_context_add_class(gtk_widget_get_style_context(w), classe);
}

static GtkWidget *criar_cabecalho(GtkWidget *vbox, const char *titulo, const char *subtitulo) {
    GtkWidget *lbl_title = gtk_label_new(titulo);
    adicionar_classe(lbl_title, "page-title");
    gtk_label_set_xalign(GTK_LABEL(lbl_title), 0.0);

    GtkWidget *lbl_sub = gtk_label_new(subtitulo);
    adicionar_classe(lbl_sub, "page-subtitle");
    gtk_label_set_xalign(GTK_LABEL(lbl_sub), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(lbl_sub), TRUE);

    gtk_box_pack_start(GTK_BOX(vbox), lbl_title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), lbl_sub, FALSE, FALSE, 0);
    return lbl_sub;
}

// Barra de dias (pílulas). O índice do dia fica guardado em cada botão.
static GtkWidget *criar_barra_dias(GtkWidget *pills[NUM_DIAS], int dia_inicial, GCallback ao_clicar, InterfaceApp *ui) {
    GtkWidget *box_dias = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    for (int i = 0; i < NUM_DIAS; i++) {
        GtkWidget *btn = gtk_button_new_with_label(DIAS_NOME[i]);
        adicionar_classe(btn, "pill-btn");
        if (i == dia_inicial) adicionar_classe(btn, "active");
        g_object_set_data(G_OBJECT(btn), "dia", GINT_TO_POINTER(i));
        g_signal_connect(btn, "clicked", ao_clicar, ui);
        gtk_box_pack_start(GTK_BOX(box_dias), btn, FALSE, FALSE, 0);
        pills[i] = btn;
    }
    return box_dias;
}

static void marcar_pilula(GtkWidget *pills[NUM_DIAS], int ativo) {
    for (int i = 0; i < NUM_DIAS; i++) {
        GtkStyleContext *ctx = gtk_widget_get_style_context(pills[i]);
        if (i == ativo) gtk_style_context_add_class(ctx, "active");
        else gtk_style_context_remove_class(ctx, "active");
    }
}

// =======================================================
// PÁGINA 1: PAINEL DE SALAS
// =======================================================

static int ordem_aulas(const void *a, const void *b) {
    const AlocacaoTurma *x = *(AlocacaoTurma *const *)a, *y = *(AlocacaoTurma *const *)b;
    int c = strcmp(x->hora_inicio, y->hora_inicio);
    return c != 0 ? c : strcmp(x->id_sala, y->id_sala);
}

// Cria um Card individual de Sala para a Grelha
static GtkWidget* criar_card_sala(const char *codigo, const char *status_ou_disciplina, gboolean livre) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    adicionar_classe(box, "room-card");
    adicionar_classe(box, livre ? "available" : "occupied");

    GtkWidget *lbl_code = gtk_label_new(codigo);
    adicionar_classe(lbl_code, "room-code");
    gtk_label_set_xalign(GTK_LABEL(lbl_code), 0.0);

    GtkWidget *lbl_desc = gtk_label_new(status_ou_disciplina);
    adicionar_classe(lbl_desc, "room-desc");
    gtk_label_set_xalign(GTK_LABEL(lbl_desc), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(lbl_desc), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(lbl_desc), 18);

    gtk_box_pack_start(GTK_BOX(box), lbl_code, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), lbl_desc, FALSE, FALSE, 0);

    return box;
}

static void painel_intervalo(InterfaceApp *ui, const char **ini, const char **fim) {
    int i = gtk_combo_box_get_active(GTK_COMBO_BOX(ui->painel_combo_hora));
    if (i < 0) i = 0;
    *ini = HORARIOS[i];
    *fim = HORARIOS[i + 1];
}

// Clique num card: mostra a programação da sala no dia
static gboolean ao_clicar_card(GtkWidget *w, GdkEventButton *ev, gpointer data) {
    (void)ev;
    InterfaceApp *ui = data;
    const char *sala = g_object_get_data(G_OBJECT(w), "sala");
    const char *dia = DIAS_SIGLA[ui->painel_dia];

    AlocacaoTurma *aulas[MAX_ALOCACOES];
    int n = 0;
    for (int i = 0; i < ui->qtd_alocacoes; i++) {
        AlocacaoTurma *a = &ui->alocacoes[i];
        if (a->ativo && strcmp(a->id_sala, sala) == 0 && strcmp(a->dia_semana, dia) == 0) aulas[n++] = a;
    }
    qsort(aulas, n, sizeof(AlocacaoTurma *), ordem_aulas);

    GString *txt = g_string_new("");
    int achou = n > 0;
    for (int i = 0; i < n; i++) {
        g_string_append_printf(txt, "%s – %s   %s (turma %s)\n%s\n\n",
                               aulas[i]->hora_inicio, aulas[i]->hora_fim, aulas[i]->disciplina, aulas[i]->turma, aulas[i]->professores);
    }
    if (!achou) g_string_append(txt, "Nenhuma aula nesta sala neste dia. A sala está livre o dia todo.");

    char titulo[120];
    snprintf(titulo, sizeof(titulo), "Sala %s – %s", sala, dia_extenso(dia));
    mostrar_mensagem(ui, GTK_MESSAGE_INFO, titulo, txt->str);
    g_string_free(txt, TRUE);
    return TRUE;
}

static void atualizar_painel(InterfaceApp *ui) {
    GList *filhos = gtk_container_get_children(GTK_CONTAINER(ui->painel_flow));
    for (GList *l = filhos; l; l = l->next) gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(filhos);

    const char *ini, *fim;
    painel_intervalo(ui, &ini, &fim);
    const char *dia = DIAS_SIGLA[ui->painel_dia];

    SalaFisica todas[MAX_SALAS];
    int n = montar_lista_salas(ui, todas, MAX_SALAS);
    int livres = 0, ocupadas = 0, aulas_dia = 0;

    for (int i = 0; i < ui->qtd_alocacoes; i++) {
        if (ui->alocacoes[i].ativo && strcmp(ui->alocacoes[i].dia_semana, dia) == 0) aulas_dia++;
    }

    for (int s = 0; s < n; s++) {
        AlocacaoTurma *ocupante = NULL;
        for (int i = 0; i < ui->qtd_alocacoes; i++) {
            AlocacaoTurma *a = &ui->alocacoes[i];
            if (a->ativo && strcmp(a->id_sala, todas[s].id_sala) == 0 && strcmp(a->dia_semana, dia) == 0 &&
                strcmp(a->hora_fim, ini) > 0 && strcmp(a->hora_inicio, fim) < 0) {
                ocupante = a;
                break;
            }
        }
        GtkWidget *card;
        if (todas[s].status_manutencao == 1) {
            card = criar_card_sala(todas[s].id_sala, "Em manutenção", FALSE);
            ocupadas++;
        } else if (ocupante) {
            char desc[200];
            snprintf(desc, sizeof(desc), "%s\nTurma %s", ocupante->disciplina, ocupante->turma);
            card = criar_card_sala(todas[s].id_sala, desc, FALSE);
            ocupadas++;
        } else {
            card = criar_card_sala(todas[s].id_sala, "Disponível", TRUE);
            livres++;
        }
        GtkWidget *evento = gtk_event_box_new();
        gtk_event_box_set_visible_window(GTK_EVENT_BOX(evento), FALSE);
        gtk_container_add(GTK_CONTAINER(evento), card);
        g_object_set_data_full(G_OBJECT(evento), "sala", g_strdup(todas[s].id_sala), g_free);
        g_signal_connect(evento, "button-press-event", G_CALLBACK(ao_clicar_card), ui);
        gtk_widget_set_tooltip_text(evento, "Clique para ver todas as aulas desta sala no dia");
        gtk_flow_box_insert(GTK_FLOW_BOX(ui->painel_flow), evento, -1);
    }

    if (n == 0) {
        GtkWidget *vazio = gtk_label_new("Nenhuma sala ainda. Vá em \"Planilha: Turma – Horário\" e clique em "
                                         "\"Importar planilha (CSV)\" para carregar a grade do semestre.");
        gtk_label_set_line_wrap(GTK_LABEL(vazio), TRUE);
        gtk_flow_box_insert(GTK_FLOW_BOX(ui->painel_flow), vazio, -1);
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "%d", livres);
    gtk_label_set_text(GTK_LABEL(ui->painel_kpi_livres), buf);
    snprintf(buf, sizeof(buf), "%d", ocupadas);
    gtk_label_set_text(GTK_LABEL(ui->painel_kpi_ocupadas), buf);
    snprintf(buf, sizeof(buf), "%d", aulas_dia);
    gtk_label_set_text(GTK_LABEL(ui->painel_kpi_aulas), buf);

    gtk_widget_show_all(ui->painel_flow);
}

static void ao_trocar_dia_painel(GtkWidget *btn, gpointer data) {
    InterfaceApp *ui = data;
    ui->painel_dia = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "dia"));
    marcar_pilula(ui->painel_pills, ui->painel_dia);
    atualizar_painel(ui);
}

static void ao_trocar_hora_painel(GtkComboBox *combo, gpointer data) {
    (void)combo;
    atualizar_painel(data);
}

static GtkWidget *criar_kpi(const char *titulo, const char *cor, GtkWidget **valor) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    adicionar_classe(box, "kpi-card");
    adicionar_classe(box, cor);
    GtkWidget *lbl = gtk_label_new(titulo);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    *valor = gtk_label_new("0");
    adicionar_classe(*valor, "kpi-value");
    gtk_label_set_xalign(GTK_LABEL(*valor), 0.0);
    gtk_box_pack_start(GTK_BOX(box), lbl, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), *valor, FALSE, FALSE, 0);
    return box;
}

// Dia e horário atuais, para o painel já abrir "agora"
static void momento_atual(int *dia, int *bloco) {
    GDateTime *agora = g_date_time_new_now_local();
    int semana = g_date_time_get_day_of_week(agora); // 1 = segunda ... 7 = domingo
    char hora[6];
    snprintf(hora, sizeof(hora), "%02d:%02d", g_date_time_get_hour(agora), g_date_time_get_minute(agora));
    g_date_time_unref(agora);

    *dia = (semana >= 1 && semana <= 6) ? semana - 1 : 0;
    *bloco = 0;
    for (int i = 0; i < NUM_HORARIOS - 1; i++) {
        if (strcmp(hora, HORARIOS[i]) >= 0 && strcmp(hora, HORARIOS[i + 1]) < 0) *bloco = i;
    }
}

static GtkWidget* criar_pagina_painel_salas(InterfaceApp *ui) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 25);
    criar_cabecalho(vbox, "Painel de salas", "Escolha o dia e o horário. Clique numa sala para ver todas as aulas dela no dia.");

    int bloco;
    momento_atual(&ui->painel_dia, &bloco);

    GtkWidget *linha = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_pack_start(GTK_BOX(linha), criar_barra_dias(ui->painel_pills, ui->painel_dia, G_CALLBACK(ao_trocar_dia_painel), ui), FALSE, FALSE, 0);

    ui->painel_combo_hora = gtk_combo_box_text_new();
    for (int i = 0; i < NUM_HORARIOS - 1; i++) {
        char rotulo[32];
        snprintf(rotulo, sizeof(rotulo), "%s às %s", HORARIOS[i], HORARIOS[i + 1]);
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui->painel_combo_hora), rotulo);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(ui->painel_combo_hora), bloco);
    g_signal_connect(ui->painel_combo_hora, "changed", G_CALLBACK(ao_trocar_hora_painel), ui);
    gtk_box_pack_start(GTK_BOX(linha), gtk_label_new("Horário:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(linha), ui->painel_combo_hora, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), linha, FALSE, FALSE, 0);

    GtkWidget *kpis = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_pack_start(GTK_BOX(kpis), criar_kpi("Salas livres", "green", &ui->painel_kpi_livres), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(kpis), criar_kpi("Salas ocupadas", "blue", &ui->painel_kpi_ocupadas), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(kpis), criar_kpi("Aulas no dia", "orange", &ui->painel_kpi_aulas), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), kpis, FALSE, FALSE, 0);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    ui->painel_flow = gtk_flow_box_new();
    gtk_widget_set_valign(ui->painel_flow, GTK_ALIGN_START);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(ui->painel_flow), 8);
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(ui->painel_flow), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(scroll), ui->painel_flow);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    return vbox;
}

// =======================================================
// PÁGINA 2: PLANILHA TURMA – HORÁRIO (GtkTreeView)
// =======================================================

static gboolean contem_texto(const char *campo, const char *busca_maiuscula) {
    char *m = g_utf8_strup(campo, -1);
    gboolean r = strstr(m, busca_maiuscula) != NULL;
    g_free(m);
    return r;
}

static void atualizar_grade(InterfaceApp *ui) {
    gtk_list_store_clear(ui->grade_store);
    const char *dia = DIAS_SIGLA[ui->grade_dia];
    char *busca = g_utf8_strup(gtk_entry_get_text(GTK_ENTRY(ui->grade_busca)), -1);
    g_strstrip(busca);

    AlocacaoTurma **visiveis = g_new(AlocacaoTurma *, ui->qtd_alocacoes + 1);
    int n = 0;
    for (int i = 0; i < ui->qtd_alocacoes; i++) {
        AlocacaoTurma *a = &ui->alocacoes[i];
        if (!a->ativo || strcmp(a->dia_semana, dia) != 0) continue;
        if (busca[0] != '\0' && !contem_texto(a->disciplina, busca) && !contem_texto(a->professores, busca) &&
            !contem_texto(a->id_sala, busca) && !contem_texto(a->turma, busca) && !contem_texto(a->departamento, busca)) continue;
        visiveis[n++] = a;
    }
    qsort(visiveis, n, sizeof(AlocacaoTurma *), ordem_aulas);

    for (int i = 0; i < n; i++) {
        AlocacaoTurma *a = visiveis[i];
        char horario[20];
        snprintf(horario, sizeof(horario), "%s – %s", a->hora_inicio, a->hora_fim);
        gtk_list_store_insert_with_values(ui->grade_store, NULL, -1,
            COL_ID, a->id_alocacao, COL_DEPTO, a->departamento, COL_COMPONENTE, a->disciplina,
            COL_TURMA, a->turma, COL_DOCENTE, a->professores, COL_HORARIO, horario, COL_SALA, a->id_sala, -1);
    }

    char contador[120];
    if (busca[0] != '\0') snprintf(contador, sizeof(contador), "%d aula(s) encontrada(s) na %s", n, DIAS_EXTENSO[ui->grade_dia]);
    else snprintf(contador, sizeof(contador), "%d aula(s) na %s", n, DIAS_EXTENSO[ui->grade_dia]);
    gtk_label_set_text(GTK_LABEL(ui->grade_contador), contador);

    g_free(visiveis);
    g_free(busca);
}

static void ao_trocar_dia_grade(GtkWidget *btn, gpointer data) {
    InterfaceApp *ui = data;
    ui->grade_dia = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "dia"));
    marcar_pilula(ui->grade_pills, ui->grade_dia);
    atualizar_grade(ui);
}

static void ao_buscar_grade(GtkSearchEntry *e, gpointer data) {
    (void)e;
    atualizar_grade(data);
}

// Devolve a posição (no vetor) da aula selecionada na tabela, ou -1
static int aula_selecionada(InterfaceApp *ui) {
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(ui->grade_tree));
    GtkTreeModel *model;
    GtkTreeIter iter;
    if (!gtk_tree_selection_get_selected(sel, &model, &iter)) return -1;
    int id;
    gtk_tree_model_get(model, &iter, COL_ID, &id, -1);
    for (int i = 0; i < ui->qtd_alocacoes; i++) if (ui->alocacoes[i].id_alocacao == id) return i;
    return -1;
}

static int exigir_selecao(InterfaceApp *ui) {
    int i = aula_selecionada(ui);
    if (i < 0) {
        mostrar_mensagem(ui, GTK_MESSAGE_INFO, "Nenhuma aula selecionada.",
                         "Clique primeiro numa linha da tabela e depois no botão.");
    }
    return i;
}

static GtkWidget *rotulo_form(const char *texto) {
    GtkWidget *l = gtk_label_new(texto);
    gtk_label_set_xalign(GTK_LABEL(l), 1.0);
    return l;
}

// ---------- Nova aula ----------
static void ao_clicar_nova_aula(GtkWidget *btn, gpointer data) {
    (void)btn;
    InterfaceApp *ui = data;

    GtkWidget *dialog = gtk_dialog_new_with_buttons("Nova aula", GTK_WINDOW(ui->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "Cancelar", GTK_RESPONSE_CANCEL, "Salvar aula", GTK_RESPONSE_ACCEPT, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 16);
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), grid);

    GtkWidget *c_depto = gtk_combo_box_text_new_with_entry();
    preencher_combo(c_depto, DEPARTAMENTOS, -1);
    GtkWidget *e_comp = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(e_comp), "Ex.: CÁLCULO DIF. INT. EM UMA VARIÁVEL");
    gtk_entry_set_width_chars(GTK_ENTRY(e_comp), 40);
    GtkWidget *e_turma = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(e_turma), "Ex.: 2C");
    GtkWidget *e_doc = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(e_doc), "Mais de um docente: separe com /");
    GtkWidget *c_dia = gtk_combo_box_text_new();
    preencher_combo(c_dia, DIAS_NOME, NUM_DIAS);
    gtk_combo_box_set_active(GTK_COMBO_BOX(c_dia), ui->grade_dia);
    GtkWidget *c_ini = gtk_combo_box_text_new_with_entry();
    preencher_combo(c_ini, HORARIOS, NUM_HORARIOS - 1);
    gtk_combo_box_set_active(GTK_COMBO_BOX(c_ini), 0);
    GtkWidget *c_fim = gtk_combo_box_text_new_with_entry();
    preencher_combo(c_fim, HORARIOS + 1, NUM_HORARIOS - 1);
    gtk_combo_box_set_active(GTK_COMBO_BOX(c_fim), 0);
    GtkWidget *c_sala = gtk_combo_box_text_new_with_entry();
    preencher_combo_salas(ui, c_sala);
    gtk_entry_set_placeholder_text(GTK_ENTRY(entrada_do_combo(c_sala)), "Ex.: A-03");

    const char *rotulos[] = {"Departamento", "Componente curricular", "Turma", "Docente(s)", "Dia", "Início", "Fim", "Sala"};
    GtkWidget *campos[] = {c_depto, e_comp, e_turma, e_doc, c_dia, c_ini, c_fim, c_sala};
    for (int i = 0; i < 8; i++) {
        gtk_grid_attach(GTK_GRID(grid), rotulo_form(rotulos[i]), 0, i, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), campos[i], 1, i, 1, 1);
    }
    gtk_widget_show_all(dialog);

    // O formulário só fecha quando salvar ou cancelar: se algo estiver errado, os dados digitados não se perdem.
    while (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        AlocacaoTurma nova;
        memset(&nova, 0, sizeof(nova));
        copiar_campo_maiusculo(nova.departamento, sizeof(nova.departamento), gtk_entry_get_text(GTK_ENTRY(entrada_do_combo(c_depto))));
        copiar_campo_maiusculo(nova.disciplina, sizeof(nova.disciplina), gtk_entry_get_text(GTK_ENTRY(e_comp)));
        copiar_campo_maiusculo(nova.turma, sizeof(nova.turma), gtk_entry_get_text(GTK_ENTRY(e_turma)));
        copiar_campo_maiusculo(nova.professores, sizeof(nova.professores), gtk_entry_get_text(GTK_ENTRY(e_doc)));
        copiar_campo_maiusculo(nova.id_sala, sizeof(nova.id_sala), gtk_entry_get_text(GTK_ENTRY(entrada_do_combo(c_sala))));
        int d = gtk_combo_box_get_active(GTK_COMBO_BOX(c_dia));
        strcpy(nova.dia_semana, DIAS_SIGLA[d < 0 ? 0 : d]);

        if (nova.disciplina[0] == '\0' || nova.turma[0] == '\0' || nova.id_sala[0] == '\0') {
            mostrar_mensagem(ui, GTK_MESSAGE_WARNING, "Faltam informações.",
                             "Preencha pelo menos o componente curricular, a turma e a sala.");
            continue;
        }
        if (!normalizar_hora(gtk_entry_get_text(GTK_ENTRY(entrada_do_combo(c_ini))), nova.hora_inicio) ||
            !normalizar_hora(gtk_entry_get_text(GTK_ENTRY(entrada_do_combo(c_fim))), nova.hora_fim)) {
            mostrar_mensagem(ui, GTK_MESSAGE_WARNING, "Horário inválido.",
                             "Escreva o horário no formato HH:MM, por exemplo 07:10 ou 13:50.");
            continue;
        }
        if (strcmp(nova.hora_inicio, nova.hora_fim) >= 0) {
            mostrar_mensagem(ui, GTK_MESSAGE_WARNING, "Horário inválido.",
                             "O horário de fim precisa ser depois do horário de início.");
            continue;
        }
        if (ui->qtd_alocacoes >= MAX_ALOCACOES) {
            mostrar_mensagem(ui, GTK_MESSAGE_ERROR, "Limite de aulas atingido.",
                             "O sistema já tem o número máximo de aulas cadastradas. Cancele aulas antigas ou fale com a equipe de TI.");
            break;
        }
        if (!sala_conhecida(ui, nova.id_sala)) {
            char det[200];
            snprintf(det, sizeof(det), "A sala \"%s\" ainda não aparece na lista de salas. "
                     "Se não for um erro de digitação, ela será cadastrada como sala nova.", nova.id_sala);
            if (!confirmar(ui, "Sala nova?", det, "Cadastrar sala e salvar")) continue;
            cadastrar_sala_se_nova(nova.id_sala);
            recarregar_salas(ui);
        }

        nova.id_alocacao = proximo_id_alocacao(ui->alocacoes, ui->qtd_alocacoes);
        nova.ativo = 1;

        int ocupante = buscar_conflito(nova, ui->alocacoes, ui->qtd_alocacoes);
        int r = adicionar_alocacao(nova, ui->alocacoes, &ui->qtd_alocacoes, MAX_ALOCACOES, ARQ_ALOCACOES);
        if (r == -1) {
            avisar_conflito(ui, &nova, ocupante);
            continue;
        }
        if (r == 0) {
            erro_ao_salvar(ui);
            continue;
        }

        ui->grade_dia = d < 0 ? 0 : d;
        marcar_pilula(ui->grade_pills, ui->grade_dia);
        atualizar_telas(ui);
        break;
    }
    gtk_widget_destroy(dialog);
}

// ---------- Trocar sala ----------
static void trocar_sala_da_aula(InterfaceApp *ui, int idx) {
    AlocacaoTurma aula = ui->alocacoes[idx];

    GtkWidget *dialog = gtk_dialog_new_with_buttons("Trocar sala", GTK_WINDOW(ui->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "Cancelar", GTK_RESPONSE_CANCEL, "Trocar sala", GTK_RESPONSE_ACCEPT, NULL);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(box), 16);
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), box);

    char info[500];
    snprintf(info, sizeof(info), "%s (turma %s)\n%s, %s às %s\nSala atual: %s",
             aula.disciplina, aula.turma, dia_extenso(aula.dia_semana), aula.hora_inicio, aula.hora_fim, aula.id_sala);
    GtkWidget *l_info = gtk_label_new(info);
    gtk_label_set_xalign(GTK_LABEL(l_info), 0.0);
    gtk_box_pack_start(GTK_BOX(box), l_info, FALSE, FALSE, 0);

    // Lista só as salas livres nesse dia/horário (backend: buscar_salas_livres)
    SalaFisica todas[MAX_SALAS], livres[MAX_SALAS];
    int n_todas = montar_lista_salas(ui, todas, MAX_SALAS), n_livres = 0;
    buscar_salas_livres(aula.dia_semana, aula.hora_inicio, aula.hora_fim, livres, &n_livres,
                        ui->alocacoes, ui->qtd_alocacoes, todas, n_todas);

    char dica[120];
    snprintf(dica, sizeof(dica), "Nova sala (%d sala(s) livre(s) nesse horário):", n_livres);
    GtkWidget *l_dica = gtk_label_new(dica);
    gtk_label_set_xalign(GTK_LABEL(l_dica), 0.0);
    gtk_box_pack_start(GTK_BOX(box), l_dica, FALSE, FALSE, 0);

    GtkWidget *c_sala = gtk_combo_box_text_new_with_entry();
    for (int i = 0; i < n_livres; i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(c_sala), livres[i].id_sala);
    if (n_livres > 0) gtk_combo_box_set_active(GTK_COMBO_BOX(c_sala), 0);
    gtk_box_pack_start(GTK_BOX(box), c_sala, FALSE, FALSE, 0);
    gtk_widget_show_all(dialog);

    while (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        AlocacaoTurma temp = aula;
        copiar_campo_maiusculo(temp.id_sala, sizeof(temp.id_sala), gtk_entry_get_text(GTK_ENTRY(entrada_do_combo(c_sala))));
        if (temp.id_sala[0] == '\0') {
            mostrar_mensagem(ui, GTK_MESSAGE_WARNING, "Escolha a nova sala.", NULL);
            continue;
        }
        if (strcmp(temp.id_sala, aula.id_sala) == 0) break; // nada mudou
        if (!sala_conhecida(ui, temp.id_sala)) {
            char det[200];
            snprintf(det, sizeof(det), "A sala \"%s\" ainda não aparece na lista de salas. "
                     "Se não for um erro de digitação, ela será cadastrada como sala nova.", temp.id_sala);
            if (!confirmar(ui, "Sala nova?", det, "Cadastrar sala e trocar")) continue;
            cadastrar_sala_se_nova(temp.id_sala);
            recarregar_salas(ui);
        }

        int ocupante = buscar_conflito(temp, ui->alocacoes, ui->qtd_alocacoes);
        int r = atualizar_sala_alocacao(ARQ_ALOCACOES, aula.id_alocacao, temp.id_sala, ui->alocacoes, ui->qtd_alocacoes);
        if (r == -1) { avisar_conflito(ui, &temp, ocupante); continue; }
        if (r == -2) { mostrar_mensagem(ui, GTK_MESSAGE_ERROR, "Aula não encontrada.", "Feche e abra o programa novamente."); break; }
        if (r == 0) { erro_ao_salvar(ui); continue; }
        atualizar_telas(ui);
        break;
    }
    gtk_widget_destroy(dialog);
}

static void ao_clicar_trocar_sala(GtkWidget *btn, gpointer data) {
    (void)btn;
    InterfaceApp *ui = data;
    int i = exigir_selecao(ui);
    if (i >= 0) trocar_sala_da_aula(ui, i);
}

static void ao_duplo_clique_linha(GtkTreeView *tv, GtkTreePath *p, GtkTreeViewColumn *c, gpointer data) {
    (void)tv; (void)p; (void)c;
    InterfaceApp *ui = data;
    int i = aula_selecionada(ui);
    if (i >= 0) trocar_sala_da_aula(ui, i);
}

// ---------- Cancelar aula ----------
static void ao_clicar_cancelar_aula(GtkWidget *btn, gpointer data) {
    (void)btn;
    InterfaceApp *ui = data;
    int i = exigir_selecao(ui);
    if (i < 0) return;
    AlocacaoTurma *a = &ui->alocacoes[i];

    char det[500];
    snprintf(det, sizeof(det), "%s (turma %s)\n%s, %s às %s, sala %s\n\nA sala ficará livre nesse horário.",
             a->disciplina, a->turma, dia_extenso(a->dia_semana), a->hora_inicio, a->hora_fim, a->id_sala);
    if (!confirmar(ui, "Cancelar esta aula?", det, "Cancelar aula")) return;

    int r = cancelar_alocacao(ARQ_ALOCACOES, a->id_alocacao, ui->alocacoes, ui->qtd_alocacoes);
    if (r == 0) erro_ao_salvar(ui);
    atualizar_telas(ui);
}

// ---------- Importar planilha ----------
static gboolean mesma_aula(AlocacaoTurma *x, AlocacaoTurma *y) {
    return strcmp(x->dia_semana, y->dia_semana) == 0 && strcmp(x->hora_inicio, y->hora_inicio) == 0 &&
           strcmp(x->hora_fim, y->hora_fim) == 0 && strcmp(x->id_sala, y->id_sala) == 0 &&
           strcmp(x->disciplina, y->disciplina) == 0 && strcmp(x->turma, y->turma) == 0;
}

static void ao_clicar_importar(GtkWidget *btn, gpointer data) {
    (void)btn;
    InterfaceApp *ui = data;

    GtkWidget *chooser = gtk_file_chooser_dialog_new("Importar planilha (arquivo CSV)", GTK_WINDOW(ui->window),
        GTK_FILE_CHOOSER_ACTION_OPEN, "Cancelar", GTK_RESPONSE_CANCEL, "Importar", GTK_RESPONSE_ACCEPT, NULL);
    GtkFileFilter *filtro = gtk_file_filter_new();
    gtk_file_filter_set_name(filtro, "Planilha CSV (*.csv)");
    gtk_file_filter_add_pattern(filtro, "*.csv");
    gtk_file_filter_add_pattern(filtro, "*.CSV");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), filtro);

    char *arquivo = NULL;
    if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
        arquivo = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
    }
    gtk_widget_destroy(chooser);
    if (arquivo == NULL) return;

    // O GTK devolve o caminho em UTF-8, mas o fopen() do Windows não entende acentos nesse formato
    // (ex.: "C:\\Users\\Vinícius\\Área de Trabalho\\aulas.csv"). Por isso o arquivo é lido pela GLib,
    // que aceita qualquer caminho, e copiado para data/importacao_temp.csv (caminho simples) antes de
    // chamar o backend. Assim o backend continua usando fopen() normal, em Windows e Linux.
    char *conteudo = NULL;
    gsize tamanho = 0;
    gboolean leu = g_file_get_contents(arquivo, &conteudo, &tamanho, NULL);
    g_free(arquivo);
    const char *temporario = "data/importacao_temp.csv";
    if (!leu || !g_file_set_contents(temporario, conteudo, (gssize)tamanho, NULL)) {
        g_free(conteudo);
        mostrar_mensagem(ui, GTK_MESSAGE_ERROR, "Não foi possível abrir o arquivo.",
                         "Confira se o arquivo não está aberto em outro programa (como o Excel) e tente de novo.");
        return;
    }
    g_free(conteudo);

    int lidas = 0, ignoradas = 0;
    int ok = importar_grade_csv_detalhado(temporario, buffer_importacao, &lidas, MAX_ALOCACOES, &ignoradas);
    g_remove(temporario);

    if (!ok) {
        mostrar_mensagem(ui, GTK_MESSAGE_ERROR, "Não foi possível abrir o arquivo.",
                         "Confira se o arquivo não está aberto em outro programa e tente de novo.");
        return;
    }
    if (lidas == 0) {
        mostrar_mensagem(ui, GTK_MESSAGE_WARNING, "Nenhuma aula encontrada no arquivo.",
                         "No Google Planilhas use Arquivo > Fazer download > Valores separados por vírgula (.csv). "
                         "O arquivo precisa ter as colunas: departamento, componente, turma, docente, "
                         "dia/horário (ex.: SEG - 07:10 às 08:50) e sala.");
        return;
    }

    // Já existe grade? Pergunta se substitui tudo ou acrescenta (ex.: importar um dia de cada vez)
    gboolean substituir = TRUE;
    int ativas = 0;
    for (int i = 0; i < ui->qtd_alocacoes; i++) if (ui->alocacoes[i].ativo) ativas++;
    if (ativas > 0) {
        char det[300];
        snprintf(det, sizeof(det), "O arquivo tem %d aula(s). O sistema já tem %d aula(s) cadastrada(s).\n\n"
                 "\"Acrescentar\" junta as aulas novas às que já existem (aulas repetidas são ignoradas). "
                 "\"Substituir tudo\" apaga a grade atual e fica só com a do arquivo.", lidas, ativas);
        GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(ui->window), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
                                              GTK_BUTTONS_NONE, "Como importar esta planilha?");
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d), "%s", det);
        gtk_dialog_add_buttons(GTK_DIALOG(d), "Cancelar", GTK_RESPONSE_CANCEL,
                               "Substituir tudo", GTK_RESPONSE_REJECT, "Acrescentar", GTK_RESPONSE_ACCEPT, NULL);
        gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_ACCEPT);
        int r = gtk_dialog_run(GTK_DIALOG(d));
        gtk_widget_destroy(d);
        if (r == GTK_RESPONSE_REJECT) {
            if (!confirmar(ui, "Apagar a grade atual?", "Todas as aulas cadastradas serão trocadas pelas do arquivo.", "Substituir tudo")) return;
            substituir = TRUE;
        } else if (r == GTK_RESPONSE_ACCEPT) {
            substituir = FALSE;
        } else {
            return;
        }
    }

    // Monta a nova grade num vetor separado; só troca a grade da memória se gravar no disco com sucesso
    int total = 0, repetidas = 0;
    if (!substituir) {
        memcpy(buffer_mesclagem, ui->alocacoes, sizeof(AlocacaoTurma) * ui->qtd_alocacoes);
        total = ui->qtd_alocacoes;
    }
    int proximo_id = proximo_id_alocacao(buffer_mesclagem, total);
    for (int i = 0; i < lidas && total < MAX_ALOCACOES; i++) {
        int repetida = 0;
        for (int k = 0; k < total; k++) {
            if (buffer_mesclagem[k].ativo && mesma_aula(&buffer_mesclagem[k], &buffer_importacao[i])) { repetida = 1; break; }
        }
        if (repetida) { repetidas++; continue; }
        buffer_mesclagem[total] = buffer_importacao[i];
        buffer_mesclagem[total].id_alocacao = proximo_id++;
        total++;
    }

    if (!salvar_alocacoes(ARQ_ALOCACOES, buffer_mesclagem, total)) {
        erro_ao_salvar(ui);
        return;
    }
    memcpy(ui->alocacoes, buffer_mesclagem, sizeof(AlocacaoTurma) * total);
    ui->qtd_alocacoes = total;

    // Cadastra automaticamente as salas que apareceram na planilha
    for (int i = 0; i < total; i++) if (ui->alocacoes[i].ativo) cadastrar_sala_se_nova(ui->alocacoes[i].id_sala);
    recarregar_salas(ui);

    // Relatório: choques de horário que já vieram na planilha (para o DATP conferir com o departamento)
    GString *rel = g_string_new("");
    g_string_append_printf(rel, "%d aula(s) importada(s).", lidas - repetidas);
    if (repetidas > 0) g_string_append_printf(rel, " %d aula(s) repetida(s) foram ignoradas.", repetidas);
    if (ignoradas > 0) g_string_append_printf(rel, " %d linha(s) do arquivo não puderam ser lidas.", ignoradas);

    int choques = 0;
    for (int i = 0; i < ui->qtd_alocacoes; i++) {
        if (!ui->alocacoes[i].ativo) continue;
        int k = buscar_conflito(ui->alocacoes[i], ui->alocacoes, i); // só compara com as anteriores
        if (k < 0) continue;
        if (choques == 0) g_string_append(rel, "\n\nAtenção: a planilha tem choques de horário. Confira com o departamento:\n");
        if (choques < 10) {
            AlocacaoTurma *a = &ui->alocacoes[i], *b = &ui->alocacoes[k];
            g_string_append_printf(rel, "\n• Sala %s, %s: %s (%s) %s–%s  ×  %s (%s) %s–%s",
                                   a->id_sala, dia_extenso(a->dia_semana), b->disciplina, b->turma, b->hora_inicio, b->hora_fim,
                                   a->disciplina, a->turma, a->hora_inicio, a->hora_fim);
        }
        choques++;
    }
    if (choques > 10) g_string_append_printf(rel, "\n... e mais %d choque(s).", choques - 10);

    // Abre a tabela no dia da primeira aula importada
    int d = indice_dia(buffer_importacao[0].dia_semana);
    if (d >= 0) { ui->grade_dia = d; marcar_pilula(ui->grade_pills, d); }
    atualizar_telas(ui);

    mostrar_mensagem(ui, choques ? GTK_MESSAGE_WARNING : GTK_MESSAGE_INFO, "Importação concluída.", rel->str);
    g_string_free(rel, TRUE);
}

static GtkWidget *criar_botao_acao(const char *rotulo, const char *dica, GCallback cb, InterfaceApp *ui) {
    GtkWidget *b = gtk_button_new_with_label(rotulo);
    adicionar_classe(b, "pill-btn");
    gtk_widget_set_tooltip_text(b, dica);
    g_signal_connect(b, "clicked", cb, ui);
    return b;
}

static GtkWidget* criar_pagina_grade(InterfaceApp *ui) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 25);
    criar_cabecalho(vbox, "Planilha: Turma – Horário",
                    "Todas as aulas do dia escolhido. Selecione uma linha para trocar a sala ou cancelar a aula "
                    "(dois cliques na linha também abrem a troca de sala).");

    ui->grade_dia = ui->painel_dia;
    GtkWidget *linha = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_pack_start(GTK_BOX(linha), criar_barra_dias(ui->grade_pills, ui->grade_dia, G_CALLBACK(ao_trocar_dia_grade), ui), FALSE, FALSE, 0);
    ui->grade_busca = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(ui->grade_busca), "Buscar disciplina, docente, sala ou turma");
    gtk_entry_set_width_chars(GTK_ENTRY(ui->grade_busca), 36);
    g_signal_connect(ui->grade_busca, "search-changed", G_CALLBACK(ao_buscar_grade), ui);
    gtk_box_pack_end(GTK_BOX(linha), ui->grade_busca, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), linha, FALSE, FALSE, 0);

    GtkWidget *acoes = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(acoes), criar_botao_acao("+ Nova aula", "Cadastrar uma aula nova na grade", G_CALLBACK(ao_clicar_nova_aula), ui), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(acoes), criar_botao_acao("Trocar sala", "Mudar a sala da aula selecionada", G_CALLBACK(ao_clicar_trocar_sala), ui), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(acoes), criar_botao_acao("Cancelar aula", "Cancelar a aula selecionada e liberar a sala", G_CALLBACK(ao_clicar_cancelar_aula), ui), FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(acoes), criar_botao_acao("Importar planilha (CSV)", "Carregar a grade a partir da planilha do Google/Excel salva em CSV", G_CALLBACK(ao_clicar_importar), ui), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), acoes, FALSE, FALSE, 0);

    ui->grade_store = gtk_list_store_new(NUM_COLS, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                         G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    ui->grade_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(ui->grade_store));
    g_object_unref(ui->grade_store);
    gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(ui->grade_tree), GTK_TREE_VIEW_GRID_LINES_HORIZONTAL);
    g_signal_connect(ui->grade_tree, "row-activated", G_CALLBACK(ao_duplo_clique_linha), ui);

    const char *titulos[] = {NULL, "Depto", "Componente curricular", "Turma", "Docente(s)", "Horário", "Sala"};
    for (int c = COL_DEPTO; c < NUM_COLS; c++) {
        GtkCellRenderer *r = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes(titulos[c], r, "text", c, NULL);
        gtk_tree_view_column_set_sort_column_id(col, c);
        gtk_tree_view_column_set_resizable(col, TRUE);
        if (c == COL_COMPONENTE || c == COL_DOCENTE) {
            gtk_tree_view_column_set_expand(col, TRUE);
            g_object_set(r, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
        }
        if (c == COL_SALA) g_object_set(r, "weight", PANGO_WEIGHT_BOLD, NULL);
        gtk_tree_view_append_column(GTK_TREE_VIEW(ui->grade_tree), col);
    }

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), ui->grade_tree);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    ui->grade_contador = gtk_label_new("");
    adicionar_classe(ui->grade_contador, "page-subtitle");
    gtk_label_set_xalign(GTK_LABEL(ui->grade_contador), 0.0);
    gtk_box_pack_start(GTK_BOX(vbox), ui->grade_contador, FALSE, FALSE, 0);

    return vbox;
}

// =======================================================
// PÁGINA 3: SALAS LIVRES
// =======================================================

static void atualizar_salas_livres(InterfaceApp *ui) {
    gtk_list_store_clear(ui->livres_store);
    int d = gtk_combo_box_get_active(GTK_COMBO_BOX(ui->livres_combo_dia));
    char ini[6], fim[6];
    if (d < 0 ||
        !normalizar_hora(gtk_entry_get_text(GTK_ENTRY(entrada_do_combo(ui->livres_combo_ini))), ini) ||
        !normalizar_hora(gtk_entry_get_text(GTK_ENTRY(entrada_do_combo(ui->livres_combo_fim))), fim)) {
        gtk_label_set_text(GTK_LABEL(ui->livres_status), "Escolha o dia e escreva os horários no formato HH:MM (ex.: 07:10).");
        return;
    }
    if (strcmp(ini, fim) >= 0) {
        gtk_label_set_text(GTK_LABEL(ui->livres_status), "O horário de fim precisa ser depois do início.");
        return;
    }

    SalaFisica todas[MAX_SALAS], livres[MAX_SALAS];
    int n_todas = montar_lista_salas(ui, todas, MAX_SALAS), n_livres = 0;
    buscar_salas_livres(DIAS_SIGLA[d], ini, fim, livres, &n_livres, ui->alocacoes, ui->qtd_alocacoes, todas, n_todas);

    for (int i = 0; i < n_livres; i++) {
        char cap[20];
        if (livres[i].capacidade > 0) snprintf(cap, sizeof(cap), "%d lugares", livres[i].capacidade);
        else snprintf(cap, sizeof(cap), "—");
        gtk_list_store_insert_with_values(ui->livres_store, NULL, -1, 0, livres[i].id_sala, 1, cap, -1);
    }

    char status[160];
    if (n_todas == 0) snprintf(status, sizeof(status), "Nenhuma sala cadastrada ainda. Importe a planilha primeiro.");
    else snprintf(status, sizeof(status), "%d de %d salas livres na %s das %s às %s.", n_livres, n_todas, DIAS_EXTENSO[d], ini, fim);
    gtk_label_set_text(GTK_LABEL(ui->livres_status), status);
}

static void ao_mudar_filtro_livres(GtkWidget *w, gpointer data) {
    (void)w;
    atualizar_salas_livres(data);
}

static GtkWidget* criar_pagina_salas_livres(InterfaceApp *ui) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 25);
    criar_cabecalho(vbox, "Salas livres", "Escolha o dia e o intervalo de horário para ver quais salas estão desocupadas.");

    GtkWidget *linha = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    ui->livres_combo_dia = gtk_combo_box_text_new();
    preencher_combo(ui->livres_combo_dia, DIAS_NOME, NUM_DIAS);
    gtk_combo_box_set_active(GTK_COMBO_BOX(ui->livres_combo_dia), ui->painel_dia);
    ui->livres_combo_ini = gtk_combo_box_text_new_with_entry();
    preencher_combo(ui->livres_combo_ini, HORARIOS, NUM_HORARIOS - 1);
    ui->livres_combo_fim = gtk_combo_box_text_new_with_entry();
    preencher_combo(ui->livres_combo_fim, HORARIOS + 1, NUM_HORARIOS - 1);
    int bloco = gtk_combo_box_get_active(GTK_COMBO_BOX(ui->painel_combo_hora));
    gtk_combo_box_set_active(GTK_COMBO_BOX(ui->livres_combo_ini), bloco);
    gtk_combo_box_set_active(GTK_COMBO_BOX(ui->livres_combo_fim), bloco);

    gtk_box_pack_start(GTK_BOX(linha), gtk_label_new("Dia:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(linha), ui->livres_combo_dia, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(linha), gtk_label_new("Das"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(linha), ui->livres_combo_ini, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(linha), gtk_label_new("às"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(linha), ui->livres_combo_fim, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), linha, FALSE, FALSE, 0);

    g_signal_connect(ui->livres_combo_dia, "changed", G_CALLBACK(ao_mudar_filtro_livres), ui);
    g_signal_connect(ui->livres_combo_ini, "changed", G_CALLBACK(ao_mudar_filtro_livres), ui);
    g_signal_connect(ui->livres_combo_fim, "changed", G_CALLBACK(ao_mudar_filtro_livres), ui);

    ui->livres_status = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(ui->livres_status), 0.0);
    gtk_box_pack_start(GTK_BOX(vbox), ui->livres_status, FALSE, FALSE, 0);

    ui->livres_store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget *tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(ui->livres_store));
    g_object_unref(ui->livres_store);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree),
        gtk_tree_view_column_new_with_attributes("Sala", gtk_cell_renderer_text_new(), "text", 0, NULL));
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree),
        gtk_tree_view_column_new_with_attributes("Capacidade", gtk_cell_renderer_text_new(), "text", 1, NULL));

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), tree);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);
    return vbox;
}

// =======================================================
// PÁGINA 4: RESERVAS (ainda não implementada no backend)
// =======================================================

static GtkWidget* criar_pagina_reservas(void) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 25);
    criar_cabecalho(vbox, "Reservas", "Reservas avulsas (provas, eventos, reposições) estarão disponíveis em uma próxima versão. "
                    "Por enquanto, use \"+ Nova aula\" na planilha Turma – Horário.");
    return vbox;
}

// =======================================================
// JANELA PRINCIPAL
// =======================================================

void atualizar_telas(InterfaceApp *ui) {
    atualizar_painel(ui);
    atualizar_grade(ui);
    atualizar_salas_livres(ui);
}

static const char *NOMES_PAGINAS[NUM_PAGINAS] = {"painel", "grade", "livres", "reservas"};

static void ao_navegar(GtkWidget *btn, gpointer data) {
    InterfaceApp *ui = data;
    int pagina = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "pagina"));
    for (int i = 0; i < NUM_PAGINAS; i++) {
        GtkStyleContext *ctx = gtk_widget_get_style_context(ui->nav_btns[i]);
        if (i == pagina) gtk_style_context_add_class(ctx, "active");
        else gtk_style_context_remove_class(ctx, "active");
    }
    gtk_stack_set_visible_child_name(GTK_STACK(ui->stack), NOMES_PAGINAS[pagina]);
}

void criar_interface_principal(GtkApplication *app, InterfaceApp *ui) {
    carregar_css();

    ui->window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(ui->window), "Controle de Salas - POLI-UPE");
    gtk_window_set_default_size(GTK_WINDOW(ui->window), 1200, 720);

    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    adicionar_classe(main_box, "main-bg");
    gtk_container_add(GTK_CONTAINER(ui->window), main_box);

    // --- BARRA LATERAL (SIDEBAR) ---
    GtkWidget *sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(sidebar, 250, -1);
    adicionar_classe(sidebar, "sidebar");

    GtkWidget *lbl_brand = gtk_label_new("Controle\nde Salas");
    adicionar_classe(lbl_brand, "sidebar-title");
    gtk_label_set_xalign(GTK_LABEL(lbl_brand), 0.0);

    GtkWidget *lbl_semestre = gtk_label_new("SEMESTRE 2026.2");
    adicionar_classe(lbl_semestre, "sidebar-subtitle");
    gtk_label_set_xalign(GTK_LABEL(lbl_semestre), 0.0);

    gtk_box_pack_start(GTK_BOX(sidebar), lbl_brand, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(sidebar), lbl_semestre, FALSE, FALSE, 0);

    // Botões de Navegação da Sidebar
    const char *rotulos_nav[NUM_PAGINAS] = {"田  Painel de salas", "📄  Planilha: Turma – Horário",
                                            "🏛  Planilha: Salas Livres", "📅  Reservas"};
    for (int i = 0; i < NUM_PAGINAS; i++) {
        ui->nav_btns[i] = gtk_button_new_with_label(rotulos_nav[i]);
        adicionar_classe(ui->nav_btns[i], "nav-btn");
        if (i == 0) adicionar_classe(ui->nav_btns[i], "active");
        gtk_widget_set_halign(gtk_bin_get_child(GTK_BIN(ui->nav_btns[i])), GTK_ALIGN_START);
        g_object_set_data(G_OBJECT(ui->nav_btns[i]), "pagina", GINT_TO_POINTER(i));
        g_signal_connect(ui->nav_btns[i], "clicked", G_CALLBACK(ao_navegar), ui);
        gtk_box_pack_start(GTK_BOX(sidebar), ui->nav_btns[i], FALSE, FALSE, 0);
    }

    gtk_box_pack_start(GTK_BOX(main_box), sidebar, FALSE, FALSE, 0);

    // --- ÁREA DE CONTEÚDO PRINCIPAL (GTK STACK) ---
    ui->stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(ui->stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);

    gtk_stack_add_named(GTK_STACK(ui->stack), criar_pagina_painel_salas(ui), "painel");
    gtk_stack_add_named(GTK_STACK(ui->stack), criar_pagina_grade(ui), "grade");
    gtk_stack_add_named(GTK_STACK(ui->stack), criar_pagina_salas_livres(ui), "livres");
    gtk_stack_add_named(GTK_STACK(ui->stack), criar_pagina_reservas(), "reservas");

    gtk_box_pack_start(GTK_BOX(main_box), ui->stack, TRUE, TRUE, 0);

    atualizar_telas(ui);
    gtk_widget_show_all(ui->window);
}
