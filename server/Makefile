# Compiler
CROSS_COMPILE ?=
CC ?= $(CROSS_COMPILE)gcc

CFLAGS ?=
CFLAGS += -Wall -Werror

LDFLAGS ?=

# Targets
SRC = $(wildcard src/*.c)
OBJ = $(SRC:src/%.c=%.o)
EXE = aesdsocket

# Rules
.phony: all default clean

all: $(EXE)
default: $(EXE)

clean:
	rm -f $(OBJ) $(EXE)

$(EXE): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: src/%.c
	$(CC) $(CFLAGS) -o $@ -c $<
