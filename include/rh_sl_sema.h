#ifndef RH_SL_SEMA_H
#define RH_SL_SEMA_H

/*
 * RSL Semantic Analysis
 *
 * Walks the AST and:
 *  1. Resolves variable references (symbol table with nested scopes)
 *  2. Type-checks expressions and inserts implicit promotions
 *  3. Validates illuminance/illuminate/solar usage by shader type
 *  4. Resolves function calls to built-ins or user-defined functions
 *  5. Annotates each expression node with its resolved_type
 */

#include "rh_sl_ast.h"

#define SL_MAX_SYMBOLS 256
#define SL_MAX_SCOPES  16

typedef struct {
    char name[SL_MAX_NAME];
    RhSLType type;
    RhSLStorage storage;
    int is_output;
    int is_builtin;       /* 1 if it's a built-in global (P, N, Cs, etc.) */
    int is_writable;      /* 1 if read-write, 0 if read-only */
    int scope_depth;      /* Scope depth where declared */
} RhSLSymbol;

typedef struct {
    /* Symbol table (flat array with scope tracking) */
    RhSLSymbol symbols[SL_MAX_SYMBOLS];
    int num_symbols;

    /* Scope stack (indices into symbols[] marking scope boundaries) */
    int scope_starts[SL_MAX_SCOPES];
    int scope_depth;

    /* Shader context */
    RhSLShaderType shader_type;

    /* User-defined functions from the AST */
    RhSLNode* functions;

    /* Current context for validation */
    RhSLType current_return_type;  /* SL_TYPE_VOID in shader body */
    int in_loop;                   /* Depth counter for break/continue */
    int in_illuminance;            /* Inside illuminance body */

    /* Errors */
    char errors[32][256];
    int num_errors;
} RhSLSema;

/* Initialize sema state (zero everything). */
void rh_sl_sema_init(RhSLSema* sema);

/* Run semantic analysis on a parsed shader AST.
 * Returns 0 on success, -1 if there were errors.
 * Errors are stored in sema->errors[]. */
int rh_sl_sema_analyze(RhSLSema* sema, RhSLNode* shader);

#endif /* RH_SL_SEMA_H */
