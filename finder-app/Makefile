# Compiler
CROSS_COMPILE ?=
CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= 
LDFLAGS ?= 

# Targets
SRC = writer.c
OBJ = $(SRC:.c=.o)
EXE = $(SRC:.c=)

# Rules
.phony: all clean

all: $(EXE)

clean:
	rm -f $(EXE) $(OBJ)

$(EXE): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c $<

