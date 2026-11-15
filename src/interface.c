/*
 * Interface grafica (GTK 3) do sistema de reserva de salas.
 *
 * Organizacao:
 *   - Estado global: vetores de salas e alocacoes carregados dos arquivos .dat
 *   - Utilitarios: mensagens, conversao de texto para UTF-8, colunas de tabela
 *   - Aba "Salas":        cadastrar, editar, remover salas
 *   - Aba "Reservas":     listar/filtrar, nova reserva, trocar sala, cancelar (liberar), importar CSV
 *   - Aba "Salas livres": buscar salas disponiveis num dia/horario e reservar
 *
 * IMPORTANTE: este arquivo esta em UTF-8 (por causa dos acentos nos textos).
 * No Code::Blocks: Settings > Editor > Encoding settings > UTF-8.
 */
#include <gtk/gtk.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/backend.h"
#include "../include/interface.h"

/* =======================================================
 * ESTADO GLOBAL
 * static: os vetores sao grandes demais para a pilha
 * ======================================================= */
static SalaFisica    salas[MAX_SALAS];
static int           qtd_salas = 0;
static AlocacaoTurma alocacoes[MAX_ALOCACOES];
static int           qtd_alocacoes = 0;
static SalaFisica    livres[MAX_SALAS];
static int           qtd_livres = 0;
static AlocacaoTurma filtradas[MAX_ALOCACOES];

static const char *DIAS[] = { "SEG", "TER", "QUA", "QUI", "SEX", "SAB", "DOM" };
#define QTD_DIAS 7

/* Colunas das tabelas */
enum { COL_S_INDICE, COL_S_NOME, COL_S_CAPACIDADE, COL_S_SITUACAO, COL_S_COR, N_COL_S };
enum { COL_R_ID, COL_R_DIA, COL_R_INICIO, COL_R_FIM, COL_R_SALA, COL_R_DISCIPLINA,
       COL_R_TURMA, COL_R_DEPARTAMENTO, COL_R_PROFESSORES, COL_R_SITUACAO, COL_R_COR, N_COL_R };

#define COR_INATIVO "#888888"

/* Widgets que precisam ser acessados pelos callbacks */
static struct {
    GtkWidget *janela;
    GtkWidget *status;

    /* Aba Salas */
    GtkListStore *loja_salas;
    GtkWidget *tv_salas;
    GtkWidget *ent_sala_nome;
    GtkWidget *spin_sala_capacidade;
    GtkWidget *chk_sala_manutencao;

    /* Aba Reservas */
    GtkListStore *loja_reservas;
    GtkWidget *tv_reservas;
    GtkWidget *cmb_filtro_dia;
    GtkWidget *cmb_filtro_sala;
    GtkWidget *chk_mostrar_canceladas;

    /* Aba Salas livres */
    GtkListStore *loja_livres;
    GtkWidget *tv_livres;
    GtkWidget *cmb_livre_dia;
    GtkWidget *ent_livre_inicio;
    GtkWidget *ent_livre_fim;
    GtkWidget *spin_livre_capacidade;
    int  busca_feita;
    char busca_dia[TAM_DIA];
    char busca_inicio[TAM_HORA];
    char busca_fim[TAM_HORA];
    int  busca_capacidade;

    /* Evita que o sinal "changed" dos filtros dispare enquanto os combos sao recriados */
    int atualizando_combos;
} ui;


/* =======================================================
 * UTILITARIOS
 * ======================================================= */

/* O GTK so aceita texto UTF-8. Dados vindos de um CSV salvo pelo Excel podem estar
 * em Windows-1252 (Latin-1); nesse caso converte, para nao aparecer lixo na tela.
 * Devolve uma copia que deve ser liberada com g_free(). */
static gchar *para_utf8(const char *texto) {
    if (g_utf8_validate(texto, -1, NULL)) return g_strdup(texto);

    gchar *convertido = g_convert(texto, -1, "UTF-8", "WINDOWS-1252", NULL, NULL, NULL);
    if (convertido != NULL) return convertido;

    return g_utf8_make_valid(texto, -1);
}

static void definir_status(const char *formato, ...) {
    va_list args;
    va_start(args, formato);
    gchar *texto = g_strdup_vprintf(formato, args);
    va_end(args);

    gchar *seguro = para_utf8(texto); /* o texto pode conter nomes vindos do CSV */
    gtk_label_set_text(GTK_LABEL(ui.status), seguro);
    g_free(seguro);
    g_free(texto);
}

static void mostrar_mensagem(GtkWidget *pai, GtkMessageType tipo, const char *texto) {
    GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(pai),
                                          GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                          tipo, GTK_BUTTONS_OK, "%s", texto);
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
}

static gboolean confirmar(GtkWidget *pai, const char *texto) {
    GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(pai),
                                          GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                          GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, "%s", texto);
    int resposta = gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
    return resposta == GTK_RESPONSE_YES;
}

/* Le o texto de um GtkEntry para um campo de tamanho fixo da struct */
static void ler_entry(GtkWidget *entry, char *destino, int tamanho) {
    copiar_texto(destino, gtk_entry_get_text(GTK_ENTRY(entry)), tamanho);
    aparar_espacos(destino);
}

static void adicionar_coluna(GtkWidget *tv, const char *titulo, int coluna, int coluna_cor) {
    GtkCellRenderer *r = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *c = gtk_tree_view_column_new_with_attributes(titulo, r, "text", coluna, NULL);
    if (coluna_cor >= 0) gtk_tree_view_column_add_attribute(c, r, "foreground", coluna_cor);
    gtk_tree_view_column_set_resizable(c, TRUE);
    gtk_tree_view_column_set_sort_column_id(c, coluna);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tv), c);
}

/* Coloca uma tabela dentro de uma area com barra de rolagem */
static GtkWidget *com_rolagem(GtkWidget *filho) {
    GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw), GTK_SHADOW_IN);
    gtk_widget_set_vexpand(sw, TRUE);
    gtk_widget_set_hexpand(sw, TRUE);
    gtk_container_add(GTK_CONTAINER(sw), filho);
    return sw;
}

static GtkWidget *rotulo(const char *texto) {
    GtkWidget *l = gtk_label_new(texto);
    gtk_widget_set_halign(l, GTK_ALIGN_END);
    return l;
}

static GtkWidget *combo_dias(const char *primeira_opcao) {
    GtkWidget *cmb = gtk_combo_box_text_new();
    if (primeira_opcao != NULL) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(cmb), primeira_opcao);
    for (int i = 0; i < QTD_DIAS; i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(cmb), DIAS[i]);
    gtk_combo_box_set_active(GTK_COMBO_BOX(cmb), 0);
    return cmb;
}

static void selecionar_dia(GtkWidget *cmb, const char *dia, int deslocamento) {
    for (int i = 0; i < QTD_DIAS; i++) {
        if (dia != NULL && strcmp(DIAS[i], dia) == 0) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(cmb), i + deslocamento);
            return;
        }
    }
}

/* Combo de salas. O "id" de cada item e o indice no vetor salas[], para recuperar
 * o nome original (sem conversao) na hora de chamar o backend. */
static void preencher_combo_salas(GtkWidget *cmb, const char *primeira_opcao,
                                  int apenas_operacionais, const char *selecionar) {
    char id[16];
    int ativo = 0, posicao = 0;

    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(cmb));
    if (primeira_opcao != NULL) {
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(cmb), "-1", primeira_opcao);
        posicao++;
    }

    for (int i = 0; i < qtd_salas; i++) {
        if (apenas_operacionais && salas[i].status_manutencao == 1) continue;
        gchar *nome = para_utf8(salas[i].id_sala);
        snprintf(id, sizeof(id), "%d", i);
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(cmb), id, nome);
        g_free(nome);
        if (selecionar != NULL && strcmp(salas[i].id_sala, selecionar) == 0) ativo = posicao;
        posicao++;
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(cmb), posicao > 0 ? ativo : -1);
}

/* Devolve o indice em salas[] escolhido no combo, ou -1 */
static int sala_do_combo(GtkWidget *cmb) {
    const gchar *id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(cmb));
    if (id == NULL) return -1;
    int i = atoi(id);
    return (i >= 0 && i < qtd_salas) ? i : -1;
}

/* Dia escolhido no combo (texto ASCII "SEG"...) ou "" para "Todos" */
static void dia_do_combo(GtkWidget *cmb, char *destino) {
    gchar *t = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(cmb));
    if (t != NULL && normalizar_dia(t, destino)) { g_free(t); return; }
    g_free(t);
    destino[0] = '\0';
}

static int indice_alocacao_por_id(int id) {
    for (int i = 0; i < qtd_alocacoes; i++) {
        if (alocacoes[i].id_alocacao == id) return i;
    }
    return -1;
}

static int contar_reservas_ativas_da_sala(const char *id_sala) {
    int n = 0;
    for (int i = 0; i < qtd_alocacoes; i++) {
        if (alocacoes[i].ativo == 1 && strcmp(alocacoes[i].id_sala, id_sala) == 0) n++;
    }
    return n;
}


/* =======================================================
 * ATUALIZACAO DAS TABELAS
 * ======================================================= */

static void atualizar_tabela_salas(void) {
    GtkTreeIter it;
    gtk_list_store_clear(ui.loja_salas);

    for (int i = 0; i < qtd_salas; i++) {
        gchar *nome = para_utf8(salas[i].id_sala);
        int manut = salas[i].status_manutencao == 1;
        gtk_list_store_append(ui.loja_salas, &it);
        gtk_list_store_set(ui.loja_salas, &it,
                           COL_S_INDICE, i,
                           COL_S_NOME, nome,
                           COL_S_CAPACIDADE, salas[i].capacidade,
                           COL_S_SITUACAO, manut ? "Em manutenção" : "Operacional",
                           COL_S_COR, manut ? COR_INATIVO : NULL,
                           -1);
        g_free(nome);
    }
}

static void atualizar_tabela_reservas(void) {
    char dia[TAM_DIA];
    char sala[TAM_ID_SALA] = "";
    int qtd_filtradas = 0;
    GtkTreeIter it;

    if (ui.atualizando_combos) return;

    dia_do_combo(ui.cmb_filtro_dia, dia);
    int s = sala_do_combo(ui.cmb_filtro_sala);
    if (s >= 0) copiar_texto(sala, salas[s].id_sala, TAM_ID_SALA);
    int mostrar_canceladas = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ui.chk_mostrar_canceladas));

    filtrar_alocacoes(alocacoes, qtd_alocacoes, dia, sala, !mostrar_canceladas,
                      filtradas, &qtd_filtradas, MAX_ALOCACOES);

    gtk_list_store_clear(ui.loja_reservas);
    for (int i = 0; i < qtd_filtradas; i++) {
        AlocacaoTurma *a = &filtradas[i];
        gchar *t_sala  = para_utf8(a->id_sala);
        gchar *t_disc  = para_utf8(a->disciplina);
        gchar *t_turma = para_utf8(a->turma);
        gchar *t_dep   = para_utf8(a->departamento);
        gchar *t_prof  = para_utf8(a->professores);

        gtk_list_store_append(ui.loja_reservas, &it);
        gtk_list_store_set(ui.loja_reservas, &it,
                           COL_R_ID, a->id_alocacao,
                           COL_R_DIA, a->dia_semana,
                           COL_R_INICIO, a->hora_inicio,
                           COL_R_FIM, a->hora_fim,
                           COL_R_SALA, t_sala,
                           COL_R_DISCIPLINA, t_disc,
                           COL_R_TURMA, t_turma,
                           COL_R_DEPARTAMENTO, t_dep,
                           COL_R_PROFESSORES, t_prof,
                           COL_R_SITUACAO, a->ativo ? "Ativa" : "Cancelada",
                           COL_R_COR, a->ativo ? NULL : COR_INATIVO,
                           -1);
        g_free(t_sala); g_free(t_disc); g_free(t_turma); g_free(t_dep); g_free(t_prof);
    }
}

static void atualizar_tabela_livres(void) {
    GtkTreeIter it;
    gtk_list_store_clear(ui.loja_livres);
    for (int i = 0; i < qtd_livres; i++) {
        gchar *nome = para_utf8(livres[i].id_sala);
        gtk_list_store_append(ui.loja_livres, &it);
        gtk_list_store_set(ui.loja_livres, &it,
                           COL_S_INDICE, i,
                           COL_S_NOME, nome,
                           COL_S_CAPACIDADE, livres[i].capacidade,
                           COL_S_SITUACAO, "Livre",
                           COL_S_COR, NULL,
                           -1);
        g_free(nome);
    }
}

/* Refaz a ultima busca de salas livres (depois de reservar/cancelar algo) */
static void refazer_busca_livres(void) {
    if (!ui.busca_feita) return;
    buscar_salas_livres(ui.busca_dia, ui.busca_inicio, ui.busca_fim, ui.busca_capacidade,
                        livres, &qtd_livres, alocacoes, qtd_alocacoes, salas, qtd_salas);
    atualizar_tabela_livres();
}

/* Recarrega salas do disco e atualiza tudo que depende delas */
static void recarregar_salas(void) {
    char filtro_atual[TAM_ID_SALA] = "";
    int s = sala_do_combo(ui.cmb_filtro_sala);
    if (s >= 0) copiar_texto(filtro_atual, salas[s].id_sala, TAM_ID_SALA);

    listar_todas_salas(ARQ_SALAS, salas, &qtd_salas, MAX_SALAS);
    atualizar_tabela_salas();

    ui.atualizando_combos = 1;
    preencher_combo_salas(ui.cmb_filtro_sala, "Todas", 0, filtro_atual);
    ui.atualizando_combos = 0;

    atualizar_tabela_reservas();
    refazer_busca_livres();
}

static void atualizar_apos_mudar_reservas(void) {
    atualizar_tabela_reservas();
    refazer_busca_livres();
}


/* =======================================================
 * ABA SALAS
 * ======================================================= */

/* Indice em salas[] da linha selecionada, ou -1 */
static int sala_selecionada(void) {
    GtkTreeModel *modelo;
    GtkTreeIter it;
    int indice = -1;
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(ui.tv_salas));
    if (gtk_tree_selection_get_selected(sel, &modelo, &it)) {
        gtk_tree_model_get(modelo, &it, COL_S_INDICE, &indice, -1);
    }
    return (indice >= 0 && indice < qtd_salas) ? indice : -1;
}

static void limpar_formulario_sala(void) {
    gtk_entry_set_text(GTK_ENTRY(ui.ent_sala_nome), "");
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ui.spin_sala_capacidade), 40);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui.chk_sala_manutencao), FALSE);
    gtk_widget_set_sensitive(ui.ent_sala_nome, TRUE);
}

static void ao_selecionar_sala(GtkTreeSelection *sel, gpointer dados) {
    (void)sel; (void)dados;
    int i = sala_selecionada();
    if (i < 0) return;

    gchar *nome = para_utf8(salas[i].id_sala);
    gtk_entry_set_text(GTK_ENTRY(ui.ent_sala_nome), nome);
    g_free(nome);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ui.spin_sala_capacidade), salas[i].capacidade);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui.chk_sala_manutencao), salas[i].status_manutencao == 1);
    /* O nome identifica a sala nas reservas, por isso nao pode ser editado */
    gtk_widget_set_sensitive(ui.ent_sala_nome, FALSE);
}

static void ao_clicar_nova_sala(GtkButton *b, gpointer dados) {
    (void)b; (void)dados;
    gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(ui.tv_salas)));
    limpar_formulario_sala();
    gtk_widget_grab_focus(ui.ent_sala_nome);
}

static void ao_clicar_cadastrar_sala(GtkButton *b, gpointer dados) {
    (void)b; (void)dados;
    SalaFisica nova;
    memset(&nova, 0, sizeof(nova));

    if (sala_selecionada() >= 0) {
        mostrar_mensagem(ui.janela, GTK_MESSAGE_INFO,
                         "Uma sala existente está selecionada.\n"
                         "Clique em \"Nova\" para limpar o formulário antes de cadastrar.");
        return;
    }

    ler_entry(ui.ent_sala_nome, nova.id_sala, TAM_ID_SALA);
    nova.capacidade = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(ui.spin_sala_capacidade));
    nova.status_manutencao = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ui.chk_sala_manutencao)) ? 1 : 0;

    int r = cadastrar_nova_sala(ARQ_SALAS, nova);
    if (r != RES_OK) {
        mostrar_mensagem(ui.janela, GTK_MESSAGE_ERROR, mensagem_erro(r));
        return;
    }
    recarregar_salas();
    limpar_formulario_sala();
    definir_status("Sala %s cadastrada.", nova.id_sala);
}

static void ao_clicar_salvar_sala(GtkButton *b, gpointer dados) {
    (void)b; (void)dados;
    int i = sala_selecionada();
    if (i < 0) {
        mostrar_mensagem(ui.janela, GTK_MESSAGE_INFO, "Selecione uma sala na tabela para editar.");
        return;
    }

    char nome[TAM_ID_SALA];
    copiar_texto(nome, salas[i].id_sala, TAM_ID_SALA);
    int capacidade = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(ui.spin_sala_capacidade));
    int manutencao = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ui.chk_sala_manutencao)) ? 1 : 0;

    int ativas = contar_reservas_ativas_da_sala(nome);
    if (manutencao && salas[i].status_manutencao == 0 && ativas > 0) {
        gchar *msg = g_strdup_printf("A sala possui %d reserva(s) ativa(s), que continuarão valendo.\n"
                                     "Colocar em manutenção mesmo assim?\n\n"
                                     "(Use \"Trocar sala\" na aba Reservas para remanejar as turmas.)",
                                     ativas);
        gboolean ok = confirmar(ui.janela, msg);
        g_free(msg);
        if (!ok) return;
    }

    int r = editar_sala(ARQ_SALAS, nome, capacidade, manutencao);
    if (r != RES_OK) {
        mostrar_mensagem(ui.janela, GTK_MESSAGE_ERROR, mensagem_erro(r));
        return;
    }
    recarregar_salas();
    limpar_formulario_sala();
    definir_status("Sala %s atualizada.", nome);
}

static void ao_clicar_remover_sala(GtkButton *b, gpointer dados) {
    (void)b; (void)dados;
    int i = sala_selecionada();
    if (i < 0) {
        mostrar_mensagem(ui.janela, GTK_MESSAGE_INFO, "Selecione uma sala na tabela para remover.");
        return;
    }

    char nome[TAM_ID_SALA];
    copiar_texto(nome, salas[i].id_sala, TAM_ID_SALA);
    gchar *exibido = para_utf8(nome);
    gchar *msg = g_strdup_printf("Remover a sala %s?\nEssa ação não pode ser desfeita.", exibido);
    gboolean ok = confirmar(ui.janela, msg);
    g_free(msg);
    g_free(exibido);
    if (!ok) return;

    int r = remover_sala(ARQ_SALAS, nome, alocacoes, qtd_alocacoes);
    if (r != RES_OK) {
        mostrar_mensagem(ui.janela, GTK_MESSAGE_ERROR, mensagem_erro(r));
        return;
    }
    recarregar_salas();
    limpar_formulario_sala();
    definir_status("Sala %s removida.", nome);
}

static GtkListStore *nova_loja_salas(void) {
    return gtk_list_store_new(N_COL_S, G_TYPE_INT, G_TYPE_STRING, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING);
}

static GtkWidget *criar_aba_salas(void) {
    GtkWidget *caixa = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(caixa), 12);

    /* Tabela */
    ui.loja_salas = nova_loja_salas();
    ui.tv_salas = gtk_tree_view_new_with_model(GTK_TREE_MODEL(ui.loja_salas));
    adicionar_coluna(ui.tv_salas, "Sala", COL_S_NOME, COL_S_COR);
    adicionar_coluna(ui.tv_salas, "Capacidade", COL_S_CAPACIDADE, COL_S_COR);
    adicionar_coluna(ui.tv_salas, "Situação", COL_S_SITUACAO, COL_S_COR);
    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(ui.tv_salas)), "changed",
                     G_CALLBACK(ao_selecionar_sala), NULL);
    gtk_box_pack_start(GTK_BOX(caixa), com_rolagem(ui.tv_salas), TRUE, TRUE, 0);

    /* Formulario */
    GtkWidget *quadro = gtk_frame_new("Dados da sala");
    GtkWidget *grade = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grade), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grade), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grade), 12);
    gtk_container_add(GTK_CONTAINER(quadro), grade);

    ui.ent_sala_nome = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(ui.ent_sala_nome), TAM_ID_SALA - 1);
    gtk_entry_set_placeholder_text(GTK_ENTRY(ui.ent_sala_nome), "Ex: A-03, LAB. MECT");
    ui.spin_sala_capacidade = gtk_spin_button_new_with_range(1, 1000, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ui.spin_sala_capacidade), 40);
    ui.chk_sala_manutencao = gtk_check_button_new_with_label("Em manutenção / inutilizada");

    gtk_grid_attach(GTK_GRID(grade), rotulo("Nome:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grade), ui.ent_sala_nome, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grade), rotulo("Capacidade:"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grade), ui.spin_sala_capacidade, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grade), ui.chk_sala_manutencao, 1, 2, 1, 1);

    GtkWidget *botoes = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *b_nova = gtk_button_new_with_label("Nova (limpar formulário)");
    GtkWidget *b_cadastrar = gtk_button_new_with_label("Cadastrar sala");
    GtkWidget *b_salvar = gtk_button_new_with_label("Salvar alterações");
    GtkWidget *b_remover = gtk_button_new_with_label("Remover sala");
    g_signal_connect(b_nova, "clicked", G_CALLBACK(ao_clicar_nova_sala), NULL);
    g_signal_connect(b_cadastrar, "clicked", G_CALLBACK(ao_clicar_cadastrar_sala), NULL);
    g_signal_connect(b_salvar, "clicked", G_CALLBACK(ao_clicar_salvar_sala), NULL);
    g_signal_connect(b_remover, "clicked", G_CALLBACK(ao_clicar_remover_sala), NULL);
    gtk_box_pack_start(GTK_BOX(botoes), b_nova, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(botoes), b_cadastrar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(botoes), b_salvar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(botoes), b_remover, FALSE, FALSE, 0);
    gtk_grid_attach(GTK_GRID(grade), botoes, 0, 3, 2, 1);

    GtkWidget *lateral = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(lateral), quadro, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(caixa), lateral, FALSE, FALSE, 0);

    return caixa;
}


/* =======================================================
 * ABA RESERVAS
 * ======================================================= */

/* id_alocacao da linha selecionada, ou -1 */
static int reserva_selecionada(void) {
    GtkTreeModel *modelo;
    GtkTreeIter it;
    int id = -1;
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(ui.tv_reservas));
    if (gtk_tree_selection_get_selected(sel, &modelo, &it)) {
        gtk_tree_model_get(modelo, &it, COL_R_ID, &id, -1);
    }
    return id;
}

static GtkWidget *entry_com_dica(const char *dica, int tamanho_max, const char *valor) {
    GtkWidget *e = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(e), tamanho_max - 1);
    gtk_entry_set_activates_default(GTK_ENTRY(e), TRUE);
    if (dica) gtk_entry_set_placeholder_text(GTK_ENTRY(e), dica);
    if (valor) gtk_entry_set_text(GTK_ENTRY(e), valor);
    return e;
}

/* Janela de nova reserva. Os parametros (podem ser NULL) pre-preenchem o formulario,
 * usado quando a reserva parte da aba "Salas livres". */
static void abrir_dialogo_reserva(const char *dia, const char *inicio, const char *fim, const char *sala) {
    if (qtd_salas == 0) {
        mostrar_mensagem(ui.janela, GTK_MESSAGE_INFO, "Cadastre ao menos uma sala antes de fazer reservas.");
        return;
    }

    GtkWidget *d = gtk_dialog_new_with_buttons("Nova reserva", GTK_WINDOW(ui.janela),
                                               GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                               "_Cancelar", GTK_RESPONSE_CANCEL,
                                               "_Reservar", GTK_RESPONSE_OK,
                                               NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_OK);

    GtkWidget *grade = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grade), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grade), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grade), 12);
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(d))), grade);

    GtkWidget *e_dep   = entry_com_dica("Ex: DSC", TAM_DEPARTAMENTO, NULL);
    GtkWidget *e_disc  = entry_com_dica("Ex: ESTRUTURA DE DADOS", TAM_DISCIPLINA, NULL);
    GtkWidget *e_turma = entry_com_dica("Ex: 2C", TAM_TURMA, NULL);
    GtkWidget *e_prof  = entry_com_dica("Ex: ANTONIO / STELA", TAM_PROFESSORES, NULL);
    GtkWidget *c_dia   = combo_dias(NULL);
    GtkWidget *e_ini   = entry_com_dica("HH:MM", TAM_HORA, inicio);
    GtkWidget *e_fim   = entry_com_dica("HH:MM", TAM_HORA, fim);
    GtkWidget *c_sala  = gtk_combo_box_text_new();
    preencher_combo_salas(c_sala, NULL, 1, sala);
    if (dia) selecionar_dia(c_dia, dia, 0);
    gtk_widget_set_hexpand(e_disc, TRUE);

    const char *rotulos[] = { "Departamento:", "Disciplina:", "Turma:", "Professor(es):",
                              "Dia da semana:", "Início:", "Fim:", "Sala:" };
    GtkWidget *campos[] = { e_dep, e_disc, e_turma, e_prof, c_dia, e_ini, e_fim, c_sala };
    for (int i = 0; i < 8; i++) {
        gtk_grid_attach(GTK_GRID(grade), rotulo(rotulos[i]), 0, i, 1, 1);
        gtk_grid_attach(GTK_GRID(grade), campos[i], 1, i, 1, 1);
    }

    gtk_widget_show_all(d);

    /* Repete ate dar certo ou o usuario cancelar, sem perder o que foi digitado */
    while (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK) {
        AlocacaoTurma nova;
        memset(&nova, 0, sizeof(nova));
        ler_entry(e_dep, nova.departamento, TAM_DEPARTAMENTO);
        ler_entry(e_disc, nova.disciplina, TAM_DISCIPLINA);
        ler_entry(e_turma, nova.turma, TAM_TURMA);
        ler_entry(e_prof, nova.professores, TAM_PROFESSORES);
        dia_do_combo(c_dia, nova.dia_semana);
        ler_entry(e_ini, nova.hora_inicio, TAM_HORA);
        ler_entry(e_fim, nova.hora_fim, TAM_HORA);

        int s = sala_do_combo(c_sala);
        if (s < 0) {
            mostrar_mensagem(d, GTK_MESSAGE_ERROR, "Escolha uma sala.");
            continue;
        }
        copiar_texto(nova.id_sala, salas[s].id_sala, TAM_ID_SALA);

        int id = 0;
        int r = adicionar_alocacao(nova, alocacoes, &qtd_alocacoes, MAX_ALOCACOES, ARQ_ALOCACOES,
                                   salas, qtd_salas, &id);
        if (r == RES_OK) {
            definir_status("Reserva %d criada: %s, %s %s-%s.", id, nova.id_sala,
                           alocacoes[qtd_alocacoes - 1].dia_semana,
                           alocacoes[qtd_alocacoes - 1].hora_inicio,
                           alocacoes[qtd_alocacoes - 1].hora_fim);
            break;
        }
        mostrar_mensagem(d, GTK_MESSAGE_ERROR, mensagem_erro(r));
    }

    gtk_widget_destroy(d);
    atualizar_apos_mudar_reservas();
}

static void ao_clicar_nova_reserva(GtkButton *b, gpointer dados) {
    (void)b; (void)dados;
    abrir_dialogo_reserva(NULL, NULL, NULL, NULL);
}

static void ao_clicar_trocar_sala(GtkButton *b, gpointer dados) {
    (void)b; (void)dados;
    int i = indice_alocacao_por_id(reserva_selecionada());
    if (i < 0) {
        mostrar_mensagem(ui.janela, GTK_MESSAGE_INFO, "Selecione uma reserva na tabela.");
        return;
    }
    if (alocacoes[i].ativo != 1) {
        mostrar_mensagem(ui.janela, GTK_MESSAGE_INFO, mensagem_erro(RES_JA_CANCELADA));
        return;
    }

    GtkWidget *d = gtk_dialog_new_with_buttons("Trocar sala", GTK_WINDOW(ui.janela),
                                               GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                               "_Cancelar", GTK_RESPONSE_CANCEL,
                                               "_Trocar", GTK_RESPONSE_OK,
                                               NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_OK);
    GtkWidget *caixa = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(caixa), 12);
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(d))), caixa);

    gchar *disc = para_utf8(alocacoes[i].disciplina);
    gchar *sala_atual = para_utf8(alocacoes[i].id_sala);
    gchar *texto = g_strdup_printf("%s (%s %s-%s)\nSala atual: %s\n\nNova sala:", disc,
                                   alocacoes[i].dia_semana, alocacoes[i].hora_inicio,
                                   alocacoes[i].hora_fim, sala_atual);
    GtkWidget *info = gtk_label_new(texto);
    gtk_label_set_xalign(GTK_LABEL(info), 0);
    g_free(texto); g_free(disc); g_free(sala_atual);

    GtkWidget *c_sala = gtk_combo_box_text_new();
    preencher_combo_salas(c_sala, NULL, 1, alocacoes[i].id_sala);
    gtk_box_pack_start(GTK_BOX(caixa), info, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(caixa), c_sala, FALSE, FALSE, 0);
    gtk_widget_show_all(d);

    int id = alocacoes[i].id_alocacao;
    while (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK) {
        int s = sala_do_combo(c_sala);
        if (s < 0) { mostrar_mensagem(d, GTK_MESSAGE_ERROR, "Escolha uma sala."); continue; }

        int r = atualizar_sala_alocacao(ARQ_ALOCACOES, id, salas[s].id_sala,
                                        alocacoes, qtd_alocacoes, salas, qtd_salas);
        if (r == RES_OK) {
            definir_status("Reserva %d movida para a sala %s.", id, salas[s].id_sala);
            break;
        }
        mostrar_mensagem(d, GTK_MESSAGE_ERROR, mensagem_erro(r));
    }

    gtk_widget_destroy(d);
    atualizar_apos_mudar_reservas();
}

static void ao_clicar_cancelar_reserva(GtkButton *b, gpointer dados) {
    (void)b; (void)dados;
    int i = indice_alocacao_por_id(reserva_selecionada());
    if (i < 0) {
        mostrar_mensagem(ui.janela, GTK_MESSAGE_INFO, "Selecione uma reserva na tabela.");
        return;
    }
    if (alocacoes[i].ativo != 1) {
        mostrar_mensagem(ui.janela, GTK_MESSAGE_INFO, mensagem_erro(RES_JA_CANCELADA));
        return;
    }

    gchar *disc = para_utf8(alocacoes[i].disciplina);
    gchar *sala = para_utf8(alocacoes[i].id_sala);
    gchar *msg = g_strdup_printf("Cancelar a reserva e liberar a sala %s?\n\n%s - turma %s\n%s, das %s às %s",
                                 sala, disc, alocacoes[i].turma, alocacoes[i].dia_semana,
                                 alocacoes[i].hora_inicio, alocacoes[i].hora_fim);
    gboolean ok = confirmar(ui.janela, msg);
    g_free(msg); g_free(disc); g_free(sala);
    if (!ok) return;

    int id = alocacoes[i].id_alocacao;
    int r = cancelar_alocacao(ARQ_ALOCACOES, id, alocacoes, qtd_alocacoes);
    if (r != RES_OK) {
        mostrar_mensagem(ui.janela, GTK_MESSAGE_ERROR, mensagem_erro(r));
        return;
    }
    definir_status("Reserva %d cancelada. Sala liberada.", id);
    atualizar_apos_mudar_reservas();
}

static void ao_clicar_importar_csv(GtkButton *b, gpointer dados) {
    (void)b; (void)dados;
    GtkWidget *d = gtk_file_chooser_dialog_new("Importar grade (CSV)", GTK_WINDOW(ui.janela),
                                               GTK_FILE_CHOOSER_ACTION_OPEN,
                                               "_Cancelar", GTK_RESPONSE_CANCEL,
                                               "_Importar", GTK_RESPONSE_ACCEPT,
                                               NULL);
    GtkFileFilter *filtro = gtk_file_filter_new();
    gtk_file_filter_set_name(filtro, "Planilha CSV (*.csv)");
    gtk_file_filter_add_pattern(filtro, "*.csv");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(d), filtro);
    /* O seletor exige caminho absoluto: pasta atual + "data" */
    gchar *atual = g_get_current_dir();
    gchar *pasta_dados = g_build_filename(atual, "data", NULL);
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(d), pasta_dados);
    g_free(pasta_dados);
    g_free(atual);

    gchar *caminho = NULL;
    if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT) {
        caminho = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));
    }
    gtk_widget_destroy(d);
    if (caminho == NULL) return;

    /* No Windows, fopen() nao entende caminhos UTF-8 com acentos; converte para a codificacao local */
    gchar *caminho_local = g_locale_from_utf8(caminho, -1, NULL, NULL, NULL);
    ResultadoImportacao res;
    int r = importar_grade_csv(caminho_local ? caminho_local : caminho, ARQ_ALOCACOES,
                               alocacoes, &qtd_alocacoes, MAX_ALOCACOES, salas, qtd_salas, &res);
    g_free(caminho_local);
    g_free(caminho);

    if (r != RES_OK) {
        mostrar_mensagem(ui.janela, GTK_MESSAGE_ERROR, mensagem_erro(r));
        return;
    }

    gchar *msg;
    if (res.rejeitadas > 0) {
        msg = g_strdup_printf("Importadas: %d\nRejeitadas: %d\n\nÚltimo erro na linha %d:\n%s\n\n"
                              "Dica: as salas precisam estar cadastradas antes da importação.",
                              res.importadas, res.rejeitadas, res.linha_ultimo_erro,
                              mensagem_erro(res.codigo_ultimo_erro));
    } else {
        msg = g_strdup_printf("Importadas: %d\nNenhuma linha rejeitada.", res.importadas);
    }
    mostrar_mensagem(ui.janela, res.rejeitadas > 0 ? GTK_MESSAGE_WARNING : GTK_MESSAGE_INFO, msg);
    g_free(msg);

    definir_status("Importação concluída: %d reserva(s) adicionada(s).", res.importadas);
    atualizar_apos_mudar_reservas();
}

static void ao_mudar_filtro(GtkWidget *w, gpointer dados) {
    (void)w; (void)dados;
    atualizar_tabela_reservas();
}

static GtkWidget *criar_aba_reservas(void) {
    GtkWidget *caixa = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(caixa), 12);

    /* Filtros */
    GtkWidget *filtros = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    ui.cmb_filtro_dia = combo_dias("Todos");
    ui.cmb_filtro_sala = gtk_combo_box_text_new();
    ui.chk_mostrar_canceladas = gtk_check_button_new_with_label("Mostrar canceladas");
    gtk_box_pack_start(GTK_BOX(filtros), gtk_label_new("Dia:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(filtros), ui.cmb_filtro_dia, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(filtros), gtk_label_new("Sala:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(filtros), ui.cmb_filtro_sala, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(filtros), ui.chk_mostrar_canceladas, FALSE, FALSE, 8);
    gtk_box_pack_start(GTK_BOX(caixa), filtros, FALSE, FALSE, 0);

    /* Tabela */
    ui.loja_reservas = gtk_list_store_new(N_COL_R, G_TYPE_INT,
                                          G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                          G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                          G_TYPE_STRING, G_TYPE_STRING);
    ui.tv_reservas = gtk_tree_view_new_with_model(GTK_TREE_MODEL(ui.loja_reservas));
    adicionar_coluna(ui.tv_reservas, "ID", COL_R_ID, COL_R_COR);
    adicionar_coluna(ui.tv_reservas, "Dia", COL_R_DIA, COL_R_COR);
    adicionar_coluna(ui.tv_reservas, "Início", COL_R_INICIO, COL_R_COR);
    adicionar_coluna(ui.tv_reservas, "Fim", COL_R_FIM, COL_R_COR);
    adicionar_coluna(ui.tv_reservas, "Sala", COL_R_SALA, COL_R_COR);
    adicionar_coluna(ui.tv_reservas, "Disciplina", COL_R_DISCIPLINA, COL_R_COR);
    adicionar_coluna(ui.tv_reservas, "Turma", COL_R_TURMA, COL_R_COR);
    adicionar_coluna(ui.tv_reservas, "Depto.", COL_R_DEPARTAMENTO, COL_R_COR);
    adicionar_coluna(ui.tv_reservas, "Professor(es)", COL_R_PROFESSORES, COL_R_COR);
    adicionar_coluna(ui.tv_reservas, "Situação", COL_R_SITUACAO, COL_R_COR);
    gtk_box_pack_start(GTK_BOX(caixa), com_rolagem(ui.tv_reservas), TRUE, TRUE, 0);

    /* Botoes */
    GtkWidget *botoes = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *b_nova = gtk_button_new_with_label("Nova reserva");
    GtkWidget *b_trocar = gtk_button_new_with_label("Trocar sala");
    GtkWidget *b_cancelar = gtk_button_new_with_label("Cancelar reserva (liberar sala)");
    GtkWidget *b_importar = gtk_button_new_with_label("Importar CSV...");
    g_signal_connect(b_nova, "clicked", G_CALLBACK(ao_clicar_nova_reserva), NULL);
    g_signal_connect(b_trocar, "clicked", G_CALLBACK(ao_clicar_trocar_sala), NULL);
    g_signal_connect(b_cancelar, "clicked", G_CALLBACK(ao_clicar_cancelar_reserva), NULL);
    g_signal_connect(b_importar, "clicked", G_CALLBACK(ao_clicar_importar_csv), NULL);
    gtk_box_pack_start(GTK_BOX(botoes), b_nova, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(botoes), b_trocar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(botoes), b_cancelar, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(botoes), b_importar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(caixa), botoes, FALSE, FALSE, 0);

    g_signal_connect(ui.cmb_filtro_dia, "changed", G_CALLBACK(ao_mudar_filtro), NULL);
    g_signal_connect(ui.cmb_filtro_sala, "changed", G_CALLBACK(ao_mudar_filtro), NULL);
    g_signal_connect(ui.chk_mostrar_canceladas, "toggled", G_CALLBACK(ao_mudar_filtro), NULL);

    return caixa;
}


/* =======================================================
 * ABA SALAS LIVRES
 * ======================================================= */

static void ao_clicar_buscar_livres(GtkButton *b, gpointer dados) {
    (void)b; (void)dados;
    char dia[TAM_DIA], inicio[TAM_HORA], fim[TAM_HORA];

    dia_do_combo(ui.cmb_livre_dia, dia);
    ler_entry(ui.ent_livre_inicio, inicio, TAM_HORA);
    ler_entry(ui.ent_livre_fim, fim, TAM_HORA);
    int capacidade = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(ui.spin_livre_capacidade));

    int r = buscar_salas_livres(dia, inicio, fim, capacidade, livres, &qtd_livres,
                                alocacoes, qtd_alocacoes, salas, qtd_salas);
    if (r != RES_OK) {
        mostrar_mensagem(ui.janela, GTK_MESSAGE_ERROR,
                         "Informe o dia e os horários no formato HH:MM (início antes do fim).");
        return;
    }

    /* Guarda a busca ja normalizada, para refaze-la quando as reservas mudarem */
    ui.busca_feita = 1;
    copiar_texto(ui.busca_dia, dia, TAM_DIA);
    normalizar_hora(inicio, ui.busca_inicio);
    normalizar_hora(fim, ui.busca_fim);
    ui.busca_capacidade = capacidade;

    atualizar_tabela_livres();
    definir_status("%d sala(s) livre(s) na %s das %s às %s.", qtd_livres,
                   ui.busca_dia, ui.busca_inicio, ui.busca_fim);
}

static void ao_clicar_reservar_livre(GtkButton *b, gpointer dados) {
    (void)b; (void)dados;
    GtkTreeModel *modelo;
    GtkTreeIter it;
    int indice = -1;

    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(ui.tv_livres));
    if (gtk_tree_selection_get_selected(sel, &modelo, &it)) {
        gtk_tree_model_get(modelo, &it, COL_S_INDICE, &indice, -1);
    }
    if (indice < 0 || indice >= qtd_livres || !ui.busca_feita) {
        mostrar_mensagem(ui.janela, GTK_MESSAGE_INFO, "Faça uma busca e selecione uma sala livre.");
        return;
    }

    char sala[TAM_ID_SALA];
    copiar_texto(sala, livres[indice].id_sala, TAM_ID_SALA);
    abrir_dialogo_reserva(ui.busca_dia, ui.busca_inicio, ui.busca_fim, sala);
}

static void ao_ativar_linha_livre(GtkTreeView *tv, GtkTreePath *p, GtkTreeViewColumn *c, gpointer dados) {
    (void)tv; (void)p; (void)c;
    ao_clicar_reservar_livre(NULL, dados);
}

static GtkWidget *criar_aba_livres(void) {
    GtkWidget *caixa = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(caixa), 12);

    GtkWidget *busca = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    ui.cmb_livre_dia = combo_dias(NULL);
    ui.ent_livre_inicio = entry_com_dica("HH:MM", TAM_HORA, "07:10");
    ui.ent_livre_fim = entry_com_dica("HH:MM", TAM_HORA, "08:50");
    gtk_entry_set_width_chars(GTK_ENTRY(ui.ent_livre_inicio), 6);
    gtk_entry_set_width_chars(GTK_ENTRY(ui.ent_livre_fim), 6);
    ui.spin_livre_capacidade = gtk_spin_button_new_with_range(0, 1000, 5);
    GtkWidget *b_buscar = gtk_button_new_with_label("Buscar");
    g_signal_connect(b_buscar, "clicked", G_CALLBACK(ao_clicar_buscar_livres), NULL);

    gtk_box_pack_start(GTK_BOX(busca), gtk_label_new("Dia:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(busca), ui.cmb_livre_dia, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(busca), gtk_label_new("das"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(busca), ui.ent_livre_inicio, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(busca), gtk_label_new("às"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(busca), ui.ent_livre_fim, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(busca), gtk_label_new("Lugares (mín.):"), FALSE, FALSE, 8);
    gtk_box_pack_start(GTK_BOX(busca), ui.spin_livre_capacidade, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(busca), b_buscar, FALSE, FALSE, 8);
    gtk_box_pack_start(GTK_BOX(caixa), busca, FALSE, FALSE, 0);

    ui.loja_livres = nova_loja_salas();
    ui.tv_livres = gtk_tree_view_new_with_model(GTK_TREE_MODEL(ui.loja_livres));
    adicionar_coluna(ui.tv_livres, "Sala", COL_S_NOME, -1);
    adicionar_coluna(ui.tv_livres, "Capacidade", COL_S_CAPACIDADE, -1);
    g_signal_connect(ui.tv_livres, "row-activated", G_CALLBACK(ao_ativar_linha_livre), NULL);
    gtk_box_pack_start(GTK_BOX(caixa), com_rolagem(ui.tv_livres), TRUE, TRUE, 0);

    GtkWidget *b_reservar = gtk_button_new_with_label("Reservar sala selecionada");
    g_signal_connect(b_reservar, "clicked", G_CALLBACK(ao_clicar_reservar_livre), NULL);
    GtkWidget *rodape = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(rodape), b_reservar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rodape),
                       gtk_label_new("Salas em manutenção não aparecem. Dica: duplo clique para reservar."),
                       FALSE, FALSE, 8);
    gtk_box_pack_start(GTK_BOX(caixa), rodape, FALSE, FALSE, 0);

    return caixa;
}


/* =======================================================
 * JANELA PRINCIPAL
 * ======================================================= */

void criar_janela_principal(void) {
    memset(&ui, 0, sizeof(ui));

    ui.janela = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(ui.janela), "DATP - Gestão e Alocação de Salas - POLI-UPE");
    gtk_window_set_default_size(GTK_WINDOW(ui.janela), 1050, 620);
    g_signal_connect(ui.janela, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *vertical = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(ui.janela), vertical);

    GtkWidget *abas = gtk_notebook_new();
    gtk_notebook_append_page(GTK_NOTEBOOK(abas), criar_aba_reservas(), gtk_label_new("Reservas"));
    gtk_notebook_append_page(GTK_NOTEBOOK(abas), criar_aba_livres(), gtk_label_new("Salas livres"));
    gtk_notebook_append_page(GTK_NOTEBOOK(abas), criar_aba_salas(), gtk_label_new("Salas"));
    gtk_box_pack_start(GTK_BOX(vertical), abas, TRUE, TRUE, 0);

    ui.status = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(ui.status), 0);
    gtk_widget_set_margin_start(ui.status, 12);
    gtk_widget_set_margin_top(ui.status, 4);
    gtk_widget_set_margin_bottom(ui.status, 6);
    gtk_box_pack_start(GTK_BOX(vertical), ui.status, FALSE, FALSE, 0);

    /* Carrega os dados dos arquivos */
    carregar_alocacoes(ARQ_ALOCACOES, alocacoes, &qtd_alocacoes, MAX_ALOCACOES);
    recarregar_salas();

    int ativas = 0;
    for (int i = 0; i < qtd_alocacoes; i++) if (alocacoes[i].ativo == 1) ativas++;
    definir_status("%d sala(s) cadastrada(s), %d reserva(s) ativa(s).", qtd_salas, ativas);

    gtk_widget_show_all(ui.janela);
}
