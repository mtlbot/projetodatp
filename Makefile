CC = gcc
CFLAGS = -Wall -g -Iinclude `pkg-config --cflags gtk+-3.0`
LIBS = `pkg-config --libs gtk+-3.0`

SRC = $(wildcard src/*.c)
TARGET = bin/app

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LIBS)

clean:
	rm -f bin/app