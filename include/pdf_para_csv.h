#ifndef PDF_PARA_CSV_H
#define PDF_PARA_CSV_H

// Converte o PDF de horários (tabela com DEPTO, COMPONENTE, TURMA, DOCENTE, DIA/HORÁRIO, SALA)
// num arquivo CSV com as mesmas colunas da planilha, lendo o PDF com a biblioteca Poppler-GLib (em C).
// O CSV gerado passa depois pela importação normal (com a tela de revisão de problemas).
//
// Retorna 1 em caso de sucesso. Em caso de erro retorna 0 e escreve em "erro" uma mensagem
// em português para mostrar ao funcionário. "linhas_convertidas" recebe quantas aulas foram encontradas.
int converter_pdf_para_csv(const char *caminho_pdf, const char *caminho_csv,
                           int *linhas_convertidas, char *erro, int tam_erro);

#endif
