/*
 * File: tools\legacy\codegen_backup.c
 * Purpose: HOSC source file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "codegen.h"
#include "ast.h"

void codegen_generate(ASTNode* ast, const char* output_file) {
    FILE* file = fopen(output_file, "w");
    if (!file) {
        printf("Error: Could not open output file %s\n", output_file);
        return;
    }
    
    fprintf(file, "#include <stdio.h>\n");
    fprintf(file, "#include <stdlib.h>\n");
    fprintf(file, "#include <string.h>\n");
    fprintf(file, "#include <windows.h>\n\n");
    fprintf(file, "int main() {\n");
    
    codegen_generate_program(ast, file);
    
    fprintf(file, "    return 0;\n");
    fprintf(file, "}\n");
    
    fclose(file);
    printf("Generated C code: %s\n", output_file);
}

void codegen_generate_program(ASTNode* ast, FILE* file) {
    if (!ast || ast->type != AST_PROGRAM) return;
    
    for (size_t i = 0; i < ast->data.program.statement_count; i++) {
        codegen_generate_statement(ast->data.program.statements[i], file);
    }
}

void codegen_generate_statement(ASTNode* ast, FILE* file) {
    if (!ast) return;
    
    switch (ast->type) {
        case AST_VAR_DECLARATION:
            fprintf(file, "    int %s = ", ast->data.var_declaration.name);
            if (ast->data.var_declaration.value) {
                codegen_generate_expression(ast->data.var_declaration.value, file);
            }
            fprintf(file, ";\n");
            break;
            
        case AST_PRINT:
            fprintf(file, "    printf(\"%%s\\n\", %s);\n", ast->data.print.message);
            break;
            
        case AST_PRINTLN:
            fprintf(file, "    printf(\"%%s\\n\", %s);\n", ast->data.print.message);
            break;
            
        case AST_DEBUG_PRINT:
            fprintf(file, "    printf(\"[DEBUG] %%s\\n\", %s);\n", ast->data.print.message);
            break;
            
        case AST_WIN32_MESSAGE_BOX:
            fprintf(file, "    MessageBox(NULL, %s, \"Message\", MB_OK);\n", ast->data.win32_message_box.message);
            break;
            
        case AST_WIN32_ERROR:
            fprintf(file, "    MessageBox(NULL, %s, \"Error\", MB_OK | MB_ICONERROR);\n", ast->data.win32_error.message);
            break;
            
        case AST_WIN32_INFO:
            fprintf(file, "    MessageBox(NULL, %s, \"Information\", MB_OK | MB_ICONINFORMATION);\n", ast->data.win32_info.message);
            break;
            
        case AST_WIN32_WARNING:
            fprintf(file, "    MessageBox(NULL, %s, \"Warning\", MB_OK | MB_ICONWARNING);\n", ast->data.win32_warning.message);
            break;
            
        case AST_WIN32_YESNO:
            fprintf(file, "    int result = MessageBox(NULL, %s, \"Question\", MB_YESNO | MB_ICONQUESTION);\n", ast->data.win32_yesno.message);
            fprintf(file, "    if (result == IDYES) {\n");
            fprintf(file, "        MessageBox(NULL, \"You clicked Yes!\", \"Result\", MB_OK);\n");
            fprintf(file, "    } else {\n");
            fprintf(file, "        MessageBox(NULL, \"You clicked No!\", \"Result\", MB_OK);\n");
            fprintf(file, "    }\n");
            break;
            
        case AST_WIN32_CREATE_WINDOW:
            fprintf(file, "    MessageBox(NULL, %s, %s, MB_OK);\n", 
                    ast->data.win32_create_window.message, 
                    ast->data.win32_create_window.title);
            break;
            
        case AST_WIN32_SLEEP:
            fprintf(file, "    Sleep(%d);\n", ast->data.win32_sleep.milliseconds);
            break;
            
        case AST_WIN32_FILE_DIALOG:
            fprintf(file, "    // File dialog: %s (Filter: %s)\n", 
                    ast->data.win32_file_dialog.title, 
                    ast->data.win32_file_dialog.filter);
            break;
            
        case AST_WIN32_COLOR_DIALOG:
            fprintf(file, "    // Color dialog: RGB(%d, %d, %d)\n", 
                    ast->data.win32_color_dialog.red, 
                    ast->data.win32_color_dialog.green, 
                    ast->data.win32_color_dialog.blue);
            break;
            
        case AST_WIN32_FONT_DIALOG:
            fprintf(file, "    // Font dialog: %s, Size: %d\n", 
                    ast->data.win32_font_dialog.font_name, 
                    ast->data.win32_font_dialog.font_size);
            break;
            
        case AST_WIN32_OPEN_URL:
            fprintf(file, "    // Open URL: %s\n", ast->data.win32_open_url.url);
            break;
            
        case AST_WIN32_BEEP:
            fprintf(file, "    Beep(%d, %d);\n", 
                    ast->data.win32_beep.frequency, 
                    ast->data.win32_beep.duration);
            break;
            
        case AST_WIN32_GET_SCREEN_SIZE:
            fprintf(file, "    int width = GetSystemMetrics(SM_CXSCREEN);\n");
            fprintf(file, "    int height = GetSystemMetrics(SM_CYSCREEN);\n");
            fprintf(file, "    printf(\"Screen Size: %%dx%%d\\n\", width, height);\n");
            break;
            
        case AST_WIN32_GET_CURSOR_POS:
            fprintf(file, "    POINT pt;\n");
            fprintf(file, "    GetCursorPos(&pt);\n");
            fprintf(file, "    printf(\"Cursor Position: (%%d, %%d)\\n\", pt.x, pt.y);\n");
            break;
            
        case AST_WIN32_SET_CURSOR_POS:
            fprintf(file, "    SetCursorPos(%d, %d);\n", 
                    ast->data.win32_set_cursor_pos.x, 
                    ast->data.win32_set_cursor_pos.y);
            break;
            
        case AST_WIN32_GET_CLIPBOARD_TEXT:
            fprintf(file, "    // Get clipboard text\n");
            break;
            
        case AST_WIN32_SET_CLIPBOARD_TEXT:
            fprintf(file, "    // Set clipboard text: %s\n", ast->data.win32_set_clipboard_text.text);
            break;
            
        case AST_WIN32_GET_SYSTEM_INFO:
            fprintf(file, "    // System info: %s\n", ast->data.win32_get_system_info.info);
            break;
            
        case AST_WIN32_GET_TIME:
            fprintf(file, "    SYSTEMTIME st;\n");
            fprintf(file, "    GetLocalTime(&st);\n");
            fprintf(file, "    printf(\"Current Time: %%02d:%%02d:%%02d\\n\", st.wHour, st.wMinute, st.wSecond);\n");
            break;
            
        default:
            break;
    }
}

void codegen_generate_expression(ASTNode* ast, FILE* file) {
    if (!ast) return;
    
    switch (ast->type) {
        case AST_LITERAL:
            switch (ast->data.literal.literal_type) {
                case LITERAL_INT:
                    fprintf(file, "%d", ast->data.literal.value.int_value);
                    break;
                case LITERAL_FLOAT:
                    fprintf(file, "%f", ast->data.literal.value.float_value);
                    break;
                case LITERAL_STRING:
                    fprintf(file, "\"%s\"", ast->data.literal.value.string_value);
                    break;
                case LITERAL_BOOL:
                    fprintf(file, "%s", ast->data.literal.value.bool_value ? "true" : "false");
                    break;
            }
            break;
            
        case AST_IDENTIFIER:
            fprintf(file, "%s", ast->data.identifier.name);
            break;
            
        case AST_BINARY_OP:
            codegen_generate_expression(ast->data.binary_op.left, file);
            fprintf(file, " %s ", ast->data.binary_op.operator);
            codegen_generate_expression(ast->data.binary_op.right, file);
            break;
            
        case AST_UNARY_OP:
            fprintf(file, "%s", ast->data.unary_op.operator);
            codegen_generate_expression(ast->data.unary_op.operand, file);
            break;
            
        default:
            break;
    }
}