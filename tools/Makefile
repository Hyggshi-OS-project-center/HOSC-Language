CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2
LDFLAGS = -luser32 -lkernel32 -lgdi32 -lcomdlg32

ROOT = ..
OUT_DIR = $(ROOT)/tools/bin

COMPILER_SRC = \
	$(ROOT)/compiler/src/hosc_compiler.c \
	$(ROOT)/compiler/src/lexer.c \
	$(ROOT)/compiler/src/parser.c \
	$(ROOT)/compiler/src/arena.c \
	$(ROOT)/compiler/src/codegen.c \
	$(ROOT)/compiler/src/ast_utils.c \
	$(ROOT)/runtime/src/hvm.c \
	$(ROOT)/runtime/src/hvm_compiler.c

RUNTIME_SRC = \
	$(ROOT)/runtime/src/hvm_runner.c \
	$(ROOT)/runtime/src/hvm.c

INCLUDES = -I$(ROOT)/compiler/include -I$(ROOT)/runtime/include

COMPILER_EXE = $(OUT_DIR)/hosc-compiler.exe
RUNTIME_EXE = $(OUT_DIR)/hvm.exe

all: $(COMPILER_EXE) $(RUNTIME_EXE)

$(OUT_DIR):
	mkdir -p $(OUT_DIR)

$(COMPILER_EXE): $(COMPILER_SRC) | $(OUT_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $(COMPILER_SRC) $(LDFLAGS)

$(RUNTIME_EXE): $(RUNTIME_SRC) | $(OUT_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $(RUNTIME_SRC) $(LDFLAGS)

clean:
	rm -f $(COMPILER_EXE) $(RUNTIME_EXE)

.PHONY: all clean
