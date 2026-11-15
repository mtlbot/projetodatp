# DATP – Sistema de Gestão e Alocação de Salas (POLI-UPE)

Sistema local, escrito em C com interface GTK 3, para **reservar e liberar salas** da instituição.
Os dados ficam em arquivos binários na pasta `data/` (sem banco de dados).

## Funcionalidades

| Aba | O que faz |
|---|---|
| **Reservas** | Lista as reservas (filtro por dia e sala), cria nova reserva, troca a sala de uma reserva, **cancela a reserva (libera a sala)** e importa a grade de um CSV |
| **Salas livres** | Busca salas disponíveis num dia/horário, com capacidade mínima, e reserva direto pela lista |
| **Salas** | Cadastra, edita (capacidade / manutenção) e remove salas |

Regras garantidas pelo sistema:
- não deixa duas reservas na mesma sala com horário sobreposto;
- sala em manutenção não aparece como livre e não pode ser reservada;
- sala com reservas ativas não pode ser removida;
- não aceita salas com nome repetido, campos vazios, horário inválido ou início depois do fim.

## Estrutura

```
include/modelos.h     estruturas SalaFisica e AlocacaoTurma
include/backend.h     todas as funções do backend e códigos de retorno
include/interface.h
src/util.c            validações, cópia segura de texto, gravação segura de arquivo
src/salas_crud.c      cadastro / edição / remoção de salas, busca de salas livres
src/alocacao_crud.c   reservas: adicionar, trocar sala, cancelar, filtrar
src/csv_import.c      importação da grade em CSV
src/interface.c       janela GTK
src/main.c            ponto de entrada do programa GTK
tests/testes.c        bateria de testes do backend (não usa GTK)
data/                 salas.dat, alocacoes.dat (criados pelo programa) e inicial.csv
```

## Arquivos de dados

- `data/salas.dat` e `data/alocacoes.dat`: binários, criados automaticamente. **Não edite à mão.**
  Se mudar as structs em `modelos.h`, apague esses arquivos.
- `data/inicial.csv`: grade para importar. Texto puro, **UTF-8**, separado por `;`:

  ```
  DEPARTAMENTO;DISCIPLINA;TURMA;PROFESSORES;DIA;INICIO;FIM;SALA
  DSC;ESTRUTURA DE DADOS;2C;ANTONIO / STELA;SEG;07:10;08:50;I-06
  ```
  As salas usadas no CSV precisam estar cadastradas antes. No LibreOffice/Excel, use
  "Salvar como → CSV", separador `;`, codificação UTF-8.

## Como instalar o GTK no Windows (uma vez só)

O MinGW que vem com o Code::Blocks **não** traz o GTK e **não** consegue usar o GTK do MSYS2
(dá erro de ligação, `undefined reference to _gnu_exception_handler`). Por isso o Code::Blocks
precisa usar o compilador do próprio MSYS2. Passo a passo:

1. Baixe e instale o MSYS2 em <https://www.msys2.org> (pasta padrão `C:\msys64`).
2. Abra o atalho **"MSYS2 UCRT64"** e rode:
   ```
   pacman -Syu
   pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-gtk3 mingw-w64-ucrt-x86_64-pkgconf
   ```
3. No Code::Blocks: **Settings → Compiler → Toolchain executables**, em
   "Compiler's installation directory" coloque `C:\msys64\ucrt64`.
   (O Code::Blocks passa a achar o `pkg-config` e as DLLs do GTK sozinho ao compilar e executar.)
4. Ainda no Code::Blocks: **Settings → Editor → Encoding settings → UTF-8**
   (os textos da interface têm acentos).
5. Opcional: para abrir o `.exe` com duplo clique fora do Code::Blocks, adicione
   `C:\msys64\ucrt64\bin` ao **PATH** do Windows (Iniciar → "variáveis de ambiente" → Path → Novo).

## Como compilar e executar

**Code::Blocks:** abra `DATP_backend.cbp`, escolha o alvo na barra de ferramentas e aperte F9.
- `Debug`: programa com interface (com console para ver avisos)
- `Release`: programa final, sem console
- `Testes`: roda os testes do backend no console

**Linha de comando** (no terminal "MSYS2 UCRT64", dentro da pasta do projeto):

```
gcc -std=gnu99 -Wall -Iinclude src/*.c -o datp.exe $(pkg-config --cflags --libs gtk+-3.0)
./datp.exe
```

Testes do backend (não precisa de GTK):

```
gcc -std=gnu99 -Wall -Iinclude tests/testes.c src/util.c src/salas_crud.c src/alocacao_crud.c src/csv_import.c -o testes.exe
./testes.exe
```

O programa deve ser executado **a partir da pasta do projeto**, porque procura os dados em `data/`.
O Code::Blocks já faz isso.

> Para levar o `.exe` para outro computador sem MSYS2, é preciso copiar junto as DLLs do GTK
> (`C:\msys64\ucrt64\bin`). Para a apresentação, rodar pelo Code::Blocks é o mais simples.
