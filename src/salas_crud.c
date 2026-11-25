#include <stdio.h>
#include <string.h>
#include "../include/backend.h"

int cadastrar_nova_sala(const char *caminho_arquivo, SalaFisica nova_sala) {
    // Preven��o de erro humano: n�o cadastrar sala sem nome
    if (strlen(nova_sala.id_sala) == 0) return 0;

    // Prevenção de duplicidade: não cadastra a mesma sala duas vezes
    FILE *existente = fopen(caminho_arquivo, "rb");
    if (existente != NULL) {
        SalaFisica s;
        while (fread(&s, sizeof(SalaFisica), 1, existente) == 1) {
            if (strcmp(s.id_sala, nova_sala.id_sala) == 0) {
                fclose(existente);
                return -1; // Sala já cadastrada
            }
        }
        fclose(existente);
    }

    FILE *arquivo = fopen(caminho_arquivo, "ab");
    if (arquivo == NULL) return 0; // Preven��o: falha de disco/permiss�o

    size_t escrito = fwrite(&nova_sala, sizeof(SalaFisica), 1, arquivo);
    fclose(arquivo);

    return (escrito == 1) ? 1 : 0;
}

int listar_todas_salas(const char *caminho_arquivo, SalaFisica *lista, int *qtd, int limite_maximo) {
    FILE *arquivo = fopen(caminho_arquivo, "rb");
    *qtd = 0;
    if (arquivo == NULL) return 0; // Retorna 0 lidos se o arquivo ainda n�o existir

    while(fread(&lista[*qtd], sizeof(SalaFisica), 1, arquivo) == 1) {
        (*qtd)++;
        // Preven��o de m�quina: impede que a RAM estoure se o arquivo for adulterado
        if (*qtd >= limite_maximo) break;
    }

    fclose(arquivo);
    return 1;
}

int buscar_salas_livres(const char *dia_semana, const char *hora_inicio, const char *hora_fim,
                        SalaFisica *salas_disponiveis, int *qtd_livres,
                        AlocacaoTurma *lista, int qtd_total_alocacoes,
                        SalaFisica *todas_salas, int total_salas) {
    *qtd_livres = 0;

    for (int i = 0; i < total_salas; i++) {
        int sala_ocupada = 0;

        // Sala em reforma/inutilizada nunca aparece como livre
        if (todas_salas[i].status_manutencao == 1) continue;

        // Compara a sala atual do loop com todas as turmas carregadas na mem�ria
        for (int j = 0; j < qtd_total_alocacoes; j++) {
            if (lista[j].ativo == 1 &&
                strcmp(lista[j].id_sala, todas_salas[i].id_sala) == 0 &&
                strcmp(lista[j].dia_semana, dia_semana) == 0) {

                // Checagem de sobreposi��o de hor�rio (l�gica de string HH:MM)
                if (strcmp(lista[j].hora_fim, hora_inicio) > 0 && strcmp(lista[j].hora_inicio, hora_fim) < 0) {
                    sala_ocupada = 1;
                    break; // J� sabemos que choca hor�rio, passa para a pr�xima sala
                }
            }
        }

        if (!sala_ocupada) {
            salas_disponiveis[*qtd_livres] = todas_salas[i];
            (*qtd_livres)++;
        }
    }
    return 1;
}

// Grava o vetor inteiro de salas no arquivo (substitui o conteudo antigo).
// Usada por excluir_sala e renomear_sala. Retorna 1 (sucesso) ou 0 (erro de disco).
int gravar_todas_salas(const char *caminho_arquivo, SalaFisica *lista, int qtd) {
    FILE *arquivo = fopen(caminho_arquivo, "wb");
    if (arquivo == NULL) return 0;

    size_t escrito = fwrite(lista, sizeof(SalaFisica), qtd, arquivo);
    fclose(arquivo);
    return (escrito == (size_t)qtd) ? 1 : 0;
}

int excluir_sala(const char *caminho_arquivo, const char *id_sala) {
    SalaFisica lista[MAX_SALAS];
    int qtd = 0;
    listar_todas_salas(caminho_arquivo, lista, &qtd, MAX_SALAS);

    // Copia para frente todas as salas, menos a que vai ser excluida
    int nova_qtd = 0, achou = 0;
    for (int i = 0; i < qtd; i++) {
        if (strcmp(lista[i].id_sala, id_sala) == 0) {
            achou = 1;
            continue;
        }
        lista[nova_qtd] = lista[i];
        nova_qtd++;
    }
    if (!achou) return -2; // A sala nao estava no arquivo

    return gravar_todas_salas(caminho_arquivo, lista, nova_qtd);
}

int renomear_sala(const char *caminho_arquivo, const char *id_antigo, const char *id_novo) {
    if (strlen(id_novo) == 0) return 0; // Nome vazio nao pode

    SalaFisica lista[MAX_SALAS];
    int qtd = 0;
    listar_todas_salas(caminho_arquivo, lista, &qtd, MAX_SALAS);

    int posicao = -1;
    for (int i = 0; i < qtd; i++) {
        if (strcmp(lista[i].id_sala, id_novo) == 0) return -1; // Ja existe sala com o novo nome
        if (strcmp(lista[i].id_sala, id_antigo) == 0) posicao = i;
    }

    // A sala so existia na grade (nunca foi gravada em salas.dat): cadastra com o novo nome
    if (posicao == -1) {
        SalaFisica nova;
        memset(&nova, 0, sizeof(nova));
        strncpy(nova.id_sala, id_novo, sizeof(nova.id_sala) - 1);
        return cadastrar_nova_sala(caminho_arquivo, nova);
    }

    // Troca so o nome; capacidade e status de manutencao continuam iguais
    memset(lista[posicao].id_sala, 0, sizeof(lista[posicao].id_sala));
    strncpy(lista[posicao].id_sala, id_novo, sizeof(lista[posicao].id_sala) - 1);
    return gravar_todas_salas(caminho_arquivo, lista, qtd);
}

int definir_manutencao_sala(const char *caminho_arquivo, const char *id_sala, int em_manutencao) {
    if (strlen(id_sala) == 0) return 0;

    SalaFisica lista[MAX_SALAS];
    int qtd = 0;
    listar_todas_salas(caminho_arquivo, lista, &qtd, MAX_SALAS);

    for (int i = 0; i < qtd; i++) {
        if (strcmp(lista[i].id_sala, id_sala) == 0) {
            lista[i].status_manutencao = em_manutencao ? 1 : 0;
            return gravar_todas_salas(caminho_arquivo, lista, qtd);
        }
    }

    // A sala so existia na grade (nunca foi gravada em salas.dat): cadastra ja com o status
    SalaFisica nova;
    memset(&nova, 0, sizeof(nova));
    strncpy(nova.id_sala, id_sala, sizeof(nova.id_sala) - 1);
    nova.status_manutencao = em_manutencao ? 1 : 0;
    return cadastrar_nova_sala(caminho_arquivo, nova) == 1 ? 1 : 0;
}