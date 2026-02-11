#ifndef RH_SL_CODEGEN_H
#define RH_SL_CODEGEN_H

/*
 * RSL Bytecode Code Generator
 *
 * Transforms a type-checked AST (from sema) into an RhSLProgram
 * containing executable VM bytecode.
 */

#include "rh_sl_ast.h"
#include "rh_sl_vm.h"

#define RH_SL_CODEGEN_MAX_ERRORS 32

typedef struct {
    char errors[RH_SL_CODEGEN_MAX_ERRORS][256];
    int num_errors;
} RhSLCodegenErrors;

/*
 * Compile a type-checked shader AST into a VM program.
 *
 * Takes ownership of nothing -- the AST can be freed independently.
 * Returns a newly allocated RhSLProgram on success, NULL on error.
 * If err_out is non-NULL, diagnostics are stored there.
 */
RhSLProgram* rh_sl_codegen(const RhSLNode* shader, RhSLCodegenErrors* err_out);

#endif /* RH_SL_CODEGEN_H */
