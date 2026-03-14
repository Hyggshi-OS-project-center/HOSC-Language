# Build script for HOSC framework runtime

CC ?= gcc
CXX ?= g++
CFLAGS ?= -Wall -Wextra -std=c99 -O2
CXXFLAGS ?= -Wall -Wextra -std=c++17 -O2
ROOT = ..
INCLUDES = -I$(ROOT)/compiler/include -I$(ROOT)/runtime/include -I$(ROOT)/core/include -I$(ROOT)/services/include

ifeq ($(OS),Windows_NT)
EXE = .exe
GUI_LIBS = -luser32 -lgdi32 -lkernel32 -lcomdlg32
RUNNER = .\\
MKDIR_BIN = if not exist bin mkdir bin
RM_FILE = del /Q
else
EXE =
GUI_LIBS =
RUNNER = ./
MKDIR_BIN = mkdir -p bin
RM_FILE = rm -f
endif

SRC = \
	$(ROOT)/core/src/file_utils.c \
	$(ROOT)/compiler/src/lexer.c \
	$(ROOT)/compiler/src/parser.c \
	$(ROOT)/compiler/src/arena.cpp \
	$(ROOT)/compiler/src/ast_utils.c \
	$(ROOT)/runtime/src/hvm.c \
	$(ROOT)/runtime/src/bytecode.cpp \
	$(ROOT)/runtime/src/bytecode_io.c \
	$(ROOT)/runtime/src/gc.cpp \
	$(ROOT)/runtime/src/vm_memory.cpp \
	$(ROOT)/runtime/src/hvm_compiler.c \
	$(ROOT)/runtime/src/hvm_platform.c \
	$(ROOT)/services/src/runtime_services.c \
	$(ROOT)/services/src/runtime_gui.c \
	src/hosc_framework.c
OUT = bin/hosc_framework$(EXE)

OBJ = $(SRC:.c=.o)
OBJ := $(OBJ:.cpp=.o)

all: framework

framework: $(OUT)

$(OUT): $(OBJ) include/hosc_runtime.h | bin
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $(OBJ) $(GUI_LIBS)

bin:
	$(MKDIR_BIN)

run: framework
	$(RUNNER)$(OUT) run examples/window.hosc

run-smoke: framework
	$(RUNNER)$(OUT) run examples/smoke.hosc

clean:
	$(RM_FILE) $(OUT) $(OBJ)

help:
	@echo "Targets:"
	@echo "  make -f makefile framework"
	@echo "  make -f makefile run"
	@echo "  make -f makefile run-smoke"
	@echo "  make -f makefile clean"

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

.PHONY: all framework run run-smoke clean help
