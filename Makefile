# Projeto DATP - Controle de Salas POLI-UPE
# Linux:          make        (gera bin/app)
# Windows/MSYS2:  make        (no terminal "MSYS2 MINGW64"; gera bin/datp_app.exe)
#                 Pacotes:    pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-gtk3 mingw-w64-x86_64-pkgconf make

CC = gcc
CFLAGS = -Wall -Wextra -g -Iinclude $(shell pkg-config --cflags gtk+-3.0)
LIBS = $(shell pkg-config --libs gtk+-3.0)

SRC = $(wildcard src/*.c)

ifeq ($(OS),Windows_NT)
    TARGET = bin/datp_app.exe
    # -mwindows: abre só a janela, sem o terminal preto atrás
    LIBS += -mwindows
else
    TARGET = bin/app
endif

all: $(TARGET)

$(TARGET): $(SRC) $(wildcard include/*.h)
	@mkdir -p bin data
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LIBS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f bin/app bin/datp_app.exe

.PHONY: all run clean
