// codegen_simple.c - Simple C code generator for HOSC AST
//
// Generates basic C code from the simplified AST structure.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "codegen.h"
#include "ast.h"

// Simple code generator that returns a string
char* codegen_generate(ASTNode* ast) {
    if (!ast) return NULL;
    
    char* result = malloc(1024);
    if (!result) return NULL;
    
    strcpy(result, "// Generated C code from HOSC AST\n");
    strcat(result, "#include <stdio.h>\n");
    strcat(result, "#include <stdlib.h>\n\n");
    strcat(result, "int main() {\n");
    
    // Generate code based on AST type
    switch (ast->type) {
        case AST_NUMBER:
            sprintf(result + strlen(result), "    printf(\"%%f\\n\", %f);\n", ast->data.number.value);
            break;
            
        case AST_VARIABLE_DECLARATION:
            sprintf(result + strlen(result), "    double %s = ", ast->data.variable_declaration.identifier);
            if (ast->data.variable_declaration.value) {
                if (ast->data.variable_declaration.value->type == AST_NUMBER) {
                    sprintf(result + strlen(result), "%f", ast->data.variable_declaration.value->data.number.value);
                } else {
                    strcat(result, "0");
                }
            } else {
                strcat(result, "0");
            }
            strcat(result, ";\n");
            sprintf(result + strlen(result), "    printf(\"%%s = %%f\\n\", \"%s\", %s);\n", 
                    ast->data.variable_declaration.identifier, ast->data.variable_declaration.identifier);
            break;
            
        default:
            strcat(result, "    // Unknown AST node type\n");
            break;
    }
    
    strcat(result, "    return 0;\n");
    strcat(result, "}\n");
    
    return result;
}
