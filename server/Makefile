# Compiler
CROSS_COMPILE ?=
CC ?= $(CROSS_COMPILE)gcc

CFLAGS ?=
CFLAGS += -Wall -Werror

LDFLAGS ?=

# Targets
SRC = aesdsocket.c
OBJ = $(SRC:.c=.o)
EXE = $(SRC:.c=)	

# Rules
.phony: all default clean

all: $(EXE)
default: $(EXE)

clean:
	rm -f $(OBJ) $(EXE)

$(EXE): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c $<
