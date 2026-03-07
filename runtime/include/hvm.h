#ifndef HVM_H
#define HVM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

#define HVM_STACK_SIZE 1024
#define HVM_CALL_STACK_SIZE 512
#define HVM_MEMORY_SIZE 4096
#define HVM_MAX_FUNCTIONS 256
#define HVM_MAX_STRINGS 512

typedef enum {
    HVM_PUSH_INT,
    HVM_PUSH_FLOAT,
    HVM_PUSH_STRING,
    HVM_PUSH_BOOL,
    HVM_POP,

    HVM_ADD,
    HVM_SUB,
    HVM_MUL,
    HVM_DIV,
    HVM_MOD,

    HVM_EQ,
    HVM_NE,
    HVM_LT,
    HVM_LE,
    HVM_GT,
    HVM_GE,

    HVM_AND,
    HVM_OR,
    HVM_NOT,

    HVM_LOAD,
    HVM_STORE,
    HVM_LOAD_GLOBAL,
    HVM_STORE_GLOBAL,

    HVM_JUMP,
    HVM_JUMP_IF_FALSE,
    HVM_JUMP_IF_TRUE,
    HVM_CALL,
    HVM_RETURN,

    HVM_PRINT,
    HVM_PRINTLN,
    HVM_READ,

    HVM_HALT,
    HVM_NOP,
    HVM_DUP,
    HVM_SWAP,

    HVM_WIN32_MSG,
    HVM_WIN32_ERROR,
    HVM_WIN32_INFO,
    HVM_WIN32_WARNING,
    HVM_SLEEP,
    HVM_BEEP,

    HVM_CREATE_WINDOW,
    HVM_DRAW_TEXT,
    HVM_DRAW_BUTTON,
    HVM_DRAW_BUTTON_STATE,
    HVM_DRAW_INPUT,
    HVM_DRAW_INPUT_STATE,
    HVM_DRAW_TEXTAREA,
    HVM_DRAW_LIST,
    HVM_SET_COLOR,
    HVM_CLEAR,
    HVM_SET_BG_COLOR,
    HVM_SET_FONT_SIZE,
    HVM_DRAW_IMAGE,
    HVM_GET_MOUSE_X,
    HVM_GET_MOUSE_Y,
    HVM_IS_MOUSE_DOWN,
    HVM_WAS_MOUSE_UP,
    HVM_WAS_MOUSE_CLICK,
    HVM_IS_MOUSE_HOVER,
    HVM_IS_KEY_DOWN,
    HVM_WAS_KEY_PRESS,
    HVM_DELTA_TIME,
    HVM_LAYOUT_RESET,
    HVM_LAYOUT_NEXT,
    HVM_LAYOUT_ROW,
    HVM_LAYOUT_COLUMN,
    HVM_LAYOUT_GRID,
    HVM_LOOP,
    HVM_MENU_SETUP_NOTEPAD,
    HVM_MENU_EVENT,
    HVM_SCROLL_SET_RANGE,
    HVM_SCROLL_Y,
    HVM_FILE_OPEN_DIALOG,
    HVM_FILE_SAVE_DIALOG,
    HVM_FILE_READ,
    HVM_FILE_READ_LINE,
    HVM_FILE_WRITE,
    HVM_INPUT_SET,
    HVM_TEXTAREA_SET,
    HVM_EXEC_COMMAND,

    HVM_OPCODE_COUNT
} HVM_Opcode;

typedef enum {
    HVM_TYPE_INT,
    HVM_TYPE_FLOAT,
    HVM_TYPE_STRING,
    HVM_TYPE_BOOL,
    HVM_TYPE_NULL
} HVM_Type;

typedef struct {
    HVM_Type type;
    union {
        int64_t int_value;
        double float_value;
        char* string_value;
        int bool_value;
    } data;
} HVM_Value;

typedef struct {
    HVM_Opcode opcode;
    union {
        int64_t int_operand;
        double float_operand;
        char* string_operand;
        size_t address_operand;
    } operand;
} HVM_Instruction;

typedef struct {
    char* name;
    size_t entry_point;
    size_t parameter_count;
    size_t local_count;
} HVM_Function;

typedef struct HVM_GCObject {
    char *ptr;
    size_t size;
    int marked;
    struct HVM_GCObject *next;
} HVM_GCObject;

typedef struct {
    HVM_Value stack[HVM_STACK_SIZE];
    size_t stack_top;

    size_t call_stack[HVM_CALL_STACK_SIZE];
    size_t call_top;

    HVM_Value memory[HVM_MEMORY_SIZE];
    size_t memory_used;

    HVM_Instruction* instructions;
    size_t instruction_count;
    size_t instruction_capacity;

    size_t pc;

    HVM_Function functions[HVM_MAX_FUNCTIONS];
    size_t function_count;

    char* strings[HVM_MAX_STRINGS];
    size_t string_count;

    int running;
    int error_code;
    char* error_message;

    HVM_GCObject *gc_objects;
    size_t gc_object_count;
    size_t gc_bytes;
    size_t gc_next_collection;
    int gc_enabled;
    int gc_pending;
} HVM_VM;

HVM_VM* hvm_create(void);
void hvm_destroy(HVM_VM* vm);
int hvm_load_bytecode(HVM_VM* vm, const HVM_Instruction* instructions, size_t count);
int hvm_run(HVM_VM* vm);

int hvm_add_instruction(HVM_VM* vm, HVM_Opcode opcode, int64_t operand);
int hvm_add_instruction_float(HVM_VM* vm, HVM_Opcode opcode, double operand);
int hvm_add_instruction_string(HVM_VM* vm, HVM_Opcode opcode, const char* operand);
int hvm_add_instruction_address(HVM_VM* vm, HVM_Opcode opcode, size_t address);

int hvm_push_int(HVM_VM* vm, int64_t value);
int hvm_push_float(HVM_VM* vm, double value);
int hvm_push_string(HVM_VM* vm, const char* value);
int hvm_push_bool(HVM_VM* vm, int value);
HVM_Value hvm_pop(HVM_VM* vm);
HVM_Value hvm_peek(HVM_VM* vm, size_t offset);

int hvm_store_variable(HVM_VM* vm, const char* name, HVM_Value value);
HVM_Value hvm_load_variable(HVM_VM* vm, const char* name);

int hvm_define_function(HVM_VM* vm, const char* name, size_t entry_point, size_t param_count);
HVM_Function* hvm_find_function(HVM_VM* vm, const char* name);

int hvm_add_string(HVM_VM* vm, const char* str);
const char* hvm_get_string(HVM_VM* vm, size_t index);

void hvm_gc_collect(HVM_VM* vm);
void hvm_gc_set_enabled(HVM_VM* vm, int enabled);
size_t hvm_gc_live_objects(HVM_VM* vm);
size_t hvm_gc_live_bytes(HVM_VM* vm);

void hvm_set_error(HVM_VM* vm, int code, const char* message);
const char* hvm_get_error(HVM_VM* vm);

void hvm_print_stack(HVM_VM* vm);
void hvm_print_instructions(HVM_VM* vm);
void hvm_disassemble(HVM_VM* vm);

#ifdef __cplusplus
}
#endif

#endif // HVM_H
