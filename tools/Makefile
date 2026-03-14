CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2
CXXFLAGS = -Wall -Wextra -std=c++17 -O2
LDFLAGS = -luser32 -lkernel32 -lgdi32 -lcomdlg32 -lstdc++

ROOT = ..
OUT_DIR = $(ROOT)/tools/bin
OBJ_DIR = $(ROOT)/tools/obj

COMPILER_SRC = \
	$(ROOT)/compiler/src/hosc_compiler.c \
	$(ROOT)/compiler/src/lexer.c \
	$(ROOT)/compiler/src/parser.c \
	$(ROOT)/compiler/src/arena.c \
	$(ROOT)/compiler/src/codegen.c \
	$(ROOT)/compiler/src/ast_utils.c \
	$(ROOT)/runtime/src/hvm.c \
	$(ROOT)/runtime/src/hvm_compiler.c

CLI_SRC = $(ROOT)/tools/hosc_cli.c

RUNTIME_SRC = \
	$(ROOT)/runtime/src/hvm_runner.c \
	$(ROOT)/runtime/src/hvm.c

CPP_API_SRC = $(ROOT)/runtime/src/hosc_cpp_api.cpp
CPP_API_OBJ = $(OBJ_DIR)/hosc_cpp_api.o

INCLUDES = -I$(ROOT)/compiler/include -I$(ROOT)/runtime/include

COMPILER_EXE = $(OUT_DIR)/hosc-compiler.exe
RUNTIME_EXE = $(OUT_DIR)/hvm.exe
CLI_EXE = $(OUT_DIR)/hosc.exe
TEST_EXE = $(OUT_DIR)/test_compiler.exe

all: $(COMPILER_EXE) $(RUNTIME_EXE) $(CLI_EXE) $(TEST_EXE)

$(OUT_DIR):
	mkdir -p $(OUT_DIR)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(CPP_API_OBJ): $(CPP_API_SRC) | $(OBJ_DIR)
	$(CC) $(CXXFLAGS) $(INCLUDES) -x c++ -c $< -o $@

$(COMPILER_EXE): $(CLI_SRC) $(COMPILER_SRC) $(CPP_API_OBJ) | $(OUT_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $(CLI_SRC) $(COMPILER_SRC) $(CPP_API_OBJ) $(LDFLAGS)

$(RUNTIME_EXE): $(RUNTIME_SRC) $(CPP_API_OBJ) | $(OUT_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $(RUNTIME_SRC) $(CPP_API_OBJ) $(LDFLAGS)

$(CLI_EXE): $(CLI_SRC) $(COMPILER_SRC) $(CPP_API_OBJ) | $(OUT_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $(CLI_SRC) $(COMPILER_SRC) $(CPP_API_OBJ) $(LDFLAGS)

$(TEST_EXE): $(ROOT)/tools/tests/test_compiler.c $(COMPILER_SRC) $(CPP_API_OBJ) | $(OUT_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -I$(ROOT)/tools/tests -o $@ $(ROOT)/tools/tests/test_compiler.c $(COMPILER_SRC) $(CPP_API_OBJ) $(LDFLAGS)

test: $(TEST_EXE)
	$(TEST_EXE)

clean:
	rm -rf $(OUT_DIR) $(OBJ_DIR)

.PHONY: all clean test
