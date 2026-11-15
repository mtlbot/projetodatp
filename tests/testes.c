/*
 * Bateria de testes do backend (modo console).
 * Usa arquivos proprios (data/teste_*) para nao mexer nos dados reais.
 * No Code::Blocks, escolha o alvo "Testes" para compilar e rodar.
 */
#include <stdio.h>
#include <string.h>
#include "../include/backend.h"

#define T_SALAS  "data/teste_salas.dat"
#define T_ALOC   "data/teste_alocacoes.dat"
#define T_CSV    "data/teste_grade.csv"

/* static: vetores grandes ficam fora da pilha (2000 alocacoes ~ 700 KB) */
static SalaFisica    salas[MAX_SALAS];
static AlocacaoTurma alocacoes[MAX_ALOCACOES];
static SalaFisica    livres[MAX_SALAS];
static AlocacaoTurma filtradas[MAX_ALOCACOES];

static int total_testes = 0, falhas = 0;

static void checar(int condicao, const char *descricao) {
    total_testes++;
    if (condicao) {
        printf("  [OK]    %s\n", descricao);
    } else {
        printf("  [FALHA] %s\n", descricao);
        falhas++;
    }
}

static SalaFisica sala(const char *id, int cap, int manut) {
    SalaFisica s;
    memset(&s, 0, sizeof(s));
    copiar_texto(s.id_sala, id, TAM_ID_SALA);
    s.capacidade = cap;
    s.status_manutencao = manut;
    return s;
}

static AlocacaoTurma reserva(const char *disc, const char *dia, const char *ini, const char *fim, const char *id_sala) {
    AlocacaoTurma a;
    memset(&a, 0, sizeof(a));
    copiar_texto(a.departamento, "DSC", TAM_DEPARTAMENTO);
    copiar_texto(a.disciplina, disc, TAM_DISCIPLINA);
    copiar_texto(a.turma, "1A", TAM_TURMA);
    copiar_texto(a.professores, "FULANO", TAM_PROFESSORES);
    copiar_texto(a.dia_semana, dia, TAM_DIA);
    copiar_texto(a.hora_inicio, ini, TAM_HORA);
    copiar_texto(a.hora_fim, fim, TAM_HORA);
    copiar_texto(a.id_sala, id_sala, TAM_ID_SALA);
    return a;
}

int main(void) {
    int qs, qa, ql, id, r;

    remove(T_SALAS);
    remove(T_ALOC);
    remove(T_CSV);

    printf("== Utilitarios ==\n");
    {
        char h[TAM_HORA], d[TAM_DIA];
        checar(normalizar_hora("7:10", h) && strcmp(h, "07:10") == 0, "\"7:10\" vira \"07:10\"");
        checar(!normalizar_hora("25:00", h), "rejeita hora 25:00");
        checar(!normalizar_hora("abc", h), "rejeita hora sem numeros");
        checar(normalizar_dia(" ter ", d) && strcmp(d, "TER") == 0, "\" ter \" vira \"TER\"");
        checar(!normalizar_dia("XYZ", d), "rejeita dia invalido");
        checar(!validar_id_sala("   "), "rejeita nome de sala so com espacos");
        checar(!validar_id_sala("A;B"), "rejeita nome de sala com ';'");
    }

    printf("\n== Salas ==\n");
    listar_todas_salas(T_SALAS, salas, &qs, MAX_SALAS);
    checar(qs == 0, "arquivo inexistente = lista vazia");
    checar(cadastrar_nova_sala(T_SALAS, sala("I-06", 40, 0)) == RES_OK, "cadastra I-06");
    checar(cadastrar_nova_sala(T_SALAS, sala("i-06", 30, 0)) == RES_DUPLICADO, "bloqueia duplicada (i-06)");
    checar(cadastrar_nova_sala(T_SALAS, sala("", 30, 0)) == RES_INVALIDO, "bloqueia sala sem nome");
    checar(cadastrar_nova_sala(T_SALAS, sala("A-01", 0, 0)) == RES_INVALIDO, "bloqueia capacidade 0");
    checar(cadastrar_nova_sala(T_SALAS, sala("A-03", 60, 0)) == RES_OK, "cadastra A-03");
    checar(cadastrar_nova_sala(T_SALAS, sala("LAB. MECT", 20, 1)) == RES_OK, "cadastra LAB. MECT (em manutencao)");
    listar_todas_salas(T_SALAS, salas, &qs, MAX_SALAS);
    checar(qs == 3, "lista 3 salas");

    printf("\n== Reservas ==\n");
    carregar_alocacoes(T_ALOC, alocacoes, &qa, MAX_ALOCACOES);
    checar(qa == 0, "arquivo inexistente = lista vazia");

    r = adicionar_alocacao(reserva("CALCULO", "seg", "7:10", "8:50", "i-06"),
                           alocacoes, &qa, MAX_ALOCACOES, T_ALOC, salas, qs, &id);
    checar(r == RES_OK && id == 1, "reserva I-06 SEG 07:10-08:50 (id 1)");
    checar(strcmp(alocacoes[0].id_sala, "I-06") == 0 && strcmp(alocacoes[0].hora_inicio, "07:10") == 0,
           "dados normalizados ao salvar");

    r = adicionar_alocacao(reserva("FISICA", "SEG", "08:00", "09:40", "I-06"),
                           alocacoes, &qa, MAX_ALOCACOES, T_ALOC, salas, qs, NULL);
    checar(r == RES_CONFLITO, "bloqueia horario sobreposto na mesma sala");

    r = adicionar_alocacao(reserva("FISICA", "SEG", "08:50", "10:30", "I-06"),
                           alocacoes, &qa, MAX_ALOCACOES, T_ALOC, salas, qs, &id);
    checar(r == RES_OK && id == 2, "permite horario colado (08:50 em diante)");

    r = adicionar_alocacao(reserva("QUIMICA", "SEG", "07:10", "08:50", "A-03"),
                           alocacoes, &qa, MAX_ALOCACOES, T_ALOC, salas, qs, &id);
    checar(r == RES_OK && id == 3, "mesmo horario em outra sala e permitido");

    r = adicionar_alocacao(reserva("X", "TER", "10:00", "09:00", "A-03"),
                           alocacoes, &qa, MAX_ALOCACOES, T_ALOC, salas, qs, NULL);
    checar(r == RES_INVALIDO, "bloqueia inicio depois do fim");

    r = adicionar_alocacao(reserva("X", "TER", "10:00", "11:00", "LAB. MECT"),
                           alocacoes, &qa, MAX_ALOCACOES, T_ALOC, salas, qs, NULL);
    checar(r == RES_SALA_MANUTENCAO, "bloqueia sala em manutencao");

    r = adicionar_alocacao(reserva("X", "TER", "10:00", "11:00", "Z-99"),
                           alocacoes, &qa, MAX_ALOCACOES, T_ALOC, salas, qs, NULL);
    checar(r == RES_NAO_ENCONTRADO, "bloqueia sala inexistente");

    printf("\n== Salas livres ==\n");
    buscar_salas_livres("SEG", "07:30", "08:00", 0, livres, &ql, alocacoes, qa, salas, qs);
    checar(ql == 0, "SEG 07:30-08:00: nenhuma livre (I-06 e A-03 ocupadas, LAB em manutencao)");
    buscar_salas_livres("TER", "07:30", "08:00", 0, livres, &ql, alocacoes, qa, salas, qs);
    checar(ql == 2, "TER 07:30-08:00: 2 livres");
    buscar_salas_livres("TER", "07:30", "08:00", 50, livres, &ql, alocacoes, qa, salas, qs);
    checar(ql == 1 && strcmp(livres[0].id_sala, "A-03") == 0, "TER com 50+ lugares: so A-03");

    printf("\n== Trocar sala / liberar ==\n");
    r = atualizar_sala_alocacao(T_ALOC, 2, "A-03", alocacoes, qa, salas, qs);
    checar(r == RES_OK, "move reserva 2 (SEG 08:50) para A-03");
    r = atualizar_sala_alocacao(T_ALOC, 1, "A-03", alocacoes, qa, salas, qs);
    checar(r == RES_CONFLITO, "bloqueia mover reserva 1 para A-03 (ocupada)");

    r = cancelar_alocacao(T_ALOC, 3, alocacoes, qa);
    checar(r == RES_OK, "cancela reserva 3 (libera A-03 SEG 07:10)");
    checar(cancelar_alocacao(T_ALOC, 3, alocacoes, qa) == RES_JA_CANCELADA, "cancelar de novo avisa");
    checar(cancelar_alocacao(T_ALOC, 99, alocacoes, qa) == RES_NAO_ENCONTRADO, "id inexistente avisa");
    r = atualizar_sala_alocacao(T_ALOC, 1, "A-03", alocacoes, qa, salas, qs);
    checar(r == RES_OK, "agora a reserva 1 pode ir para A-03");

    filtrar_alocacoes(alocacoes, qa, "seg", "", 1, filtradas, &ql, MAX_ALOCACOES);
    checar(ql == 2, "filtro: 2 reservas ativas na SEG");

    printf("\n== Persistencia ==\n");
    carregar_alocacoes(T_ALOC, alocacoes, &qa, MAX_ALOCACOES);
    checar(qa == 3 && alocacoes[2].ativo == 0 && strcmp(alocacoes[0].id_sala, "A-03") == 0,
           "dados relidos do disco batem com a memoria");

    printf("\n== Editar / remover sala ==\n");
    checar(remover_sala(T_SALAS, "A-03", alocacoes, qa) == RES_SALA_EM_USO, "nao remove sala com reserva ativa");
    checar(editar_sala(T_SALAS, "LAB. MECT", 25, 0) == RES_OK, "tira LAB. MECT da manutencao");
    checar(remover_sala(T_SALAS, "I-06", alocacoes, qa) == RES_OK, "remove I-06 (sem reservas ativas)");
    listar_todas_salas(T_SALAS, salas, &qs, MAX_SALAS);
    checar(qs == 2 && indice_sala(salas, qs, "I-06") == -1 && salas[indice_sala(salas, qs, "LAB. MECT")].capacidade == 25,
           "arquivo de salas atualizado");

    printf("\n== Importacao CSV ==\n");
    {
        FILE *f = fopen(T_CSV, "w");
        fprintf(f, "\xEF\xBB\xBF" "DEPARTAMENTO;DISCIPLINA;TURMA;PROFESSORES;DIA;INICIO;FIM;SALA\n");
        fprintf(f, "DSC;ESTRUTURA DE DADOS;2C;ANTONIO / STELA;TER;07:10;08:50;LAB. MECT\n"); /* ok */
        fprintf(f, "DSC;BANCO DE DADOS;3A;MARIA;ter;8:00;9:00;lab. mect\n");                /* conflito */
        fprintf(f, "\n");                                                                      /* em branco */
        fprintf(f, "DEE;CIRCUITOS;1B;JOAO;QUA;10:00;12:00\n");                                 /* falta campo */
        fprintf(f, "DEE;CIRCUITOS;1B;JOAO;QUA;10:00;12:00;A-03\n");                            /* ok */
        fclose(f);

        ResultadoImportacao res;
        r = importar_grade_csv(T_CSV, T_ALOC, alocacoes, &qa, MAX_ALOCACOES, salas, qs, &res);
        checar(r == RES_OK && res.importadas == 2 && res.rejeitadas == 2,
               "importa 2 linhas validas e rejeita 2");
        printf("          ultimo erro na linha %d: %s\n", res.linha_ultimo_erro, mensagem_erro(res.codigo_ultimo_erro));
        carregar_alocacoes(T_ALOC, alocacoes, &qa, MAX_ALOCACOES);
        checar(qa == 5 && alocacoes[4].id_alocacao == 5, "importadas salvas no disco com ids 4 e 5");
    }

    remove(T_SALAS);
    remove(T_ALOC);
    remove(T_CSV);

    printf("\n%d testes, %d falha(s).\n", total_testes, falhas);
    return falhas == 0 ? 0 : 1;
}
