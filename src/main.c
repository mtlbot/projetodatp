#include <gtk/gtk.h>
#include "../include/interface.h"

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    /* Garante que a pasta de dados exista (ex: na primeira execucao) */
    g_mkdir_with_parents("data", 0755);

    criar_janela_principal();
    gtk_main();
    return 0;
}
