CC = gcc
CFLAGS = -O3 -Wall -Wextra -Werror -std=c99 -pedantic -Iinclude
SRC = src/dfrc.c src/board.c src/format.c src/convert_vf.c

ifeq ($(OS),Windows_NT)
    MKDIR = if not exist bin mkdir bin
    RM = if exist bin rmdir /s /q bin
    EXEC_EXT = .exe
else
    MKDIR = mkdir -p bin
    RM = rm -rf bin *.o
    EXEC_EXT =
endif

.PHONY: all check-header test-dfrc test-board test-roundtrip test-convert clean

all: bin check-header bin/rodf-tool$(EXEC_EXT) test-dfrc test-board test-roundtrip test-convert

bin:
	@$(MKDIR)

# CLI Binary
bin/rodf-tool$(EXEC_EXT): $(SRC) cmd/rodf_tool.c
	$(CC) $(CFLAGS) -o $@ $^
	@echo [OK] Built bin/rodf-tool$(EXEC_EXT)

# Verify include/rodentformat.h has valid C syntax and self-contained headers
check-header:
	$(CC) $(CFLAGS) -fsyntax-only include/rodentformat.h
	@echo [OK] include/rodentformat.h compiles cleanly.

test-dfrc: bin
	$(CC) $(CFLAGS) -o bin/test_dfrc$(EXEC_EXT) tests/test_dfrc.c src/dfrc.c
	@bin/test_dfrc$(EXEC_EXT)

test-board: bin
	$(CC) $(CFLAGS) -o bin/test_board$(EXEC_EXT) tests/test_board.c src/board.c src/dfrc.c
	@bin/test_board$(EXEC_EXT)

test-roundtrip: bin
	$(CC) $(CFLAGS) -o bin/test_roundtrip$(EXEC_EXT) tests/test_roundtrip.c src/format.c src/dfrc.c
	@bin/test_roundtrip$(EXEC_EXT)

test-convert: bin
	$(CC) $(CFLAGS) -o bin/test_convert$(EXEC_EXT) tests/test_convert.c src/convert_vf.c src/format.c src/dfrc.c
	@bin/test_convert$(EXEC_EXT)

clean:
	@$(RM)
