#include <gtk/gtk.h>
#include "interface.h"

static InterfaceApp app_ui;

static void activate(GtkApplication *app, gpointer user_data) {
    criar_interface_principal(app, &app_ui);
}

int main(int argc, char **argv) {
    GtkApplication *app = gtk_application_new("br.upe.poli.datp", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}