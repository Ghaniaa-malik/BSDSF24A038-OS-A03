CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -g -Iinclude -DUSE_READLINE
LDFLAGS ?=
LDLIBS ?= -lreadline
SRC := src/main.c src/shell.c src/execute.c
OBJ := $(SRC:.c=.o)

.PHONY: all clean
all: bin/myshell

bin/myshell: $(OBJ)
	@mkdir -p bin
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

src/%.o: src/%.c include/shell.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) bin/myshell
