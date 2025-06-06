CROSS_COMPILE ?=

# Compiler
CC = $(CROSS_COMPILE)gcc
CFLAGS = 

# Linker
LD = $(CROSS_COMPILE)gcc
LDFLAGS = 

# Targets
SRC = writer.c
OBJ = $(SRC:.c=.o)
EXE = $(SRC:.c=)

# Rules
.phony: default clean

default: $(EXE)

clean:
	rm -f $(EXE) $(OBJ)

$(EXE): $(OBJ)
	$(LD) -o $@ $(LDFLAGS) $<

%.o: %.c
	$(CC) -c -o $@ $(CFLAGS) $<

