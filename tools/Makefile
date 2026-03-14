CC ?= gcc
CXX ?= g++
CFLAGS ?= -Wall -Wextra -std=c99 -O2
CXXFLAGS ?= -Wall -Wextra -std=c++17 -O2
LDFLAGS = -luser32 -lkernel32 -lgdi32 -lcomdlg32

ROOT = ..
OUT_DIR = $(ROOT)/tools/bin

COMPILER_SRC = \
	$(ROOT)/core/src/file_utils.c \
	$(ROOT)/compiler/src/hosc_compiler.c \
	$(ROOT)/compiler/src/lexer.c \
	$(ROOT)/compiler/src/parser.c \
	$(ROOT)/compiler/src/arena.cpp \
	$(ROOT)/compiler/src/codegen.c \
	$(ROOT)/compiler/src/ast_utils.c \
	$(ROOT)/runtime/src/hvm.c \
	$(ROOT)/runtime/src/bytecode.cpp \
	$(ROOT)/runtime/src/gc.cpp \
	$(ROOT)/runtime/src/bytecode_io.c \
	$(ROOT)/runtime/src/vm_memory.cpp \
	$(ROOT)/services/src/runtime_gui.c \
	$(ROOT)/services/src/runtime_services.c \
	$(ROOT)/runtime/src/hvm_compiler.c

RUNTIME_SRC = \
	$(ROOT)/core/src/file_utils.c \
	$(ROOT)/runtime/src/hvm_runner.c \
	$(ROOT)/runtime/src/hvm_platform.c \
	$(ROOT)/runtime/src/hvm.c \
	$(ROOT)/runtime/src/bytecode.cpp \
	$(ROOT)/runtime/src/gc.cpp \
	$(ROOT)/runtime/src/bytecode_io.c \
	$(ROOT)/runtime/src/vm_memory.cpp \
	$(ROOT)/services/src/runtime_gui.c \
	$(ROOT)/services/src/runtime_services.c

INCLUDES = -I$(ROOT)/compiler/include -I$(ROOT)/runtime/include -I$(ROOT)/core/include -I$(ROOT)/services/include

COMPILER_EXE = $(OUT_DIR)/hosc_compiler.exe
RUNTIME_EXE = $(OUT_DIR)/hvm.exe

COMPILER_OBJ = $(COMPILER_SRC:.c=.o)
COMPILER_OBJ := $(COMPILER_OBJ:.cpp=.o)
RUNTIME_OBJ = $(RUNTIME_SRC:.c=.o)
RUNTIME_OBJ := $(RUNTIME_OBJ:.cpp=.o)

all: $(COMPILER_EXE) $(RUNTIME_EXE)

$(OUT_DIR):
	mkdir -p $(OUT_DIR)

$(COMPILER_EXE): $(COMPILER_OBJ) | $(OUT_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $(COMPILER_OBJ) $(LDFLAGS)

$(RUNTIME_EXE): $(RUNTIME_OBJ) | $(OUT_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $(RUNTIME_OBJ) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

clean:
	rm -f $(COMPILER_EXE) $(RUNTIME_EXE) $(COMPILER_OBJ) $(RUNTIME_OBJ)

.PHONY: all clean


