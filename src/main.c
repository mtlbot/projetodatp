#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include "interface.h"

// GLib < 2.74 (ex.: Ubuntu 22.04 / Zorin 17) não tem G_APPLICATION_DEFAULT_FLAGS
#if !GLIB_CHECK_VERSION(2, 74, 0)
#define G_APPLICATION_DEFAULT_FLAGS G_APPLICATION_FLAGS_NONE
#endif

static InterfaceApp app_ui;

// Garante que style.css e a pasta data/ sejam encontrados mesmo quando o
// programa é aberto com dois cliques dentro de bin/ (Windows) ou de outra pasta.
static void ir_para_pasta_do_projeto(const char *argv0) {
    if (g_file_test("style.css", G_FILE_TEST_EXISTS)) return;

    char *caminho = g_find_program_in_path(argv0);
    if (caminho == NULL) caminho = g_strdup(argv0);
    char *pasta_bin = g_path_get_dirname(caminho);
    char *candidatos[] = { g_strdup(pasta_bin), g_build_filename(pasta_bin, "..", NULL) };

    for (int i = 0; i < 2; i++) {
        char *css = g_build_filename(candidatos[i], "style.css", NULL);
        int achou = g_file_test(css, G_FILE_TEST_EXISTS);
        g_free(css);
        if (achou) {
            g_chdir(candidatos[i]);
            break;
        }
    }
    g_free(candidatos[0]);
    g_free(candidatos[1]);
    g_free(pasta_bin);
    g_free(caminho);
}

static void activate(GtkApplication *app, gpointer user_data) {
    (void)user_data;
    carregar_dados(&app_ui);           // backend: carregar_alocacoes + listar_todas_salas
    criar_interface_principal(app, &app_ui);
}

int main(int argc, char **argv) {
    ir_para_pasta_do_projeto(argv[0]);
    g_mkdir_with_parents("data", 0755);

    GtkApplication *app = gtk_application_new("br.upe.poli.datp", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
