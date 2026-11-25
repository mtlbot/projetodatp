#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>
#include "interface.h"
#include "backend.h"

// =======================================================
// TELA DE LOGIN
// =======================================================
// Pede o nome e a senha antes de abrir o sistema.
// - O nome fica guardado: "Entrou no sistema" vai para o histórico (aba Notificações)
//   e cada mudança feita depois leva o nome da pessoa.
// - A senha é uma só para todo o DATP (SENHA_SISTEMA em modelos.h) e serve só para entrar.

typedef struct {
    GtkApplication *app;
    InterfaceApp *ui;
    GtkWidget *janela;
    GtkWidget *entrada_nome;
    GtkWidget *entrada_senha;
    GtkWidget *erro;
} TelaLogin;

// Confere a senha. Ignora espaços nas pontas e maiúsculas/minúsculas
// (quem digitar "DATP360" com o Caps Lock ligado também entra).
static int senha_correta(const char *digitada) {
    char *limpa = g_strstrip(g_strdup(digitada));
    int ok = g_ascii_strcasecmp(limpa, SENHA_SISTEMA) == 0;
    g_free(limpa);
    return ok;
}

// Lê o nome do último login (data/ultimo_usuario.txt) para já vir preenchido
static void ler_ultimo_usuario(char *nome, int tamanho) {
    nome[0] = '\0';
    FILE *f = fopen(ARQ_ULTIMO_USUARIO, "rb");
    if (f == NULL) return;
    if (fgets(nome, tamanho, f) == NULL) nome[0] = '\0';
    nome[strcspn(nome, "\r\n")] = '\0';
    fclose(f);
}

static void salvar_ultimo_usuario(const char *nome) {
    FILE *f = fopen(ARQ_ULTIMO_USUARIO, "wb");
    if (f == NULL) return; // não é grave: só não vem preenchido na próxima vez
    fprintf(f, "%s\n", nome);
    fclose(f);
}

static void mostrar_erro(TelaLogin *t, const char *texto, GtkWidget *campo) {
    gtk_label_set_text(GTK_LABEL(t->erro), texto);
    gtk_widget_show(t->erro);
    gtk_widget_grab_focus(campo);
}

static void ao_entrar(GtkWidget *w, gpointer data) {
    (void)w;
    TelaLogin *t = data;

    char *nome = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(t->entrada_nome))));
    if (nome[0] == '\0') {
        mostrar_erro(t, "Digite o seu nome para entrar.", t->entrada_nome);
        g_free(nome);
        return;
    }
    if (!senha_correta(gtk_entry_get_text(GTK_ENTRY(t->entrada_senha)))) {
        mostrar_erro(t, "Senha incorreta. Confira e tente de novo.", t->entrada_senha);
        gtk_editable_select_region(GTK_EDITABLE(t->entrada_senha), 0, -1);
        g_free(nome);
        return;
    }

    // Deu certo: guarda quem entrou e registra no histórico
    definir_usuario(nome);
    salvar_ultimo_usuario(usuario_atual());
    registrar_historico(ARQ_HISTORICO, "Entrou no sistema");
    g_free(nome);

    InterfaceApp *ui = t->ui;
    GtkApplication *app = t->app;
    gtk_widget_destroy(t->janela); // libera "t" (ver ao_destruir_login)

    if (ui->window == NULL) {
        criar_interface_principal(app, ui); // primeira vez: monta a janela principal
    } else {
        mostrar_usuario_na_tela(ui);        // voltou do "Trocar de usuário"
        atualizar_telas(ui);
        gtk_widget_show(ui->window);
        gtk_window_present(GTK_WINDOW(ui->window));
    }
}

// Enter no campo do nome: pula para a senha
static void ao_enter_nome(GtkWidget *w, gpointer data) {
    (void)w;
    TelaLogin *t = data;
    gtk_widget_grab_focus(t->entrada_senha);
}

static void ao_mostrar_senha(GtkToggleButton *chk, gpointer data) {
    TelaLogin *t = data;
    gtk_entry_set_visibility(GTK_ENTRY(t->entrada_senha), gtk_toggle_button_get_active(chk));
}

// Começou a digitar de novo: a mensagem de erro some
static void ao_digitar(GtkEditable *e, gpointer data) {
    (void)e;
    TelaLogin *t = data;
    gtk_widget_hide(t->erro);
}

static void ao_destruir_login(GtkWidget *w, gpointer data) {
    (void)w;
    g_free(data);
}

// Fechou a tela de login no "X" sem entrar.
// Se a janela principal está escondida (veio do "Trocar de usuário"), fecha o programa todo.
static gboolean ao_fechar_login(GtkWidget *w, GdkEvent *ev, gpointer data) {
    (void)w; (void)ev;
    TelaLogin *t = data;
    if (t->ui->window != NULL && !gtk_widget_get_visible(t->ui->window)) {
        g_application_quit(G_APPLICATION(t->app));
    }
    return FALSE; // deixa a janela fechar
}

static GtkWidget *rotulo_campo(const char *texto) {
    GtkWidget *l = gtk_label_new(texto);
    gtk_label_set_xalign(GTK_LABEL(l), 0.0);
    gtk_style_context_add_class(gtk_widget_get_style_context(l), "login-rotulo");
    return l;
}

void mostrar_login(GtkApplication *app, InterfaceApp *ui) {
    TelaLogin *t = g_new0(TelaLogin, 1);
    t->app = app;
    t->ui = ui;

    t->janela = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(t->janela), "DATP 360 - Entrar");
    gtk_window_set_default_size(GTK_WINDOW(t->janela), 1200, 720);
    gtk_window_set_position(GTK_WINDOW(t->janela), GTK_WIN_POS_CENTER);
    gtk_window_set_icon_from_file(GTK_WINDOW(t->janela), "icone.png", NULL);
    gtk_style_context_add_class(gtk_widget_get_style_context(t->janela), "login-fundo");
    g_signal_connect(t->janela, "destroy", G_CALLBACK(ao_destruir_login), t);
    g_signal_connect(t->janela, "delete-event", G_CALLBACK(ao_fechar_login), t);

    // Cartão branco no meio da tela
    GtkWidget *cartao = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(cartao), "login-cartao");
    gtk_widget_set_halign(cartao, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(cartao, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(cartao, 440, -1);
    gtk_container_add(GTK_CONTAINER(t->janela), cartao);

    GdkPixbuf *logo = gdk_pixbuf_new_from_file_at_size("icone.png", 88, 88, NULL);
    if (logo) {
        GtkWidget *img = gtk_image_new_from_pixbuf(logo);
        g_object_unref(logo);
        gtk_box_pack_start(GTK_BOX(cartao), img, FALSE, FALSE, 0);
    }

    GtkWidget *titulo = gtk_label_new("DATP 360");
    gtk_style_context_add_class(gtk_widget_get_style_context(titulo), "login-titulo");
    gtk_box_pack_start(GTK_BOX(cartao), titulo, FALSE, FALSE, 0);

    GtkWidget *sub = gtk_label_new("Controle de salas e laboratórios da POLI-UPE");
    gtk_style_context_add_class(gtk_widget_get_style_context(sub), "login-sub");
    gtk_label_set_line_wrap(GTK_LABEL(sub), TRUE);
    gtk_label_set_justify(GTK_LABEL(sub), GTK_JUSTIFY_CENTER);
    gtk_box_pack_start(GTK_BOX(cartao), sub, FALSE, FALSE, 0);

    // Nome
    gtk_box_pack_start(GTK_BOX(cartao), rotulo_campo("Seu nome"), FALSE, FALSE, 0);
    t->entrada_nome = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(t->entrada_nome), MAX_LETRAS_NOME);
    gtk_entry_set_placeholder_text(GTK_ENTRY(t->entrada_nome), "Digite o seu nome");
    gtk_style_context_add_class(gtk_widget_get_style_context(t->entrada_nome), "login-campo");
    char ultimo[TAM_NOME_USUARIO];
    ler_ultimo_usuario(ultimo, sizeof(ultimo));
    gtk_entry_set_text(GTK_ENTRY(t->entrada_nome), ultimo);
    g_signal_connect(t->entrada_nome, "activate", G_CALLBACK(ao_enter_nome), t);
    g_signal_connect(t->entrada_nome, "changed", G_CALLBACK(ao_digitar), t);
    gtk_box_pack_start(GTK_BOX(cartao), t->entrada_nome, FALSE, FALSE, 0);

    // Senha
    gtk_box_pack_start(GTK_BOX(cartao), rotulo_campo("Senha"), FALSE, FALSE, 0);
    t->entrada_senha = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(t->entrada_senha), FALSE);
    gtk_entry_set_input_purpose(GTK_ENTRY(t->entrada_senha), GTK_INPUT_PURPOSE_PASSWORD);
    gtk_entry_set_placeholder_text(GTK_ENTRY(t->entrada_senha), "Senha do DATP");
    gtk_style_context_add_class(gtk_widget_get_style_context(t->entrada_senha), "login-campo");
    g_signal_connect(t->entrada_senha, "activate", G_CALLBACK(ao_entrar), t);
    g_signal_connect(t->entrada_senha, "changed", G_CALLBACK(ao_digitar), t);
    gtk_box_pack_start(GTK_BOX(cartao), t->entrada_senha, FALSE, FALSE, 0);

    GtkWidget *chk = gtk_check_button_new_with_label("Mostrar senha");
    gtk_style_context_add_class(gtk_widget_get_style_context(chk), "login-check");
    g_signal_connect(chk, "toggled", G_CALLBACK(ao_mostrar_senha), t);
    gtk_box_pack_start(GTK_BOX(cartao), chk, FALSE, FALSE, 0);

    // Mensagem de erro (vermelha, escondida até precisar)
    t->erro = gtk_label_new("");
    gtk_style_context_add_class(gtk_widget_get_style_context(t->erro), "login-erro");
    gtk_label_set_xalign(GTK_LABEL(t->erro), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(t->erro), TRUE);
    gtk_widget_set_no_show_all(t->erro, TRUE);
    gtk_box_pack_start(GTK_BOX(cartao), t->erro, FALSE, FALSE, 0);

    GtkWidget *btn = gtk_button_new_with_label("Entrar");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn), "pill-btn");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn), "primary");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn), "login-botao");
    g_signal_connect(btn, "clicked", G_CALLBACK(ao_entrar), t);
    gtk_box_pack_start(GTK_BOX(cartao), btn, FALSE, FALSE, 0);

    GtkWidget *rodape = gtk_label_new("Seu nome fica registrado no histórico de mudanças (aba Notificações).");
    gtk_style_context_add_class(gtk_widget_get_style_context(rodape), "login-rodape");
    gtk_label_set_line_wrap(GTK_LABEL(rodape), TRUE);
    gtk_label_set_justify(GTK_LABEL(rodape), GTK_JUSTIFY_CENTER);
    gtk_box_pack_start(GTK_BOX(cartao), rodape, FALSE, FALSE, 0);

    gtk_widget_show_all(t->janela);
    // Nome já preenchido (mesma pessoa de antes): o cursor vai direto para a senha
    gtk_widget_grab_focus(ultimo[0] != '\0' ? t->entrada_senha : t->entrada_nome);
}
