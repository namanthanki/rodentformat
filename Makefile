CC = gcc
CFLAGS = -O3 -Wall -Wextra -Werror -std=c99 -pedantic -Iinclude

ifeq ($(OS),Windows_NT)
    MKDIR = if not exist bin mkdir bin
    RM = if exist bin rmdir /s /q bin
    EXEC_EXT = .exe
else
    MKDIR = mkdir -p bin
    RM = rm -rf bin *.o
    EXEC_EXT =
endif

.PHONY: all check-header test-dfrc clean

all: bin check-header test-dfrc

bin:
	@$(MKDIR)

# Verify include/rodentformat.h has valid C syntax and self-contained headers
check-header:
	$(CC) $(CFLAGS) -fsyntax-only include/rodentformat.h
	@echo [OK] include/rodentformat.h compiles cleanly.

test-dfrc: bin
	$(CC) $(CFLAGS) -o bin/test_dfrc$(EXEC_EXT) tests/test_dfrc.c src/dfrc.c
	@bin/test_dfrc$(EXEC_EXT)

clean:
	@$(RM)
