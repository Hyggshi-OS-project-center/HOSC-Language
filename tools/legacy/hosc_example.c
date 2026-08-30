/*
 * File: tools\legacy\hosc_example.c
 * Purpose: HOSC source file.
 */

// hosc_example.c - HOSC Library Example
//
// Demonstrates how to use the HOSC library to parse and execute HOSC code.

#include "hosc_lib.h"
#include "ast.h"
#include "parser.h"
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Simple example using the HOSC library
int main() {
    printf("=== HOSC Library Example ===\n\n");
    
    // Example HOSC source code
    const char* hosc_source = "let x = 42; let y = 3.14;";
    
    printf("HOSC Source: %s\n\n", hosc_source);
    
    // Parse the HOSC source
    ASTNode* ast = parser_parse(hosc_source);
    if (!ast) {
        printf("Error: Failed to parse HOSC source\n");
        return 1;
    }
    
    printf("Successfully parsed HOSC source!\n");
    
    // Execute the AST
    printf("Executing AST...\n");
    runtime_execute(ast);
    
    printf("\nExecution completed!\n");
    
    // Cleanup
    free_ast(ast);
    
    return 0;
}