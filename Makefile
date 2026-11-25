# DATP 360 - Controle de salas e laboratórios da POLI-UPE
# Linux:          make        (gera bin/app)
# Windows/MSYS2:  make        (no terminal "MSYS2 MINGW64"; gera bin/datp_app.exe)
#                 Pacotes:    pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-gtk3 mingw-w64-x86_64-pkgconf make
#                 Leitura de PDF (biblioteca): pacman -S mingw-w64-x86_64-poppler   (Linux: sudo apt install libpoppler-glib-dev)
#
# make pacote  (Windows/MSYS2): monta a pasta DATP360/ com o programa e TUDO que ele precisa
#              (DLLs do GTK e da Poppler, ícones). Essa pasta roda em qualquer Windows, sem o MSYS2:
#              copie para o computador do DATP e abra bin/datp_app.exe.

CC = gcc
CFLAGS = -Wall -Wextra -g -Iinclude $(shell pkg-config --cflags gtk+-3.0 poppler-glib)
LIBS = $(shell pkg-config --libs gtk+-3.0 poppler-glib) -lm

SRC = $(wildcard src/*.c)

ifeq ($(OS),Windows_NT)
    TARGET = bin/datp_app.exe
    # -mwindows: abre só a janela, sem o terminal preto atrás
    LIBS += -mwindows
    # Ícone do .exe (aparece no Explorer e na barra de tarefas)
    RES = bin/icone.res
else
    TARGET = bin/app
    RES =
endif

all: $(TARGET)

$(TARGET): $(SRC) $(wildcard include/*.h) $(RES)
	@mkdir -p bin data
	$(CC) $(CFLAGS) $(SRC) $(RES) -o $(TARGET) $(LIBS)

bin/icone.res: icone.rc icone.ico
	@mkdir -p bin
	windres icone.rc -O coff -o bin/icone.res

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f bin/app bin/datp_app.exe bin/icone.res

# ---------------------------------------------------------------
# Pacote para Windows (rodar no terminal MSYS2 MINGW64)
# ---------------------------------------------------------------
PACOTE = DATP360
MINGW = /mingw64

# Copia para $(PACOTE)/bin todas as DLLs do MSYS2 que um .exe/.dll usa
define copiar_dlls
	ldd $(1) | grep -i '$(MINGW)/' | awk '{print $$3}' | sort -u | xargs -r -I{} cp -u {} $(PACOTE)/bin/
endef

pacote: $(TARGET)
	rm -rf $(PACOTE)
	mkdir -p $(PACOTE)/bin $(PACOTE)/data $(PACOTE)/ajuda $(PACOTE)/lib $(PACOTE)/share/glib-2.0 $(PACOTE)/share/icons
	cp $(TARGET) $(PACOTE)/bin/
	$(call copiar_dlls,$(TARGET))
	# Arquivos que o GTK procura ao lado do programa (imagens, ícones, configurações)
	cp -r $(MINGW)/lib/gdk-pixbuf-2.0 $(PACOTE)/lib/
	for f in $(PACOTE)/lib/gdk-pixbuf-2.0/2.10.0/loaders/*.dll; do $(call copiar_dlls,$$f); done
	cp -r $(MINGW)/share/glib-2.0/schemas $(PACOTE)/share/glib-2.0/
	cp -r $(MINGW)/share/icons/Adwaita $(MINGW)/share/icons/hicolor $(PACOTE)/share/icons/
	# Arquivos do próprio sistema
	cp style.css icone.png icone.ico $(PACOTE)/
	cp -r ajuda/. $(PACOTE)/ajuda/
	-cp data/*.dat data/historico.txt data/semestre.txt $(PACOTE)/data/ 2>/dev/null
	@echo ""
	@echo "Pronto! A pasta $(PACOTE) tem o sistema completo."
	@echo "Copie a pasta para o computador do DATP, abra $(PACOTE)/bin/datp_app.exe."

.PHONY: all run clean pacote
