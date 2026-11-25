#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "interface.h"
#include "../include/backend.h"
#include "pdf_para_csv.h"

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

// Colunas da tabela de salas livres
enum { LIV_DIA, LIV_HORARIO, LIV_SALA, LIV_CAP, LIV_DIA_IDX, LIV_INI, LIV_FIM, LIV_NUM_COLS };

// Buffers grandes ficam fora da pilha
static AlocacaoTurma buffer_importacao[MAX_ALOCACOES];
static AlocacaoTurma buffer_mesclagem[MAX_ALOCACOES];

// Funções definidas mais abaixo, mas usadas antes (em C a função precisa ser "anunciada" antes do uso)
static void usar_sala_livre(InterfaceApp *ui, const char *sala, int dia, const char *ini, const char *fim);
static void abrir_nova_aula(InterfaceApp *ui, int dia, const char *ini, const char *fim, const char *sala);
static void ir_para_pagina(InterfaceApp *ui, int pagina);
static void ao_clicar_exportar_livres(GtkWidget *btn, gpointer data);
static void atualizar_notificacoes(InterfaceApp *ui);

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
// DESFAZER: balão que aparece por alguns segundos depois de cada ação
// =======================================================

#define SEGUNDOS_BALAO 8

// "Fotos" dos dados. A pendente é tirada no começo da ação; a de desfazer
// só é trocada quando a ação dá certo (assim, uma ação cancelada no meio
// não estraga o Desfazer da ação anterior).
static AlocacaoTurma foto_pendente_aulas[MAX_ALOCACOES];
static SalaFisica    foto_pendente_salas[MAX_SALAS];
static int foto_pendente_qtd_aulas = 0, foto_pendente_qtd_salas = 0;

static AlocacaoTurma foto_desfazer_aulas[MAX_ALOCACOES];
static SalaFisica    foto_desfazer_salas[MAX_SALAS];
static int foto_desfazer_qtd_aulas = 0, foto_desfazer_qtd_salas = 0;

// Peças do balão (criadas uma vez só, em criar_balao)
static GtkWidget *balao_revealer = NULL;
static GtkWidget *balao_texto = NULL;
static GtkWidget *balao_btn_desfazer = NULL;
static guint balao_timer = 0; // número do "despertador" que esconde o balão (0 = nenhum)
static InterfaceApp *ui_do_balao = NULL; // guardado em criar_balao, para atualizar o histórico na tela

// PASSO A da ação: tira a foto de como tudo está ANTES de mudar
static void preparar_desfazer(InterfaceApp *ui) {
    memcpy(foto_pendente_aulas, ui->alocacoes, sizeof(AlocacaoTurma) * ui->qtd_alocacoes);
    foto_pendente_qtd_aulas = ui->qtd_alocacoes;
    memcpy(foto_pendente_salas, ui->salas, sizeof(SalaFisica) * ui->qtd_salas);
    foto_pendente_qtd_salas = ui->qtd_salas;
}

// O despertador tocou: esconde o balão
static gboolean esconder_balao(gpointer data) {
    (void)data;
    gtk_revealer_set_reveal_child(GTK_REVEALER(balao_revealer), FALSE);
    balao_timer = 0;
    return G_SOURCE_REMOVE; // não repete
}

// Mostra o balão com um texto; com_desfazer diz se o botão "Desfazer" aparece
static void mostrar_balao(const char *texto, gboolean com_desfazer) {
    gtk_label_set_text(GTK_LABEL(balao_texto), texto);
    gtk_widget_set_visible(balao_btn_desfazer, com_desfazer);
    gtk_revealer_set_reveal_child(GTK_REVEALER(balao_revealer), TRUE);

    if (balao_timer != 0) g_source_remove(balao_timer); // já tinha um balão: recomeça a contagem
    balao_timer = g_timeout_add_seconds(SEGUNDOS_BALAO, esconder_balao, NULL);
}

// Guarda a ação no histórico (data/historico.txt), mostrado na aba Notificações
static char ultima_acao[400] = "";
static void registrar_no_historico(const char *texto) {
    registrar_historico(ARQ_HISTORICO, texto);
    if (ui_do_balao) atualizar_notificacoes(ui_do_balao); // a lista do histórico já mostra a mudança
}

// PASSO B da ação: deu certo, então a foto vira a do Desfazer e o balão aparece
static void oferecer_desfazer(const char *texto) {
    copiar_texto(ultima_acao, sizeof(ultima_acao), texto);
    registrar_no_historico(texto);
    memcpy(foto_desfazer_aulas, foto_pendente_aulas, sizeof(AlocacaoTurma) * foto_pendente_qtd_aulas);
    foto_desfazer_qtd_aulas = foto_pendente_qtd_aulas;
    memcpy(foto_desfazer_salas, foto_pendente_salas, sizeof(SalaFisica) * foto_pendente_qtd_salas);
    foto_desfazer_qtd_salas = foto_pendente_qtd_salas;
    mostrar_balao(texto, TRUE);
}

// Clique em "Desfazer": volta a foto para a memória e para os arquivos .dat
static void ao_clicar_desfazer(GtkWidget *btn, gpointer data) {
    (void)btn;
    InterfaceApp *ui = data;

    int ok_aulas = salvar_alocacoes(ARQ_ALOCACOES, foto_desfazer_aulas, foto_desfazer_qtd_aulas);
    int ok_salas = gravar_todas_salas(ARQ_SALAS, foto_desfazer_salas, foto_desfazer_qtd_salas);
    if (!ok_aulas || !ok_salas) {
        carregar_dados(ui); // mostra o que realmente ficou gravado no disco
        atualizar_telas(ui);
        erro_ao_salvar(ui);
        return;
    }
    memcpy(ui->alocacoes, foto_desfazer_aulas, sizeof(AlocacaoTurma) * foto_desfazer_qtd_aulas);
    ui->qtd_alocacoes = foto_desfazer_qtd_aulas;
    recarregar_salas(ui);

    char registro[450];
    snprintf(registro, sizeof(registro), "Desfeito: %s", ultima_acao);
    registrar_no_historico(registro);
    atualizar_telas(ui);
    mostrar_balao("Pronto, a ação foi desfeita.", FALSE);
}

// Clique no "✕": fecha o balão antes da hora
static void ao_fechar_balao(GtkWidget *btn, gpointer data) {
    (void)btn; (void)data;
    if (balao_timer != 0) {
        g_source_remove(balao_timer);
        balao_timer = 0;
    }
    gtk_revealer_set_reveal_child(GTK_REVEALER(balao_revealer), FALSE);
}

// Monta o balão (fica escondido até alguém chamar mostrar_balao)
static GtkWidget *criar_balao(InterfaceApp *ui) {
    ui_do_balao = ui;
    balao_revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(balao_revealer), GTK_REVEALER_TRANSITION_TYPE_SLIDE_UP);
    gtk_widget_set_halign(balao_revealer, GTK_ALIGN_CENTER); // centralizado...
    gtk_widget_set_valign(balao_revealer, GTK_ALIGN_END);    // ...lá embaixo

    GtkWidget *caixa = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    adicionar_classe(caixa, "balao");

    balao_texto = gtk_label_new("");
    gtk_label_set_line_wrap(GTK_LABEL(balao_texto), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(balao_texto), 60);

    balao_btn_desfazer = gtk_button_new_with_label("Desfazer");
    adicionar_classe(balao_btn_desfazer, "balao-desfazer");
    g_signal_connect(balao_btn_desfazer, "clicked", G_CALLBACK(ao_clicar_desfazer), ui);

    GtkWidget *fechar = gtk_button_new_with_label("✕");
    adicionar_classe(fechar, "balao-fechar");
    gtk_widget_set_tooltip_text(fechar, "Fechar aviso");
    g_signal_connect(fechar, "clicked", G_CALLBACK(ao_fechar_balao), NULL);

    gtk_box_pack_start(GTK_BOX(caixa), balao_texto, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(caixa), balao_btn_desfazer, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(caixa), fechar, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(balao_revealer), caixa);
    return balao_revealer;
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

// Mostra todas as aulas de uma sala num dia
static void mostrar_programacao_sala(InterfaceApp *ui, const char *sala, int dia_idx) {
    const char *dia = DIAS_SIGLA[dia_idx];
    AlocacaoTurma *aulas[MAX_ALOCACOES];
    int n = 0;
    for (int i = 0; i < ui->qtd_alocacoes; i++) {
        AlocacaoTurma *a = &ui->alocacoes[i];
        if (a->ativo && strcmp(a->id_sala, sala) == 0 && strcmp(a->dia_semana, dia) == 0) aulas[n++] = a;
    }
    qsort(aulas, n, sizeof(AlocacaoTurma *), ordem_aulas);

    GString *txt = g_string_new("");
    for (int i = 0; i < n; i++) {
        g_string_append_printf(txt, "%s – %s   %s (turma %s)\n%s\n\n",
                               aulas[i]->hora_inicio, aulas[i]->hora_fim, aulas[i]->disciplina, aulas[i]->turma, aulas[i]->professores);
    }
    if (n == 0) g_string_append(txt, "Nenhuma aula nesta sala neste dia. A sala está livre o dia todo.");

    char titulo[120];
    snprintf(titulo, sizeof(titulo), "Sala %s – %s", sala, dia_extenso(dia));
    mostrar_mensagem(ui, GTK_MESSAGE_INFO, titulo, txt->str);
    g_string_free(txt, TRUE);
}

// Clique num card: sala livre -> oferece usar a sala; ocupada -> mostra a programação do dia
static gboolean ao_clicar_card(GtkWidget *w, GdkEventButton *ev, gpointer data) {
    (void)ev;
    InterfaceApp *ui = data;
    char sala[25];
    copiar_texto(sala, sizeof(sala), g_object_get_data(G_OBJECT(w), "sala")); // cópia: o card pode ser recriado
    gboolean livre = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "livre"));
    if (livre) {
        const char *ini, *fim;
        painel_intervalo(ui, &ini, &fim);
        usar_sala_livre(ui, sala, ui->painel_dia, ini, fim);
    } else {
        mostrar_programacao_sala(ui, sala, ui->painel_dia);
    }
    return TRUE;
}

// -------------------------------------------------------
// Excluir e renomear sala (botões no card do painel)
// -------------------------------------------------------

// Conta quantas aulas ATIVAS usam a sala (em qualquer dia)
static int contar_aulas_da_sala(InterfaceApp *ui, const char *sala) {
    int n = 0;
    for (int i = 0; i < ui->qtd_alocacoes; i++) {
        if (ui->alocacoes[i].ativo && strcmp(ui->alocacoes[i].id_sala, sala) == 0) n++;
    }
    return n;
}

static void ao_clicar_excluir_sala(GtkWidget *btn, gpointer data) {
    InterfaceApp *ui = data;
    // Copia o nome agora: atualizar_telas() destrói o card (e o botão) mais abaixo
    char sala[25];
    copiar_texto(sala, sizeof(sala), g_object_get_data(G_OBJECT(btn), "sala"));
    preparar_desfazer(ui);

    // Segurança: não deixa excluir sala que ainda tem aula, para nenhuma turma ficar sem sala
    int aulas = contar_aulas_da_sala(ui, sala);
    if (aulas > 0) {
        char titulo[120], detalhe[400];
        snprintf(titulo, sizeof(titulo), "A sala %s ainda tem %d aula(s) na semana.", sala, aulas);
        snprintf(detalhe, sizeof(detalhe),
                 "Para nenhuma turma ficar sem sala, mude essas aulas antes de excluir: "
                 "vá em \"Planilha: Turma – Horário\", digite %s na busca e use \"Trocar sala\" em cada aula. "
                 "Depois volte aqui e exclua a sala.", sala);
        mostrar_mensagem(ui, GTK_MESSAGE_WARNING, titulo, detalhe);
        return;
    }

    char titulo[120];
    snprintf(titulo, sizeof(titulo), "Excluir a sala %s?", sala);
    if (!confirmar(ui, titulo, "Ela deixa de aparecer no painel e na lista de salas livres. "
                               "Se precisar dela de novo, é só cadastrar uma aula nela.", "Excluir sala")) return;

    if (excluir_sala(ARQ_SALAS, sala) == 0) {
        mostrar_mensagem(ui, GTK_MESSAGE_ERROR, "Não foi possível excluir a sala.",
                         "O programa não conseguiu gravar o arquivo " ARQ_SALAS ". Nada foi alterado.");
        return;
    }
    recarregar_salas(ui);
    atualizar_telas(ui);

    char aviso[100];
    snprintf(aviso, sizeof(aviso), "Sala %s excluída.", sala);
    oferecer_desfazer(aviso);
}

static void ao_clicar_renomear_sala(GtkWidget *btn, gpointer data) {
    InterfaceApp *ui = data;
    char antigo[25];
    copiar_texto(antigo, sizeof(antigo), g_object_get_data(G_OBJECT(btn), "sala"));
    preparar_desfazer(ui);

    // Janelinha com uma caixa de texto já preenchida com o nome atual
    GtkWidget *d = gtk_dialog_new_with_buttons("Renomear sala", GTK_WINDOW(ui->window),
                                               GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                               "Voltar", GTK_RESPONSE_CANCEL,
                                               "Salvar novo nome", GTK_RESPONSE_OK, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_OK);
    GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(d));
    gtk_container_set_border_width(GTK_CONTAINER(area), 15);
    gtk_box_set_spacing(GTK_BOX(area), 8);

    char texto[200];
    snprintf(texto, sizeof(texto), "Novo nome para a sala %s.\nTodas as aulas desta sala passam a usar o novo nome.", antigo);
    GtkWidget *lbl = gtk_label_new(texto);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    GtkWidget *entrada = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entrada), antigo);
    gtk_entry_set_max_length(GTK_ENTRY(entrada), 24);           // cabe em id_sala[25]
    gtk_entry_set_activates_default(GTK_ENTRY(entrada), TRUE);  // Enter = Salvar
    gtk_box_pack_start(GTK_BOX(area), lbl, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(area), entrada, FALSE, FALSE, 0);
    gtk_widget_show_all(d);

    int resposta = gtk_dialog_run(GTK_DIALOG(d));
    char novo[25];
    char *limpo = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(entrada)))); // tira espaços das pontas
    copiar_texto(novo, sizeof(novo), limpo);
    g_free(limpo);
    gtk_widget_destroy(d);

    if (resposta != GTK_RESPONSE_OK || strcmp(novo, antigo) == 0) return;
    if (novo[0] == '\0') {
        mostrar_mensagem(ui, GTK_MESSAGE_WARNING, "O nome da sala não pode ficar vazio.", NULL);
        return;
    }
    // Não deixa criar duas salas com o mesmo nome (ignora maiúsculas/minúsculas)
    if (g_ascii_strcasecmp(novo, antigo) != 0 && sala_conhecida(ui, novo)) {
        char titulo[120];
        snprintf(titulo, sizeof(titulo), "Já existe uma sala chamada %s.", novo);
        mostrar_mensagem(ui, GTK_MESSAGE_WARNING, titulo,
                         "Escolha outro nome. Se as duas forem a mesma sala, troque a sala das aulas "
                         "em \"Planilha: Turma – Horário\" e depois exclua a sala que sobrou.");
        return;
    }

    // 1º as aulas, 2º o cadastro da sala
    int alteradas = renomear_sala_nas_alocacoes(ARQ_ALOCACOES, antigo, novo, ui->alocacoes, ui->qtd_alocacoes);
    if (alteradas < 0) {
        carregar_dados(ui); // desfaz na memória: lê de novo o que está no disco
        atualizar_telas(ui);
        erro_ao_salvar(ui);
        return;
    }
    if (renomear_sala(ARQ_SALAS, antigo, novo) == 0) {
        mostrar_mensagem(ui, GTK_MESSAGE_ERROR, "Não foi possível gravar o novo nome no arquivo de salas.",
                         "As aulas já foram atualizadas, mas o arquivo " ARQ_SALAS " não pôde ser gravado.");
    }
    recarregar_salas(ui);
    atualizar_telas(ui);

    char aviso[150];
    snprintf(aviso, sizeof(aviso), "Sala %s agora se chama %s (%d aula(s) atualizada(s)).", antigo, novo, alteradas);
    oferecer_desfazer(aviso);
}

// Clique em "Manutenção" / "Liberar sala": troca o estado da sala
static void ao_clicar_manutencao_sala(GtkWidget *btn, gpointer data) {
    InterfaceApp *ui = data;
    // Copia o nome agora: atualizar_telas() destrói o card (e o botão) mais abaixo
    char sala[25];
    copiar_texto(sala, sizeof(sala), g_object_get_data(G_OBJECT(btn), "sala"));
    int em_manutencao = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "manutencao"));
    int novo_estado = !em_manutencao;
    preparar_desfazer(ui);

    char titulo[120], detalhe[500];
    if (novo_estado) {
        int aulas = contar_aulas_da_sala(ui, sala);
        snprintf(titulo, sizeof(titulo), "Colocar a sala %s em manutenção?", sala);
        if (aulas > 0) {
            snprintf(detalhe, sizeof(detalhe),
                     "Ela deixa de aparecer como livre. Atenção: ela ainda tem %d aula(s) na semana. "
                     "Essas aulas continuam marcadas nela até você mudar: vá em \"Planilha: Turma – Horário\", "
                     "digite %s na busca e use \"Trocar sala\" em cada aula.", aulas, sala);
        } else {
            snprintf(detalhe, sizeof(detalhe), "Ela deixa de aparecer como livre até você clicar em \"Liberar sala\".");
        }
        if (!confirmar(ui, titulo, detalhe, "Colocar em manutenção")) return;
    } else {
        snprintf(titulo, sizeof(titulo), "Liberar a sala %s?", sala);
        if (!confirmar(ui, titulo, "Ela volta a aparecer como livre nos horários sem aula.", "Liberar sala")) return;
    }

    if (!definir_manutencao_sala(ARQ_SALAS, sala, novo_estado)) {
        mostrar_mensagem(ui, GTK_MESSAGE_ERROR, "Não foi possível mudar a sala.",
                         "O programa não conseguiu gravar o arquivo " ARQ_SALAS ". Nada foi alterado.");
        return;
    }
    recarregar_salas(ui);
    atualizar_telas(ui);

    char aviso[100];
    snprintf(aviso, sizeof(aviso), novo_estado ? "Sala %s em manutenção." : "Sala %s liberada.", sala);
    oferecer_desfazer(aviso);
}

// Coloca os botões "Renomear", "Excluir" e "Manutenção" na parte de baixo do card
static void adicionar_botoes_card(GtkWidget *card, const char *sala, int em_manutencao, InterfaceApp *ui) {
    GtkWidget *linha = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_margin_top(linha, 6);

    GtkWidget *btn_renomear = gtk_button_new_with_label("Renomear");
    GtkWidget *btn_excluir = gtk_button_new_with_label("Excluir");
    adicionar_classe(btn_renomear, "card-action");
    adicionar_classe(btn_excluir, "card-action");
    adicionar_classe(btn_excluir, "danger");
    gtk_widget_set_tooltip_text(btn_renomear, "Mudar o nome desta sala (as aulas dela acompanham)");
    gtk_widget_set_tooltip_text(btn_excluir, "Tirar esta sala do sistema");

    // Cada botão guarda o nome da sala; g_free apaga a cópia quando o botão for destruído
    g_object_set_data_full(G_OBJECT(btn_renomear), "sala", g_strdup(sala), g_free);
    g_object_set_data_full(G_OBJECT(btn_excluir), "sala", g_strdup(sala), g_free);
    g_signal_connect(btn_renomear, "clicked", G_CALLBACK(ao_clicar_renomear_sala), ui);
    g_signal_connect(btn_excluir, "clicked", G_CALLBACK(ao_clicar_excluir_sala), ui);

    gtk_box_pack_start(GTK_BOX(linha), btn_renomear, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(linha), btn_excluir, FALSE, FALSE, 0);
    
    // Botão de manutenção: o texto muda conforme o estado da sala
    GtkWidget *btn_manut = gtk_button_new_with_label(em_manutencao ? "Liberar sala" : "Manutenção");
    adicionar_classe(btn_manut, "card-action");
    gtk_widget_set_tooltip_text(btn_manut, em_manutencao ? "A sala voltou a funcionar: ela volta a aparecer como livre"
                                                         : "Sala em reforma ou quebrada: ela deixa de aparecer como livre");
    g_object_set_data_full(G_OBJECT(btn_manut), "sala", g_strdup(sala), g_free);
    g_object_set_data(G_OBJECT(btn_manut), "manutencao", GINT_TO_POINTER(em_manutencao));
    g_signal_connect(btn_manut, "clicked", G_CALLBACK(ao_clicar_manutencao_sala), ui);
    gtk_box_pack_start(GTK_BOX(linha), btn_manut, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(card), linha, FALSE, FALSE, 0);
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
        gboolean card_livre = FALSE;
        if (todas[s].status_manutencao == 1) {
            card = criar_card_sala(todas[s].id_sala, "Em manutenção", FALSE);
            adicionar_classe(card, "maintenance");
            ocupadas++;
        } else if (ocupante) {
            char desc[200];
            snprintf(desc, sizeof(desc), "%s\nTurma %s", ocupante->disciplina, ocupante->turma);
            card = criar_card_sala(todas[s].id_sala, desc, FALSE);
            ocupadas++;
        } else {
            card = criar_card_sala(todas[s].id_sala, "Disponível", TRUE);
            card_livre = TRUE;
            livres++;
        }
        adicionar_botoes_card(card, todas[s].id_sala, todas[s].status_manutencao, ui);
        GtkWidget *evento = gtk_event_box_new();
        gtk_event_box_set_visible_window(GTK_EVENT_BOX(evento), FALSE);
        gtk_container_add(GTK_CONTAINER(evento), card);
        g_object_set_data_full(G_OBJECT(evento), "sala", g_strdup(todas[s].id_sala), g_free);
        g_object_set_data(G_OBJECT(evento), "livre", GINT_TO_POINTER(card_livre));
        g_signal_connect(evento, "button-press-event", G_CALLBACK(ao_clicar_card), ui);
        gtk_widget_set_tooltip_text(evento, card_livre ? "Clique para cadastrar uma aula nesta sala ou trazer uma aula para ela"
                                                       : "Clique para ver todas as aulas desta sala no dia");
        gtk_flow_box_insert(GTK_FLOW_BOX(ui->painel_flow), evento, -1);
    }

    if (n == 0) {
        GtkWidget *vazio = gtk_label_new("Nenhuma sala ainda. Vá em \"Planilha: Turma – Horário\" e clique em "
                                         "\"Importar (CSV ou PDF)\" para carregar a grade do semestre.");
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

static void ir_para_notificacoes(InterfaceApp *ui) {
    ir_para_pagina(ui, PAG_AVISOS);
}

static GtkWidget* criar_pagina_painel_salas(InterfaceApp *ui) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 25);
    criar_cabecalho(vbox, "Painel de salas", "Escolha o dia e o horário. Clique numa sala verde para usá-la, ou numa azul para ver as aulas dela no dia.");

    // Faixa amarela que aparece quando há turmas sem sala ou choques de horário
    ui->painel_aviso = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    adicionar_classe(ui->painel_aviso, "faixa-aviso");
    ui->painel_aviso_lbl = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(ui->painel_aviso_lbl), 0.0);
    GtkWidget *b_ver = gtk_button_new_with_label("Ver notificações");
    adicionar_classe(b_ver, "pill-btn");
    g_signal_connect_swapped(b_ver, "clicked", G_CALLBACK(ir_para_notificacoes), ui);
    gtk_box_pack_start(GTK_BOX(ui->painel_aviso), ui->painel_aviso_lbl, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(ui->painel_aviso), b_ver, FALSE, FALSE, 0);
    gtk_widget_set_no_show_all(ui->painel_aviso, TRUE); // só aparece quando tem pendência
    gtk_widget_show(ui->painel_aviso_lbl);
    gtk_widget_show(b_ver);
    gtk_box_pack_start(GTK_BOX(vbox), ui->painel_aviso, FALSE, FALSE, 0);

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
            COL_TURMA, a->turma, COL_DOCENTE, a->professores, COL_HORARIO, horario,
            COL_SALA, a->id_sala[0] ? a->id_sala : "SEM SALA", -1);
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
// dia/ini/fim/sala vêm preenchidos quando a aula é criada a partir de uma sala livre (ou NULL)
static void abrir_nova_aula(InterfaceApp *ui, int dia, const char *ini, const char *fim, const char *sala) {
    preparar_desfazer(ui);

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
    gtk_combo_box_set_active(GTK_COMBO_BOX(c_dia), dia);
    GtkWidget *c_ini = gtk_combo_box_text_new_with_entry();
    preencher_combo(c_ini, HORARIOS, NUM_HORARIOS - 1);
    gtk_combo_box_set_active(GTK_COMBO_BOX(c_ini), 0);
    GtkWidget *c_fim = gtk_combo_box_text_new_with_entry();
    preencher_combo(c_fim, HORARIOS + 1, NUM_HORARIOS - 1);
    gtk_combo_box_set_active(GTK_COMBO_BOX(c_fim), 0);
    GtkWidget *c_sala = gtk_combo_box_text_new_with_entry();
    preencher_combo_salas(ui, c_sala);
    gtk_entry_set_placeholder_text(GTK_ENTRY(entrada_do_combo(c_sala)), "Ex.: A-03");
    if (ini) gtk_entry_set_text(GTK_ENTRY(entrada_do_combo(c_ini)), ini);
    if (fim) gtk_entry_set_text(GTK_ENTRY(entrada_do_combo(c_fim)), fim);
    if (sala) gtk_entry_set_text(GTK_ENTRY(entrada_do_combo(c_sala)), sala);

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

        char aviso[300];
        snprintf(aviso, sizeof(aviso), "Aula cadastrada: %s (turma %s) na sala %s.", nova.disciplina, nova.turma, nova.id_sala);
        oferecer_desfazer(aviso);
        break;
    }
    gtk_widget_destroy(dialog);
}

static void ao_clicar_nova_aula(GtkWidget *btn, gpointer data) {
    (void)btn;
    InterfaceApp *ui = data;
    abrir_nova_aula(ui, ui->grade_dia, NULL, NULL, NULL);
}

// ---------- Trocar sala ----------
static void trocar_sala_da_aula(InterfaceApp *ui, int idx) {
    AlocacaoTurma aula = ui->alocacoes[idx];
    preparar_desfazer(ui);

    GtkWidget *dialog = gtk_dialog_new_with_buttons("Trocar sala", GTK_WINDOW(ui->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "Cancelar", GTK_RESPONSE_CANCEL, "Trocar sala", GTK_RESPONSE_ACCEPT, NULL);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(box), 16);
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), box);

    char info[500];
    snprintf(info, sizeof(info), "%s (turma %s)\n%s, %s às %s\nSala atual: %s",
             aula.disciplina, aula.turma, dia_extenso(aula.dia_semana), aula.hora_inicio, aula.hora_fim,
             aula.id_sala[0] ? aula.id_sala : "(sem sala)");
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

        char aviso[300];
        if (aula.id_sala[0]) snprintf(aviso, sizeof(aviso), "%s (turma %s) mudou da sala %s para a %s.", aula.disciplina, aula.turma, aula.id_sala, temp.id_sala);
        else snprintf(aviso, sizeof(aviso), "%s (turma %s) agora tem sala: %s.", aula.disciplina, aula.turma, temp.id_sala);
        oferecer_desfazer(aviso);
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

    preparar_desfazer(ui);
    int r = cancelar_alocacao(ARQ_ALOCACOES, a->id_alocacao, ui->alocacoes, ui->qtd_alocacoes);
    if (r == 0) erro_ao_salvar(ui);
    atualizar_telas(ui);

    if (r == 1) {
        char aviso[300];
        snprintf(aviso, sizeof(aviso), "Aula cancelada: %s (turma %s). A sala %s ficou livre.", a->disciplina, a->turma, a->id_sala);
        oferecer_desfazer(aviso);
    }
}

// ---------- Usar uma sala livre (Painel e Salas livres) ----------

// Lista as aulas do dia que acontecem no intervalo e que PODEM ir para a sala (sem choque)
static int aulas_que_cabem_na_sala(InterfaceApp *ui, const char *sala, int dia, const char *ini, const char *fim, int *indices, int max) {
    int n = 0;
    for (int i = 0; i < ui->qtd_alocacoes && n < max; i++) {
        AlocacaoTurma *a = &ui->alocacoes[i];
        if (!a->ativo || strcmp(a->dia_semana, DIAS_SIGLA[dia]) != 0 || strcmp(a->id_sala, sala) == 0) continue;
        if (!(strcmp(a->hora_fim, ini) > 0 && strcmp(a->hora_inicio, fim) < 0)) continue; // fora do intervalo
        AlocacaoTurma teste = *a;
        copiar_texto(teste.id_sala, sizeof(teste.id_sala), sala);
        if (buscar_conflito(teste, ui->alocacoes, ui->qtd_alocacoes) >= 0) continue; // não cabe inteira
        indices[n++] = i;
    }
    return n;
}

// Janela com uma lista de aulas para escolher; devolve a posição escolhida (ou -1)
static int escolher_aula_da_lista(InterfaceApp *ui, const char *titulo, const char *explicacao, int *indices, int n, const char *texto_ok) {
    GtkWidget *d = gtk_dialog_new_with_buttons(titulo, GTK_WINDOW(ui->window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                               "Voltar", GTK_RESPONSE_CANCEL, texto_ok, GTK_RESPONSE_OK, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_OK);
    GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(d));
    gtk_container_set_border_width(GTK_CONTAINER(area), 15);
    gtk_box_set_spacing(GTK_BOX(area), 10);

    GtkWidget *lbl = gtk_label_new(explicacao);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(lbl), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(lbl), 70);
    gtk_box_pack_start(GTK_BOX(area), lbl, FALSE, FALSE, 0);

    GtkWidget *combo = gtk_combo_box_text_new();
    for (int k = 0; k < n; k++) {
        AlocacaoTurma *a = &ui->alocacoes[indices[k]];
        char item[300];
        snprintf(item, sizeof(item), "%s – %s   %s (turma %s)   sala %s", a->hora_inicio, a->hora_fim, a->disciplina, a->turma, a->id_sala);
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), item);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
    gtk_box_pack_start(GTK_BOX(area), combo, FALSE, FALSE, 0);
    gtk_widget_show_all(d);

    int r = gtk_dialog_run(GTK_DIALOG(d));
    int escolha = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
    gtk_widget_destroy(d);
    if (r != GTK_RESPONSE_OK || escolha < 0) return -1;
    return indices[escolha];
}

static void mover_aula_para_sala(InterfaceApp *ui, const char *sala, int dia, const char *ini, const char *fim) {
    int indices[MAX_ALOCACOES];
    int n = aulas_que_cabem_na_sala(ui, sala, dia, ini, fim, indices, MAX_ALOCACOES);
    if (n == 0) {
        char det[300];
        snprintf(det, sizeof(det), "Nenhuma aula da %s entre %s e %s pode ir para a sala %s sem bater com outra aula dela.",
                 DIAS_EXTENSO[dia], ini, fim, sala);
        mostrar_mensagem(ui, GTK_MESSAGE_INFO, "Nenhuma aula para trazer.", det);
        return;
    }

    char explicacao[300];
    snprintf(explicacao, sizeof(explicacao), "Escolha a aula que vai para a sala %s (%s). "
             "A lista mostra só as aulas desse horário que cabem nela sem choque.", sala, DIAS_EXTENSO[dia]);
    int idx = escolher_aula_da_lista(ui, "Trazer aula para esta sala", explicacao, indices, n, "Trazer para esta sala");
    if (idx < 0) return;

    preparar_desfazer(ui);
    AlocacaoTurma aula = ui->alocacoes[idx];
    int r = atualizar_sala_alocacao(ARQ_ALOCACOES, aula.id_alocacao, sala, ui->alocacoes, ui->qtd_alocacoes);
    if (r == -1) { mostrar_mensagem(ui, GTK_MESSAGE_WARNING, "A sala ficou ocupada nesse meio tempo.", NULL); return; }
    if (r != 1) { erro_ao_salvar(ui); return; }
    atualizar_telas(ui);

    char aviso[300];
    snprintf(aviso, sizeof(aviso), "%s (turma %s) mudou da sala %s para a %s.", aula.disciplina, aula.turma, aula.id_sala, sala);
    oferecer_desfazer(aviso);
}

static void usar_sala_livre(InterfaceApp *ui, const char *sala, int dia, const char *ini, const char *fim) {
    char titulo[120], det[300];
    snprintf(titulo, sizeof(titulo), "Sala %s livre", sala);
    snprintf(det, sizeof(det), "A sala %s está livre na %s das %s às %s.\nO que você quer fazer?", sala, DIAS_EXTENSO[dia], ini, fim);

    enum { VER = 1, MOVER = 2, NOVA = 3 };
    GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(ui->window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                          GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "%s", titulo);
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d), "%s", det);
    gtk_dialog_add_buttons(GTK_DIALOG(d), "Voltar", GTK_RESPONSE_CANCEL, "Ver aulas do dia", VER,
                           "Trazer aula para cá", MOVER, "Nova aula aqui", NOVA, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(d), NOVA);
    int r = gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);

    if (r == NOVA) abrir_nova_aula(ui, dia, ini, fim, sala);
    else if (r == MOVER) mover_aula_para_sala(ui, sala, dia, ini, fim);
    else if (r == VER) mostrar_programacao_sala(ui, sala, dia);
}

// ---------- Inverter salas (troca as salas de duas aulas entre si) ----------
static void ao_clicar_inverter_salas(GtkWidget *btn, gpointer data) {
    (void)btn;
    InterfaceApp *ui = data;
    int ia = exigir_selecao(ui);
    if (ia < 0) return;
    AlocacaoTurma a = ui->alocacoes[ia];
    if (a.id_sala[0] == '\0') {
        mostrar_mensagem(ui, GTK_MESSAGE_INFO, "Esta aula ainda não tem sala.", "Use \"Trocar sala\" para escolher uma sala para ela.");
        return;
    }

    // Candidatas: aulas do mesmo dia, em outra sala, com horário que se cruza com o da aula escolhida
    int indices[MAX_ALOCACOES], n = 0;
    for (int i = 0; i < ui->qtd_alocacoes; i++) {
        AlocacaoTurma *b = &ui->alocacoes[i];
        if (i == ia || !b->ativo || b->id_sala[0] == '\0' || strcmp(b->dia_semana, a.dia_semana) != 0 || strcmp(b->id_sala, a.id_sala) == 0) continue;
        if (strcmp(b->hora_fim, a.hora_inicio) > 0 && strcmp(b->hora_inicio, a.hora_fim) < 0) indices[n++] = i;
    }
    if (n == 0) {
        mostrar_mensagem(ui, GTK_MESSAGE_INFO, "Nenhuma aula para inverter.",
                         "Não há outra aula em outra sala nesse mesmo dia e horário.");
        return;
    }

    char explicacao[400];
    snprintf(explicacao, sizeof(explicacao), "%s (turma %s) está na sala %s, %s das %s às %s.\n"
             "Escolha a aula que vai trocar de sala com ela: cada uma fica com a sala da outra.",
             a.disciplina, a.turma, a.id_sala, DIAS_EXTENSO[indice_dia(a.dia_semana)], a.hora_inicio, a.hora_fim);
    int ib = escolher_aula_da_lista(ui, "Inverter salas", explicacao, indices, n, "Inverter salas");
    if (ib < 0) return;
    AlocacaoTurma b = ui->alocacoes[ib];

    preparar_desfazer(ui);
    int r = inverter_salas(ARQ_ALOCACOES, a.id_alocacao, b.id_alocacao, ui->alocacoes, ui->qtd_alocacoes);
    if (r == -1) {
        mostrar_mensagem(ui, GTK_MESSAGE_WARNING, "Não dá para inverter essas duas aulas.",
                         "Os horários não são iguais e, depois da troca, uma delas bateria com outra aula na sala nova. "
                         "Escolha outra aula ou use \"Trocar sala\".");
        return;
    }
    if (r != 1) { erro_ao_salvar(ui); return; }
    atualizar_telas(ui);

    char aviso[400];
    snprintf(aviso, sizeof(aviso), "Salas invertidas: %s (%s) foi para a %s e %s (%s) foi para a %s.",
             a.disciplina, a.turma, b.id_sala, b.disciplina, b.turma, a.id_sala);
    oferecer_desfazer(aviso);
}

// ---------- Exportar planilhas (CSV) ----------

// Pergunta onde salvar; devolve o caminho escolhido (g_free depois) ou NULL
static char *escolher_onde_salvar(InterfaceApp *ui, const char *titulo, const char *nome_sugerido) {
    GtkWidget *ch = gtk_file_chooser_dialog_new(titulo, GTK_WINDOW(ui->window), GTK_FILE_CHOOSER_ACTION_SAVE,
                                                "Cancelar", GTK_RESPONSE_CANCEL, "Salvar", GTK_RESPONSE_ACCEPT, NULL);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(ch), TRUE);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(ch), nome_sugerido);
    const char *pasta = g_get_user_special_dir(G_USER_DIRECTORY_DESKTOP);
    if (pasta) gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(ch), pasta);

    char *caminho = NULL;
    if (gtk_dialog_run(GTK_DIALOG(ch)) == GTK_RESPONSE_ACCEPT) caminho = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(ch));
    gtk_widget_destroy(ch);

    if (caminho && !g_str_has_suffix(caminho, ".csv") && !g_str_has_suffix(caminho, ".CSV")) {
        char *com_extensao = g_strconcat(caminho, ".csv", NULL);
        g_free(caminho);
        caminho = com_extensao;
    }
    return caminho;
}

static void avisar_planilha_salva(InterfaceApp *ui, const char *caminho, gboolean ok) {
    if (!ok) {
        mostrar_mensagem(ui, GTK_MESSAGE_ERROR, "Não foi possível salvar a planilha.",
                         "Confira se o arquivo não está aberto no Excel e se a pasta permite gravar.");
        return;
    }
    char *nome = g_path_get_basename(caminho);
    char aviso[300];
    snprintf(aviso, sizeof(aviso), "Planilha salva: %s (abre no Excel e no Google Planilhas).", nome);
    mostrar_balao(aviso, FALSE);
    registrar_no_historico(aviso);
    g_free(nome);
}

static void ao_clicar_exportar_grade(GtkWidget *btn, gpointer data) {
    (void)btn;
    InterfaceApp *ui = data;
    char *destino = escolher_onde_salvar(ui, "Salvar planilha Turma – Horário", "Turma-Horario-2026.2.csv");
    if (!destino) return;

    // Mesmo truque da importação: o backend grava num caminho simples e a GLib copia
    // para o destino (que pode ter acento, ex.: "Área de Trabalho" no Windows)
    const char *temporario = "data/exportacao_temp.csv";
    gboolean ok = exportar_grade_csv(temporario, ui->alocacoes, ui->qtd_alocacoes) == 1;
    if (ok) {
        char *conteudo = NULL;
        gsize tamanho = 0;
        ok = g_file_get_contents(temporario, &conteudo, &tamanho, NULL) &&
             g_file_set_contents(destino, conteudo, (gssize)tamanho, NULL);
        g_free(conteudo);
    }
    g_remove(temporario);
    avisar_planilha_salva(ui, destino, ok);
    g_free(destino);
}

// Junta um campo no CSV, com aspas se precisar
static void csv_campo(GString *s, const char *texto, gboolean ultimo) {
    if (strpbrk(texto, ";\"\n")) {
        g_string_append_c(s, '"');
        for (const char *p = texto; *p; p++) {
            if (*p == '"') g_string_append_c(s, '"');
            g_string_append_c(s, *p);
        }
        g_string_append_c(s, '"');
    } else {
        g_string_append(s, texto);
    }
    g_string_append(s, ultimo ? "\r\n" : ";");
}

static void ao_clicar_exportar_livres(GtkWidget *btn, gpointer data) {
    (void)btn;
    InterfaceApp *ui = data;
    GtkTreeModel *model = GTK_TREE_MODEL(ui->livres_store);
    GtkTreeIter iter;
    if (!gtk_tree_model_get_iter_first(model, &iter)) {
        mostrar_mensagem(ui, GTK_MESSAGE_INFO, "A lista está vazia.", "Escolha um dia e um horário com salas livres antes de exportar.");
        return;
    }
    char *destino = escolher_onde_salvar(ui, "Salvar lista de salas livres", "Salas-Livres-2026.2.csv");
    if (!destino) return;

    GString *csv = g_string_new("\xEF\xBB\xBF"); // BOM: faz o Excel reconhecer os acentos
    g_string_append(csv, "DIA;HORÁRIO;SALA\r\n");
    do {
        char *dia, *horario, *sala, *cap;
        gtk_tree_model_get(model, &iter, LIV_DIA, &dia, LIV_HORARIO, &horario, LIV_SALA, &sala, LIV_CAP, &cap, -1);
        csv_campo(csv, dia, FALSE);
        csv_campo(csv, horario, FALSE);
        csv_campo(csv, sala, TRUE);
        g_free(dia); g_free(horario); g_free(sala); g_free(cap);
    } while (gtk_tree_model_iter_next(model, &iter));

    gboolean ok = g_file_set_contents(destino, csv->str, (gssize)csv->len, NULL);
    g_string_free(csv, TRUE);
    avisar_planilha_salva(ui, destino, ok);
    g_free(destino);
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
    preparar_desfazer(ui);

    GtkWidget *chooser = gtk_file_chooser_dialog_new("Importar planilha (CSV) ou horário em PDF", GTK_WINDOW(ui->window),
        GTK_FILE_CHOOSER_ACTION_OPEN, "Cancelar", GTK_RESPONSE_CANCEL, "Importar", GTK_RESPONSE_ACCEPT, NULL);
    GtkFileFilter *filtro = gtk_file_filter_new();
    gtk_file_filter_set_name(filtro, "Planilha CSV ou PDF (*.csv, *.pdf)");
    gtk_file_filter_add_pattern(filtro, "*.csv");
    gtk_file_filter_add_pattern(filtro, "*.CSV");
    gtk_file_filter_add_pattern(filtro, "*.pdf");
    gtk_file_filter_add_pattern(filtro, "*.PDF");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), filtro);

    char *arquivo = NULL;
    if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
        arquivo = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
    }
    gtk_widget_destroy(chooser);
    if (arquivo == NULL) return;

    // PDF: primeiro converte para CSV (mesmo nome, ao lado do PDF) e depois importa esse CSV normalmente
    if (g_str_has_suffix(arquivo, ".pdf") || g_str_has_suffix(arquivo, ".PDF")) {
        char *csv = g_strdup(arquivo);
        strcpy(csv + strlen(csv) - 4, ".csv");
        char erro[400];
        int convertidas = 0;
        gboolean ok_pdf = converter_pdf_para_csv(arquivo, csv, &convertidas, erro, sizeof(erro));
        g_free(arquivo);
        if (!ok_pdf) {
            mostrar_mensagem(ui, GTK_MESSAGE_ERROR, "Não foi possível converter o PDF.", erro);
            g_free(csv);
            return;
        }
        char det[700];
        snprintf(det, sizeof(det), "%d aula(s) encontradas no PDF. Também foi criada uma planilha com elas:\n%s\n\n"
                 "Ela abre no Excel ou no Google Planilhas. Agora o sistema vai importar as aulas.", convertidas, csv);
        mostrar_mensagem(ui, GTK_MESSAGE_INFO, "PDF convertido em planilha (CSV).", det);
        arquivo = csv;
    }

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
    int sem_sala = 0;
    for (int i = 0; i < ui->qtd_alocacoes; i++) if (ui->alocacoes[i].ativo && ui->alocacoes[i].id_sala[0] == '\0') sem_sala++;
    if (sem_sala > 0) g_string_append_printf(rel, "\n\n%d turma(s) vieram sem sala definida. Elas aparecem em \"Notificações\" para você escolher a sala.", sem_sala);

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
    oferecer_desfazer("Planilha importada. Se foi o arquivo errado, clique em Desfazer.");
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
                    "Todas as aulas do dia escolhido. Selecione uma linha para trocar a sala, inverter com outra aula ou cancelar "
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
    gtk_box_pack_start(GTK_BOX(acoes), criar_botao_acao("Inverter salas", "Trocar a sala da aula selecionada com a de outra aula no mesmo horário", G_CALLBACK(ao_clicar_inverter_salas), ui), FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(acoes), criar_botao_acao("Exportar planilha (CSV)", "Salvar a grade inteira numa planilha que abre no Excel e no Google Planilhas", G_CALLBACK(ao_clicar_exportar_grade), ui), FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(acoes), criar_botao_acao("Importar (CSV ou PDF)", "Carregar a grade a partir da planilha salva em CSV ou do horário em PDF", G_CALLBACK(ao_clicar_importar), ui), FALSE, FALSE, 0);
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

#define TEXTO_TODOS_DIAS "Todos os dias"
#define TEXTO_TODOS_HORARIOS "Todos os horários"

// Coloca na tabela as salas livres de um dia num intervalo; devolve quantas achou
static int listar_livres_no_intervalo(InterfaceApp *ui, int dia, const char *ini, const char *fim,
                                      SalaFisica *todas, int n_todas) {
    SalaFisica livres[MAX_SALAS];
    int n_livres = 0;
    buscar_salas_livres(DIAS_SIGLA[dia], ini, fim, livres, &n_livres, ui->alocacoes, ui->qtd_alocacoes, todas, n_todas);

    char horario[20];
    snprintf(horario, sizeof(horario), "%s – %s", ini, fim);
    for (int i = 0; i < n_livres; i++) {
        char cap[20];
        if (livres[i].capacidade > 0) snprintf(cap, sizeof(cap), "%d lugares", livres[i].capacidade);
        else snprintf(cap, sizeof(cap), "—");
        gtk_list_store_insert_with_values(ui->livres_store, NULL, -1,
            LIV_DIA, DIAS_NOME[dia], LIV_HORARIO, horario, LIV_SALA, livres[i].id_sala, LIV_CAP, cap,
            LIV_DIA_IDX, dia, LIV_INI, ini, LIV_FIM, fim, -1);
    }
    return n_livres;
}

static void atualizar_salas_livres(InterfaceApp *ui) {
    gtk_list_store_clear(ui->livres_store);

    // Dia: posição 0 = "Todos os dias", 1..6 = segunda..sábado
    int escolha_dia = gtk_combo_box_get_active(GTK_COMBO_BOX(ui->livres_combo_dia));
    gboolean todos_dias = escolha_dia <= 0;
    int dia_ini = todos_dias ? 0 : escolha_dia - 1;
    int dia_fim = todos_dias ? NUM_DIAS - 1 : escolha_dia - 1;

    // Horário: "Todos os horários" percorre os blocos de aula da planilha
    const char *texto_ini = gtk_entry_get_text(GTK_ENTRY(entrada_do_combo(ui->livres_combo_ini)));
    gboolean todos_horarios = strcmp(texto_ini, TEXTO_TODOS_HORARIOS) == 0;
    gtk_widget_set_sensitive(ui->livres_combo_fim, !todos_horarios);

    char ini[6], fim[6];
    if (!todos_horarios) {
        if (!normalizar_hora(texto_ini, ini) ||
            !normalizar_hora(gtk_entry_get_text(GTK_ENTRY(entrada_do_combo(ui->livres_combo_fim))), fim)) {
            gtk_label_set_text(GTK_LABEL(ui->livres_status), "Escreva os horários no formato HH:MM (ex.: 07:10) ou escolha \"" TEXTO_TODOS_HORARIOS "\".");
            return;
        }
        if (strcmp(ini, fim) >= 0) {
            gtk_label_set_text(GTK_LABEL(ui->livres_status), "O horário de fim precisa ser depois do início.");
            return;
        }
    }

    SalaFisica todas[MAX_SALAS];
    int n_todas = montar_lista_salas(ui, todas, MAX_SALAS);
    if (n_todas == 0) {
        gtk_label_set_text(GTK_LABEL(ui->livres_status), "Nenhuma sala cadastrada ainda. Importe a planilha primeiro.");
        return;
    }

    int total = 0;
    for (int d = dia_ini; d <= dia_fim; d++) {
        if (todos_horarios) {
            for (int h = 0; h < NUM_HORARIOS - 1; h++) total += listar_livres_no_intervalo(ui, d, HORARIOS[h], HORARIOS[h + 1], todas, n_todas);
        } else {
            total += listar_livres_no_intervalo(ui, d, ini, fim, todas, n_todas);
        }
    }

    char status[220];
    if (!todos_dias && !todos_horarios) {
        snprintf(status, sizeof(status), "%d de %d salas livres na %s das %s às %s. Dois cliques numa sala para usá-la.",
                 total, n_todas, DIAS_EXTENSO[dia_ini], ini, fim);
    } else {
        snprintf(status, sizeof(status), "%d opção(ões) de sala livre (%s, %s). Clique no título de uma coluna para ordenar. "
                 "Dois cliques numa linha para usar a sala.",
                 total, todos_dias ? "todos os dias" : DIAS_EXTENSO[dia_ini],
                 todos_horarios ? "todos os horários" : "no horário escolhido");
    }
    gtk_label_set_text(GTK_LABEL(ui->livres_status), status);
}

static void ao_mudar_filtro_livres(GtkWidget *w, gpointer data) {
    (void)w;
    atualizar_salas_livres(data);
}

// Usa a sala da linha selecionada (botão ou dois cliques)
static void usar_linha_livre(InterfaceApp *ui) {
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(ui->livres_tree));
    GtkTreeModel *model;
    GtkTreeIter iter;
    if (!gtk_tree_selection_get_selected(sel, &model, &iter)) {
        mostrar_mensagem(ui, GTK_MESSAGE_INFO, "Nenhuma sala selecionada.", "Clique primeiro numa linha da tabela e depois no botão.");
        return;
    }
    char *sala, *ini, *fim;
    int dia;
    gtk_tree_model_get(model, &iter, LIV_SALA, &sala, LIV_DIA_IDX, &dia, LIV_INI, &ini, LIV_FIM, &fim, -1);
    usar_sala_livre(ui, sala, dia, ini, fim);
    g_free(sala);
    g_free(ini);
    g_free(fim);
}

static void ao_clicar_usar_livre(GtkWidget *btn, gpointer data) {
    (void)btn;
    usar_linha_livre(data);
}

static void ao_duplo_clique_livre(GtkTreeView *tv, GtkTreePath *p, GtkTreeViewColumn *c, gpointer data) {
    (void)tv; (void)p; (void)c;
    usar_linha_livre(data);
}

static GtkWidget* criar_pagina_salas_livres(InterfaceApp *ui) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 25);
    criar_cabecalho(vbox, "Salas livres", "Escolha o dia e o horário (ou \"Todos\") para ver as salas desocupadas. "
                    "Selecione uma sala para cadastrar uma aula nela ou trazer uma aula para ela.");

    GtkWidget *linha = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    ui->livres_combo_dia = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui->livres_combo_dia), TEXTO_TODOS_DIAS);
    preencher_combo(ui->livres_combo_dia, DIAS_NOME, NUM_DIAS);
    gtk_combo_box_set_active(GTK_COMBO_BOX(ui->livres_combo_dia), ui->painel_dia + 1);

    ui->livres_combo_ini = gtk_combo_box_text_new_with_entry();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui->livres_combo_ini), TEXTO_TODOS_HORARIOS);
    preencher_combo(ui->livres_combo_ini, HORARIOS, NUM_HORARIOS - 1);
    ui->livres_combo_fim = gtk_combo_box_text_new_with_entry();
    preencher_combo(ui->livres_combo_fim, HORARIOS + 1, NUM_HORARIOS - 1);
    int bloco = gtk_combo_box_get_active(GTK_COMBO_BOX(ui->painel_combo_hora));
    gtk_combo_box_set_active(GTK_COMBO_BOX(ui->livres_combo_ini), bloco + 1); // +1 por causa do "Todos"
    gtk_combo_box_set_active(GTK_COMBO_BOX(ui->livres_combo_fim), bloco);

    gtk_box_pack_start(GTK_BOX(linha), gtk_label_new("Dia:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(linha), ui->livres_combo_dia, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(linha), gtk_label_new("Das"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(linha), ui->livres_combo_ini, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(linha), gtk_label_new("às"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(linha), ui->livres_combo_fim, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(linha), criar_botao_acao("Exportar lista (CSV)", "Salvar esta lista de salas livres numa planilha",
                                                      G_CALLBACK(ao_clicar_exportar_livres), ui), FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(linha), criar_botao_acao("Usar sala selecionada", "Cadastrar uma aula nesta sala ou trazer uma aula para ela",
                                                      G_CALLBACK(ao_clicar_usar_livre), ui), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), linha, FALSE, FALSE, 0);

    g_signal_connect(ui->livres_combo_dia, "changed", G_CALLBACK(ao_mudar_filtro_livres), ui);
    g_signal_connect(ui->livres_combo_ini, "changed", G_CALLBACK(ao_mudar_filtro_livres), ui);
    g_signal_connect(ui->livres_combo_fim, "changed", G_CALLBACK(ao_mudar_filtro_livres), ui);

    ui->livres_status = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(ui->livres_status), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(ui->livres_status), TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), ui->livres_status, FALSE, FALSE, 0);

    ui->livres_store = gtk_list_store_new(LIV_NUM_COLS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                          G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING);
    ui->livres_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(ui->livres_store));
    g_object_unref(ui->livres_store);
    gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(ui->livres_tree), GTK_TREE_VIEW_GRID_LINES_HORIZONTAL);
    g_signal_connect(ui->livres_tree, "row-activated", G_CALLBACK(ao_duplo_clique_livre), ui);

    const char *titulos[] = {"Dia", "Horário", "Sala"};
    for (int c = LIV_DIA; c <= LIV_SALA; c++) {
        GtkCellRenderer *r = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes(titulos[c], r, "text", c, NULL);
        gtk_tree_view_column_set_sort_column_id(col, c == LIV_DIA ? LIV_DIA_IDX : c);
        gtk_tree_view_column_set_resizable(col, TRUE);
        if (c == LIV_SALA) g_object_set(r, "weight", PANGO_WEIGHT_BOLD, NULL);
        gtk_tree_view_append_column(GTK_TREE_VIEW(ui->livres_tree), col);
    }

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), ui->livres_tree);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);
    return vbox;
}

// =======================================================
// PÁGINA 4: NOTIFICAÇÕES (pendências + histórico de mudanças)
// =======================================================

static char historico_linhas[200][TAM_LINHA_HISTORICO];

// Conta pendências: turmas sem sala e choques de horário (mesma sala, mesmo dia, horários que se cruzam)
static void contar_pendencias(InterfaceApp *ui, int *sem_sala, int *choques) {
    *sem_sala = 0;
    *choques = 0;
    for (int i = 0; i < ui->qtd_alocacoes; i++) {
        AlocacaoTurma *a = &ui->alocacoes[i];
        if (!a->ativo) continue;
        if (a->id_sala[0] == '\0') { (*sem_sala)++; continue; }
        if (buscar_conflito(*a, ui->alocacoes, i) >= 0) (*choques)++; // compara só com as anteriores: conta cada par uma vez
    }
}

static void ao_resolver_pendencia(GtkWidget *btn, gpointer data) {
    InterfaceApp *ui = data;
    int id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "id_aula"));
    for (int i = 0; i < ui->qtd_alocacoes; i++) {
        if (ui->alocacoes[i].id_alocacao == id) { trocar_sala_da_aula(ui, i); return; }
    }
}

// Uma linha da lista: título em negrito, explicação embaixo e (opcional) botão à direita
static void adicionar_linha_aviso(GtkWidget *lista, const char *titulo, const char *texto, const char *botao, int id_aula, InterfaceApp *ui) {
    GtkWidget *linha = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(linha), 8);
    GtkWidget *textos = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

    GtkWidget *l_titulo = gtk_label_new(NULL);
    char *marcado = g_markup_printf_escaped("<b>%s</b>", titulo);
    gtk_label_set_markup(GTK_LABEL(l_titulo), marcado);
    g_free(marcado);
    gtk_label_set_xalign(GTK_LABEL(l_titulo), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(l_titulo), TRUE);
    gtk_box_pack_start(GTK_BOX(textos), l_titulo, FALSE, FALSE, 0);

    if (texto) {
        GtkWidget *l_texto = gtk_label_new(texto);
        gtk_label_set_xalign(GTK_LABEL(l_texto), 0.0);
        gtk_label_set_line_wrap(GTK_LABEL(l_texto), TRUE);
        adicionar_classe(l_texto, "page-subtitle");
        gtk_box_pack_start(GTK_BOX(textos), l_texto, FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(linha), textos, TRUE, TRUE, 0);

    if (botao) {
        GtkWidget *b = gtk_button_new_with_label(botao);
        adicionar_classe(b, "pill-btn");
        gtk_widget_set_valign(b, GTK_ALIGN_CENTER);
        g_object_set_data(G_OBJECT(b), "id_aula", GINT_TO_POINTER(id_aula));
        g_signal_connect(b, "clicked", G_CALLBACK(ao_resolver_pendencia), ui);
        gtk_box_pack_end(GTK_BOX(linha), b, FALSE, FALSE, 0);
    }
    gtk_container_add(GTK_CONTAINER(lista), linha);
}

static void limpar_lista(GtkWidget *lista) {
    GList *filhos = gtk_container_get_children(GTK_CONTAINER(lista));
    for (GList *l = filhos; l; l = l->next) gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(filhos);
}

static void atualizar_notificacoes(InterfaceApp *ui) {
    int sem_sala, choques;
    contar_pendencias(ui, &sem_sala, &choques);
    int total = sem_sala + choques;

    // Contador na barra lateral
    char rotulo[60];
    if (total > 0) snprintf(rotulo, sizeof(rotulo), "🔔  Notificações (%d)", total);
    else snprintf(rotulo, sizeof(rotulo), "🔔  Notificações");
    gtk_button_set_label(GTK_BUTTON(ui->nav_btns[PAG_AVISOS]), rotulo);
    gtk_widget_set_halign(gtk_bin_get_child(GTK_BIN(ui->nav_btns[PAG_AVISOS])), GTK_ALIGN_START); // o GTK recria o texto: alinha de novo

    // Faixa de aviso no topo do Painel
    if (total > 0) {
        char texto[200];
        snprintf(texto, sizeof(texto), "⚠  %d turma(s) sem sala e %d choque(s) de horário precisam de atenção.", sem_sala, choques);
        gtk_label_set_text(GTK_LABEL(ui->painel_aviso_lbl), texto);
        gtk_widget_show(ui->painel_aviso);
    } else {
        gtk_widget_hide(ui->painel_aviso);
    }

    // Lista de pendências
    limpar_lista(ui->avisos_pendencias);
    for (int i = 0; i < ui->qtd_alocacoes; i++) {
        AlocacaoTurma *a = &ui->alocacoes[i];
        if (!a->ativo) continue;
        char titulo[300], texto[400];
        if (a->id_sala[0] == '\0') {
            snprintf(titulo, sizeof(titulo), "Turma sem sala: %s (turma %s)", a->disciplina, a->turma);
            snprintf(texto, sizeof(texto), "%s, %s às %s. Docente: %s", DIAS_EXTENSO[indice_dia(a->dia_semana) < 0 ? 0 : indice_dia(a->dia_semana)],
                     a->hora_inicio, a->hora_fim, a->professores);
            adicionar_linha_aviso(ui->avisos_pendencias, titulo, texto, "Escolher sala", a->id_alocacao, ui);
            continue;
        }
        int k = buscar_conflito(*a, ui->alocacoes, i);
        if (k >= 0) {
            AlocacaoTurma *b = &ui->alocacoes[k];
            snprintf(titulo, sizeof(titulo), "Choque de horário na sala %s (%s)", a->id_sala, dia_extenso(a->dia_semana));
            snprintf(texto, sizeof(texto), "%s (turma %s) %s–%s  ×  %s (turma %s) %s–%s",
                     b->disciplina, b->turma, b->hora_inicio, b->hora_fim, a->disciplina, a->turma, a->hora_inicio, a->hora_fim);
            adicionar_linha_aviso(ui->avisos_pendencias, titulo, texto, "Trocar sala", a->id_alocacao, ui);
        }
    }
    if (total == 0) {
        adicionar_linha_aviso(ui->avisos_pendencias, "✓  Tudo certo por aqui.",
                              "Nenhuma turma sem sala e nenhum choque de horário.", NULL, 0, ui);
    }

    // Histórico (mais recente primeiro)
    limpar_lista(ui->avisos_historico);
    int n = ler_historico(ARQ_HISTORICO, historico_linhas, 200);
    for (int i = 0; i < n; i++) {
        char *sep = strstr(historico_linhas[i], " | ");
        if (sep) {
            *sep = '\0';
            adicionar_linha_aviso(ui->avisos_historico, sep + 3, historico_linhas[i], NULL, 0, ui);
        } else {
            adicionar_linha_aviso(ui->avisos_historico, historico_linhas[i], NULL, NULL, 0, ui);
        }
    }
    if (n == 0) adicionar_linha_aviso(ui->avisos_historico, "Nenhuma mudança registrada ainda.",
                                      "Cada aula cadastrada, trocada ou cancelada aparece aqui com data e hora.", NULL, 0, ui);

    gtk_widget_show_all(ui->avisos_pendencias);
    gtk_widget_show_all(ui->avisos_historico);
}

static GtkWidget *criar_secao(GtkWidget *vbox, const char *titulo, GtkWidget **lista) {
    GtkWidget *l = gtk_label_new(titulo);
    adicionar_classe(l, "section-title");
    gtk_label_set_xalign(GTK_LABEL(l), 0.0);
    gtk_box_pack_start(GTK_BOX(vbox), l, FALSE, FALSE, 0);
    *lista = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(*lista), GTK_SELECTION_NONE);
    adicionar_classe(*lista, "aviso-lista");
    gtk_box_pack_start(GTK_BOX(vbox), *lista, FALSE, FALSE, 0);
    return *lista;
}

static GtkWidget* criar_pagina_notificacoes(InterfaceApp *ui) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 25);
    criar_cabecalho(vbox, "Notificações", "O que precisa de atenção na grade e tudo o que foi mudado no sistema, com data e hora.");

    GtkWidget *conteudo = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    criar_secao(conteudo, "Pendências", &ui->avisos_pendencias);
    criar_secao(conteudo, "Histórico de mudanças", &ui->avisos_historico);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), conteudo);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);
    return vbox;
}

// =======================================================
// PÁGINA 5: AJUDA (vídeo, resetar)
// =======================================================

#define ARQ_VIDEO_TUTORIAL "ajuda/tutorial.mp4"


static void ao_clicar_video(GtkWidget *btn, gpointer data) {
    (void)btn;
    InterfaceApp *ui = data;
    if (!g_file_test(ARQ_VIDEO_TUTORIAL, G_FILE_TEST_EXISTS)) {
        mostrar_mensagem(ui, GTK_MESSAGE_WARNING, "O vídeo não foi encontrado.",
                         "O arquivo " ARQ_VIDEO_TUTORIAL " precisa estar na pasta do programa.");
        return;
    }
    char *pasta = g_get_current_dir();
    char *completo = g_build_filename(pasta, ARQ_VIDEO_TUTORIAL, NULL);
    char *uri = g_filename_to_uri(completo, NULL, NULL);
    GError *erro = NULL;
    // Abre no player padrão do computador (Filmes e TV, VLC, etc.)
    if (!uri || !gtk_show_uri_on_window(GTK_WINDOW(ui->window), uri, GDK_CURRENT_TIME, &erro)) {
        mostrar_mensagem(ui, GTK_MESSAGE_WARNING, "Não foi possível abrir o vídeo.",
                         erro ? erro->message : "Abra o arquivo ajuda/tutorial.mp4 pela pasta do programa.");
        if (erro) g_error_free(erro);
    }
    g_free(uri);
    g_free(completo);
    g_free(pasta);
}

// Copia os .dat para data/backup-AAAAMMDD-HHMMSS antes de apagar (segurança extra além do Desfazer)
static char *fazer_backup(void) {
    GDateTime *agora = g_date_time_new_now_local();
    char *nome = g_date_time_format(agora, "data/backup-%Y%m%d-%H%M%S");
    g_date_time_unref(agora);
    g_mkdir_with_parents(nome, 0755);
    const char *arquivos[] = {ARQ_ALOCACOES, ARQ_SALAS, ARQ_HISTORICO, NULL};
    for (int i = 0; arquivos[i]; i++) {
        char *conteudo = NULL;
        gsize tam = 0;
        if (g_file_get_contents(arquivos[i], &conteudo, &tam, NULL)) {
            char *base = g_path_get_basename(arquivos[i]);
            char *destino = g_build_filename(nome, base, NULL);
            g_file_set_contents(destino, conteudo, (gssize)tam, NULL);
            g_free(destino);
            g_free(base);
            g_free(conteudo);
        }
    }
    return nome;
}

static void ao_clicar_resetar(GtkWidget *btn, gpointer data) {
    (void)btn;
    InterfaceApp *ui = data;

    GtkWidget *d = gtk_dialog_new_with_buttons("Resetar sistema", GTK_WINDOW(ui->window),
                                               GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                               "Voltar", GTK_RESPONSE_CANCEL, "Apagar tudo", GTK_RESPONSE_OK, NULL);
    GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(d));
    gtk_container_set_border_width(GTK_CONTAINER(area), 15);
    gtk_box_set_spacing(GTK_BOX(area), 8);
    int ativas = 0;
    for (int i = 0; i < ui->qtd_alocacoes; i++) if (ui->alocacoes[i].ativo) ativas++;
    char texto[400];
    snprintf(texto, sizeof(texto), "Isso apaga TODAS as %d aulas e TODAS as salas do sistema (por exemplo, para começar um semestre novo).\n"
             "Uma cópia de segurança é salva na pasta data/ antes.\n\nPara confirmar, digite APAGAR abaixo:", ativas);
    GtkWidget *lbl = gtk_label_new(texto);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(lbl), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(lbl), 60);
    GtkWidget *entrada = gtk_entry_new();
    gtk_box_pack_start(GTK_BOX(area), lbl, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(area), entrada, FALSE, FALSE, 0);
    gtk_widget_show_all(d);

    int r = gtk_dialog_run(GTK_DIALOG(d));
    char *digitado = g_utf8_strup(gtk_entry_get_text(GTK_ENTRY(entrada)), -1);
    g_strstrip(digitado);
    gboolean confirmou = r == GTK_RESPONSE_OK && strcmp(digitado, "APAGAR") == 0;
    g_free(digitado);
    gtk_widget_destroy(d);
    if (r == GTK_RESPONSE_OK && !confirmou) {
        mostrar_mensagem(ui, GTK_MESSAGE_INFO, "Nada foi apagado.", "Para resetar, é preciso digitar a palavra APAGAR.");
        return;
    }
    if (!confirmou) return;

    preparar_desfazer(ui);
    char *pasta_backup = fazer_backup();
    if (!salvar_alocacoes(ARQ_ALOCACOES, ui->alocacoes, 0) || !gravar_todas_salas(ARQ_SALAS, ui->salas, 0)) {
        carregar_dados(ui);
        atualizar_telas(ui);
        erro_ao_salvar(ui);
        g_free(pasta_backup);
        return;
    }
    ui->qtd_alocacoes = 0;
    recarregar_salas(ui);
    atualizar_telas(ui);

    char aviso[300];
    snprintf(aviso, sizeof(aviso), "Sistema zerado: todas as aulas e salas foram apagadas (cópia em %s).", pasta_backup);
    oferecer_desfazer(aviso);
    g_free(pasta_backup);
}

static GtkWidget *criar_cartao_ajuda(const char *titulo, const char *texto, GtkWidget *botao) {
    GtkWidget *caixa = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    adicionar_classe(caixa, "ajuda-card");
    GtkWidget *textos = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *l1 = gtk_label_new(titulo);
    adicionar_classe(l1, "section-title");
    gtk_label_set_xalign(GTK_LABEL(l1), 0.0);
    GtkWidget *l2 = gtk_label_new(texto);
    gtk_label_set_xalign(GTK_LABEL(l2), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(l2), TRUE);
    gtk_box_pack_start(GTK_BOX(textos), l1, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(textos), l2, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(caixa), textos, TRUE, TRUE, 0);
    if (botao) {
        gtk_widget_set_valign(botao, GTK_ALIGN_CENTER);
        gtk_box_pack_end(GTK_BOX(caixa), botao, FALSE, FALSE, 0);
    }
    return caixa;
}

static GtkWidget* criar_pagina_ajuda(InterfaceApp *ui) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 25);
    criar_cabecalho(vbox, "Ajuda", "Como usar o sistema e ferramentas de manutenção.");

    GtkWidget *b_video = criar_botao_acao("▶  Assistir ao tutorial", "Abre o vídeo no player do computador", G_CALLBACK(ao_clicar_video), ui);
    adicionar_classe(b_video, "primary");
    gtk_box_pack_start(GTK_BOX(vbox), criar_cartao_ajuda("Vídeo tutorial",
        "Um vídeo curto mostrando o uso do dia a dia: importar a planilha, ver salas livres, cadastrar e trocar aulas, desfazer e exportar.",
        b_video), FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), criar_cartao_ajuda("Passo a passo rápido",
        "1. Em \"Planilha: Turma – Horário\", clique em Importar e escolha o CSV (ou o PDF) do semestre.\n"
        "2. No \"Painel de salas\", escolha o dia e o horário para ver o que está livre (verde) e ocupado (azul).\n"
        "3. Clique numa sala verde para cadastrar uma aula nela ou trazer uma aula para ela.\n"
        "4. Em \"Salas livres\", use \"Todos\" para ver qualquer dia ou horário de uma vez.\n"
        "5. Errou? Clique em \"Desfazer\" no aviso que aparece embaixo da tela.\n"
        "6. Em \"Notificações\" ficam as turmas sem sala, os choques de horário e o histórico de tudo o que mudou.",
        NULL), FALSE, FALSE, 0);

    GtkWidget *b_reset = criar_botao_acao("Resetar sistema", "Apaga todas as aulas e salas (pede confirmação)", G_CALLBACK(ao_clicar_resetar), ui);
    adicionar_classe(b_reset, "danger");
    GtkWidget *perigo = criar_cartao_ajuda("Resetar sistema",
        "Apaga todas as aulas e todas as salas, por exemplo para começar um semestre novo. "
        "Antes de apagar, o sistema guarda uma cópia de segurança na pasta data/.", b_reset);
    adicionar_classe(perigo, "perigo");
    gtk_box_pack_start(GTK_BOX(vbox), perigo, FALSE, FALSE, 0);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), vbox);
    return scroll;
}

// =======================================================
// JANELA PRINCIPAL
// =======================================================

void atualizar_telas(InterfaceApp *ui) {
    atualizar_painel(ui);
    atualizar_grade(ui);
    atualizar_salas_livres(ui);
    atualizar_notificacoes(ui);
}

static const char *NOMES_PAGINAS[NUM_PAGINAS] = {"painel", "grade", "livres", "avisos", "ajuda"};

static void ir_para_pagina(InterfaceApp *ui, int pagina) {
    for (int i = 0; i < NUM_PAGINAS; i++) {
        GtkStyleContext *ctx = gtk_widget_get_style_context(ui->nav_btns[i]);
        if (i == pagina) gtk_style_context_add_class(ctx, "active");
        else gtk_style_context_remove_class(ctx, "active");
    }
    gtk_stack_set_visible_child_name(GTK_STACK(ui->stack), NOMES_PAGINAS[pagina]);
}

static void ao_navegar(GtkWidget *btn, gpointer data) {
    ir_para_pagina(data, GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "pagina")));
}

void criar_interface_principal(GtkApplication *app, InterfaceApp *ui) {
    carregar_css();

    ui->window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(ui->window), "Controle de Salas - POLI-UPE");
    gtk_window_set_default_size(GTK_WINDOW(ui->window), 1200, 720);
    gtk_window_set_icon_from_file(GTK_WINDOW(ui->window), "icone.png", NULL); // ícone da janela e da barra de tarefas

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
                                            "🏛  Planilha: Salas Livres", "🔔  Notificações", "❓  Ajuda"};
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
    gtk_stack_add_named(GTK_STACK(ui->stack), criar_pagina_notificacoes(ui), "avisos");
    gtk_stack_add_named(GTK_STACK(ui->stack), criar_pagina_ajuda(ui), "ajuda");

    // O "overlay" deixa o balão flutuar por cima das telas
    GtkWidget *overlay = gtk_overlay_new();
    gtk_container_add(GTK_CONTAINER(overlay), ui->stack);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), criar_balao(ui));
    gtk_box_pack_start(GTK_BOX(main_box), overlay, TRUE, TRUE, 0);

    atualizar_telas(ui);
    gtk_widget_show_all(ui->window);
}
