#define _POSIX_C_SOURCE 200809L
#include "rh_sl_codegen.h"
#include "rh_sl_opcodes.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdarg.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Safe string copy (no strncpy truncation warnings) */
static void sl_strcpy(char* dst, size_t dstsz, const char* src) {
    size_t len = strlen(src);
    if (len >= dstsz) len = dstsz - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* ------------------------------------------------------------------ */
/*  Internal data structures                                           */
/* ------------------------------------------------------------------ */

#define MAX_CODE     4096
#define MAX_CONSTS   512
#define MAX_STRINGS  64
#define MAX_SYMBOLS  256
#define MAX_LABELS   256
#define MAX_PATCHES  512
#define MAX_LOOPS    16
#define MAX_FUNCS    32

typedef struct {
    char name[SL_MAX_NAME];
    RhSLType type;
    int reg;           /* first register for this symbol */
    int scope_depth;
} CGSymbol;

typedef struct {
    int break_label;
    int continue_label;
} LoopContext;

typedef struct {
    char name[SL_MAX_NAME];
    int label;         /* label for the function body */
    int return_reg;    /* register for return value */
    RhSLType return_type;
} FuncEntry;

typedef struct {
    /* Code buffer */
    uint64_t code[MAX_CODE];
    int code_len;

    /* Constant pool */
    float consts[MAX_CONSTS];
    int const_count;

    /* String table */
    char* strings[MAX_STRINGS];
    int string_count;

    /* Register allocator */
    int next_reg;
    int max_reg;

    /* Symbol table with scope */
    CGSymbol symbols[MAX_SYMBOLS];
    int num_symbols;
    int scope_starts[16];
    int scope_depth;

    /* Saved reg watermarks per scope (for register reuse) */
    int scope_regs[16];

    /* Labels and jump patches */
    int label_addrs[MAX_LABELS];
    int num_labels;

    int patch_label[MAX_PATCHES];  /* which label the patch references */
    int patch_addr[MAX_PATCHES];   /* which code address to patch */
    int num_patches;

    /* Loop context stack */
    LoopContext loops[MAX_LOOPS];
    int loop_depth;

    /* User function entries */
    FuncEntry funcs[MAX_FUNCS];
    int num_funcs;

    /* Error reporting */
    RhSLCodegenErrors* err;

    /* Shader type */
    RhSLShaderType shader_type;

    /* String param tracking: maps param name -> string table index */
    char  string_param_names[RH_SL_MAX_PARAMS][SL_MAX_NAME];
    int   string_param_str_idx[RH_SL_MAX_PARAMS];
    int   num_string_params;
} CodegenState;

/* ------------------------------------------------------------------ */
/*  Error reporting                                                    */
/* ------------------------------------------------------------------ */

static void cg_error(CodegenState* cg, int line, const char* fmt, ...) {
    if (!cg->err) return;
    if (cg->err->num_errors >= RH_SL_CODEGEN_MAX_ERRORS) return;
    char* dst = cg->err->errors[cg->err->num_errors];
    int off = snprintf(dst, 256, "codegen line %d: ", line);
    if (off < 0) off = 0;
    if (off < 256) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(dst + off, (size_t)(256 - off), fmt, ap);
        va_end(ap);
    }
    cg->err->num_errors++;
}

/* ------------------------------------------------------------------ */
/*  Code emission                                                      */
/* ------------------------------------------------------------------ */

static void emit(CodegenState* cg, uint64_t instr) {
    if (cg->code_len < MAX_CODE)
        cg->code[cg->code_len++] = instr;
}

static int current_addr(CodegenState* cg) {
    return cg->code_len;
}

/* ------------------------------------------------------------------ */
/*  Label / jump patch management                                      */
/* ------------------------------------------------------------------ */

static int label_new(CodegenState* cg) {
    if (cg->num_labels >= MAX_LABELS) return 0;
    int id = cg->num_labels++;
    cg->label_addrs[id] = -1; /* unresolved */
    return id;
}

static void label_define(CodegenState* cg, int label) {
    if (label >= 0 && label < cg->num_labels)
        cg->label_addrs[label] = current_addr(cg);
}

/* Emit a jump instruction whose destination is a label (possibly forward).
 * The actual address will be patched in resolve_jumps(). */
static void emit_jump(CodegenState* cg, uint8_t op, int label, uint16_t src1) {
    int addr = current_addr(cg);
    /* If label is already defined, use its address directly */
    if (label >= 0 && label < cg->num_labels && cg->label_addrs[label] >= 0) {
        emit(cg, RH_SL_INSTR(op, (uint16_t)cg->label_addrs[label], src1, 0));
    } else {
        /* Forward reference -- emit with placeholder 0, patch later */
        emit(cg, RH_SL_INSTR(op, 0, src1, 0));
        if (cg->num_patches < MAX_PATCHES) {
            cg->patch_label[cg->num_patches] = label;
            cg->patch_addr[cg->num_patches] = addr;
            cg->num_patches++;
        }
    }
}

static void resolve_jumps(CodegenState* cg) {
    for (int i = 0; i < cg->num_patches; i++) {
        int label = cg->patch_label[i];
        int addr = cg->patch_addr[i];
        if (label < 0 || label >= cg->num_labels) continue;
        int target = cg->label_addrs[label];
        if (target < 0) continue; /* unresolved -- shouldn't happen */

        /* Rebuild instruction with correct dst */
        uint64_t instr = cg->code[addr];
        uint8_t op = RH_SL_DECODE_OP(instr);
        uint8_t flags = RH_SL_DECODE_FLAGS(instr);
        uint16_t src1 = RH_SL_DECODE_SRC1(instr);
        uint16_t src2 = RH_SL_DECODE_SRC2(instr);
        cg->code[addr] = RH_SL_ENCODE(op, flags, (uint16_t)target, src1, src2);
    }
}

/* ------------------------------------------------------------------ */
/*  Register allocation                                                */
/* ------------------------------------------------------------------ */

static int alloc_reg(CodegenState* cg, int count) {
    int r = cg->next_reg;
    cg->next_reg += count;
    if (cg->next_reg > cg->max_reg)
        cg->max_reg = cg->next_reg;
    return r;
}

static int type_reg_count(RhSLType type) {
    switch (type) {
    case SL_TYPE_FLOAT:  return 1;
    case SL_TYPE_COLOR:
    case SL_TYPE_POINT:
    case SL_TYPE_VECTOR:
    case SL_TYPE_NORMAL: return 3;
    default:             return 1;
    }
}

static int is_tuple_type(RhSLType t) {
    return t == SL_TYPE_COLOR || t == SL_TYPE_POINT ||
           t == SL_TYPE_VECTOR || t == SL_TYPE_NORMAL;
}

/* ------------------------------------------------------------------ */
/*  Scope management                                                   */
/* ------------------------------------------------------------------ */

static void push_scope(CodegenState* cg) {
    if (cg->scope_depth >= 16) return;
    cg->scope_starts[cg->scope_depth] = cg->num_symbols;
    cg->scope_regs[cg->scope_depth] = cg->next_reg;
    cg->scope_depth++;
}

static void pop_scope(CodegenState* cg) {
    if (cg->scope_depth <= 0) return;
    cg->scope_depth--;
    cg->num_symbols = cg->scope_starts[cg->scope_depth];
    cg->next_reg = cg->scope_regs[cg->scope_depth];
}

/* ------------------------------------------------------------------ */
/*  Symbol table                                                       */
/* ------------------------------------------------------------------ */

static CGSymbol* cg_declare(CodegenState* cg, const char* name, RhSLType type, int reg) {
    if (cg->num_symbols >= MAX_SYMBOLS) return NULL;
    CGSymbol* sym = &cg->symbols[cg->num_symbols++];
    sl_strcpy(sym->name, sizeof(sym->name), name);
    sym->type = type;
    sym->reg = reg;
    sym->scope_depth = cg->scope_depth;
    return sym;
}

static CGSymbol* cg_lookup(CodegenState* cg, const char* name) {
    for (int i = cg->num_symbols - 1; i >= 0; i--) {
        if (strcmp(cg->symbols[i].name, name) == 0)
            return &cg->symbols[i];
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Builtin name -> fixed register mapping                             */
/* ------------------------------------------------------------------ */

typedef struct {
    const char* name;
    int reg;
    RhSLType type;
} BuiltinReg;

static const BuiltinReg builtin_regs[] = {
    {"P",    R_P,     SL_TYPE_POINT},
    {"N",    R_N,     SL_TYPE_NORMAL},
    {"Ng",   R_NG,    SL_TYPE_NORMAL},
    {"I",    R_I,     SL_TYPE_VECTOR},
    {"E",    R_E,     SL_TYPE_POINT},
    {"Cs",   R_CS,    SL_TYPE_COLOR},
    {"Os",   R_OS,    SL_TYPE_COLOR},
    {"Ci",   R_CI,    SL_TYPE_COLOR},
    {"Oi",   R_OI,    SL_TYPE_COLOR},
    {"s",    R_S,     SL_TYPE_FLOAT},
    {"t",    R_T,     SL_TYPE_FLOAT},
    {"u",    R_U,     SL_TYPE_FLOAT},
    {"v",    R_V,     SL_TYPE_FLOAT},
    {"du",   R_DU,    SL_TYPE_FLOAT},
    {"dv",   R_DV,    SL_TYPE_FLOAT},
    {"L",    R_L,     SL_TYPE_VECTOR},
    {"Cl",   R_CL,    SL_TYPE_COLOR},
    {"Pw",   R_PW,    SL_TYPE_POINT},
    {"Ps",   R_PS,    SL_TYPE_POINT},
    {"dPdu", R_DPDU,  SL_TYPE_VECTOR},
    {"dPdv", R_DPDV,  SL_TYPE_VECTOR},
    {NULL,   0,       SL_TYPE_VOID}
};

static void populate_builtin_symbols(CodegenState* cg) {
    for (int i = 0; builtin_regs[i].name; i++) {
        cg_declare(cg, builtin_regs[i].name,
                   builtin_regs[i].type, builtin_regs[i].reg);
    }
}

/* ------------------------------------------------------------------ */
/*  Constant pool                                                      */
/* ------------------------------------------------------------------ */

static int add_const(CodegenState* cg, float val) {
    /* Deduplicate */
    for (int i = 0; i < cg->const_count; i++) {
        if (cg->consts[i] == val)
            return i;
    }
    if (cg->const_count >= MAX_CONSTS) return 0;
    int idx = cg->const_count++;
    cg->consts[idx] = val;
    return idx;
}

/* ------------------------------------------------------------------ */
/*  String table                                                       */
/* ------------------------------------------------------------------ */

static int add_string(CodegenState* cg, const char* str) {
    /* Deduplicate */
    for (int i = 0; i < cg->string_count; i++) {
        if (strcmp(cg->strings[i], str) == 0)
            return i;
    }
    if (cg->string_count >= MAX_STRINGS) return 0;
    int idx = cg->string_count++;
    size_t len = strlen(str) + 1;
    cg->strings[idx] = malloc(len);
    memcpy(cg->strings[idx], str, len);
    return idx;
}

/* ------------------------------------------------------------------ */
/*  Forward declarations                                               */
/* ------------------------------------------------------------------ */

static int emit_expr(CodegenState* cg, const RhSLNode* node);
static void emit_stmt(CodegenState* cg, const RhSLNode* node);

/* ------------------------------------------------------------------ */
/*  Expression code generation                                         */
/* ------------------------------------------------------------------ */

/* Count arguments in a linked list */
static int count_args(const RhSLNode* args) {
    int n = 0;
    for (const RhSLNode* a = args; a; a = a->next) n++;
    return n;
}

/* Emit code for a builtin function call, return register with result */
static int emit_builtin_call(CodegenState* cg, const RhSLNode* node) {
    const char* name = node->u.call.name;
    int nargs = count_args(node->u.call.args);

    /* Evaluate arguments into registers */
    int arg_regs[6];
    int arg_idx = 0;
    for (const RhSLNode* a = node->u.call.args; a && arg_idx < 6; a = a->next) {
        arg_regs[arg_idx++] = emit_expr(cg, a);
    }

    /* --- 1-arg float -> float functions --- */
    if (nargs == 1 && node->resolved_type == SL_TYPE_FLOAT) {
        uint8_t op = 0;
        if      (strcmp(name, "abs")   == 0) op = OP_FABS;
        else if (strcmp(name, "ceil")  == 0) op = OP_FCEIL;
        else if (strcmp(name, "floor") == 0) op = OP_FFLOOR;
        else if (strcmp(name, "round") == 0) op = OP_FROUND;
        else if (strcmp(name, "sqrt")  == 0) op = OP_FSQRT;
        else if (strcmp(name, "exp")   == 0) op = OP_FEXP;
        else if (strcmp(name, "log")   == 0) op = OP_FLOG;
        else if (strcmp(name, "sign")  == 0) op = OP_FSIGN;
        else if (strcmp(name, "sin")   == 0) op = OP_FSIN;
        else if (strcmp(name, "cos")   == 0) op = OP_FCOS;
        else if (strcmp(name, "tan")   == 0) op = OP_FTAN;
        else if (strcmp(name, "asin")  == 0) op = OP_FASIN;
        else if (strcmp(name, "acos")  == 0) op = OP_FACOS;
        else if (strcmp(name, "atan")  == 0) op = OP_FATAN;

        if (op != 0) {
            int dst = alloc_reg(cg, 1);
            emit(cg, RH_SL_INSTR(op, (uint16_t)dst, (uint16_t)arg_regs[0], 0));
            return dst;
        }

        /* radians(x) = x * (PI/180) */
        if (strcmp(name, "radians") == 0) {
            int dst = alloc_reg(cg, 1);
            int ci = add_const(cg, (float)(M_PI / 180.0));
            int tmp = alloc_reg(cg, 1);
            emit(cg, RH_SL_INSTR(OP_FCONST, (uint16_t)tmp, (uint16_t)ci, 0));
            emit(cg, RH_SL_INSTR(OP_FMUL, (uint16_t)dst, (uint16_t)arg_regs[0], (uint16_t)tmp));
            return dst;
        }

        /* degrees(x) = x * (180/PI) */
        if (strcmp(name, "degrees") == 0) {
            int dst = alloc_reg(cg, 1);
            int ci = add_const(cg, (float)(180.0 / M_PI));
            int tmp = alloc_reg(cg, 1);
            emit(cg, RH_SL_INSTR(OP_FCONST, (uint16_t)tmp, (uint16_t)ci, 0));
            emit(cg, RH_SL_INSTR(OP_FMUL, (uint16_t)dst, (uint16_t)arg_regs[0], (uint16_t)tmp));
            return dst;
        }

        /* length(tuple) -> float */
        if (strcmp(name, "length") == 0) {
            int dst = alloc_reg(cg, 1);
            emit(cg, RH_SL_INSTR(OP_LENGTH, (uint16_t)dst, (uint16_t)arg_regs[0], 0));
            return dst;
        }

        /* area(P) -> float (stub) */
        if (strcmp(name, "area") == 0) {
            int dst = alloc_reg(cg, 1);
            emit(cg, RH_SL_INSTR(OP_AREA, (uint16_t)dst, (uint16_t)arg_regs[0], 0));
            return dst;
        }

        /* xcomp/ycomp/zcomp */
        if (strcmp(name, "xcomp") == 0) {
            int dst = alloc_reg(cg, 1);
            emit(cg, RH_SL_INSTR_F(OP_VCOMP, 0, (uint16_t)dst, (uint16_t)arg_regs[0], 0));
            return dst;
        }
        if (strcmp(name, "ycomp") == 0) {
            int dst = alloc_reg(cg, 1);
            emit(cg, RH_SL_INSTR_F(OP_VCOMP, 1, (uint16_t)dst, (uint16_t)arg_regs[0], 0));
            return dst;
        }
        if (strcmp(name, "zcomp") == 0) {
            int dst = alloc_reg(cg, 1);
            emit(cg, RH_SL_INSTR_F(OP_VCOMP, 2, (uint16_t)dst, (uint16_t)arg_regs[0], 0));
            return dst;
        }

        /* noise(float) or noise(point) */
        if (strcmp(name, "noise") == 0) {
            const RhSLNode* arg = node->u.call.args;
            if (arg && is_tuple_type(arg->resolved_type)) {
                int dst = alloc_reg(cg, 1);
                emit(cg, RH_SL_INSTR(OP_NOISE3, (uint16_t)dst, (uint16_t)arg_regs[0], 0));
                return dst;
            }
            int dst = alloc_reg(cg, 1);
            emit(cg, RH_SL_INSTR(OP_NOISE1, (uint16_t)dst, (uint16_t)arg_regs[0], 0));
            return dst;
        }

        /* cellnoise(float) */
        if (strcmp(name, "cellnoise") == 0) {
            int dst = alloc_reg(cg, 1);
            emit(cg, RH_SL_INSTR(OP_CELLNOISE, (uint16_t)dst, (uint16_t)arg_regs[0], 0));
            return dst;
        }
    }

    /* --- 1-arg tuple -> tuple functions --- */
    if (nargs == 1 && is_tuple_type(node->resolved_type)) {
        if (strcmp(name, "normalize") == 0) {
            int dst = alloc_reg(cg, 3);
            emit(cg, RH_SL_INSTR(OP_NORMALIZE, (uint16_t)dst, (uint16_t)arg_regs[0], 0));
            return dst;
        }
        if (strcmp(name, "calculatenormal") == 0) {
            int dst = alloc_reg(cg, 3);
            emit(cg, RH_SL_INSTR(OP_CALCNORMAL, (uint16_t)dst, (uint16_t)arg_regs[0], 0));
            return dst;
        }
    }

    /* --- 2-arg float -> float functions --- */
    if (nargs == 2 && node->resolved_type == SL_TYPE_FLOAT) {
        uint8_t op = 0;
        if      (strcmp(name, "min")  == 0) op = OP_FMIN;
        else if (strcmp(name, "max")  == 0) op = OP_FMAX;
        else if (strcmp(name, "mod")  == 0) op = OP_FMOD;
        else if (strcmp(name, "pow")  == 0) op = OP_FPOW;
        else if (strcmp(name, "atan") == 0) op = OP_FATAN2;

        if (op != 0) {
            int dst = alloc_reg(cg, 1);
            emit(cg, RH_SL_INSTR(op, (uint16_t)dst, (uint16_t)arg_regs[0], (uint16_t)arg_regs[1]));
            return dst;
        }

        /* dot(a, b) */
        if (strcmp(name, "dot") == 0) {
            int dst = alloc_reg(cg, 1);
            emit(cg, RH_SL_INSTR(OP_DOT, (uint16_t)dst, (uint16_t)arg_regs[0], (uint16_t)arg_regs[1]));
            return dst;
        }

        /* distance(a, b) */
        if (strcmp(name, "distance") == 0) {
            int dst = alloc_reg(cg, 1);
            emit(cg, RH_SL_INSTR(OP_DISTANCE, (uint16_t)dst, (uint16_t)arg_regs[0], (uint16_t)arg_regs[1]));
            return dst;
        }

        /* noise(float, float) */
        if (strcmp(name, "noise") == 0) {
            int dst = alloc_reg(cg, 1);
            emit(cg, RH_SL_INSTR(OP_NOISE2, (uint16_t)dst, (uint16_t)arg_regs[0], (uint16_t)arg_regs[1]));
            return dst;
        }

        /* pnoise(float, float) */
        if (strcmp(name, "pnoise") == 0) {
            int dst = alloc_reg(cg, 1);
            emit(cg, RH_SL_INSTR(OP_PNOISE, (uint16_t)dst, (uint16_t)arg_regs[0], (uint16_t)arg_regs[1]));
            return dst;
        }

        /* step(edge, x) = x < edge ? 0 : 1 */
        if (strcmp(name, "step") == 0) {
            int dst = alloc_reg(cg, 1);
            emit(cg, RH_SL_INSTR(OP_FGE, (uint16_t)dst, (uint16_t)arg_regs[1], (uint16_t)arg_regs[0]));
            return dst;
        }

        /* comp(color, index) -- treat as VCOMP with variable index */
        if (strcmp(name, "comp") == 0) {
            /* Simplified: just use xcomp for now (comp with float index
             * would need runtime dispatch, but most usage is with literal 0/1/2) */
            int dst = alloc_reg(cg, 1);
            emit(cg, RH_SL_INSTR_F(OP_VCOMP, 0, (uint16_t)dst, (uint16_t)arg_regs[0], 0));
            return dst;
        }
    }

    /* --- 2-arg tuple -> tuple functions --- */
    if (nargs == 2 && is_tuple_type(node->resolved_type)) {
        if (strcmp(name, "cross") == 0) {
            int dst = alloc_reg(cg, 3);
            emit(cg, RH_SL_INSTR(OP_CROSS, (uint16_t)dst, (uint16_t)arg_regs[0], (uint16_t)arg_regs[1]));
            return dst;
        }
        if (strcmp(name, "faceforward") == 0) {
            int dst = alloc_reg(cg, 3);
            emit(cg, RH_SL_INSTR(OP_FACEFORWARD, (uint16_t)dst, (uint16_t)arg_regs[0], (uint16_t)arg_regs[1]));
            return dst;
        }
        if (strcmp(name, "reflect") == 0) {
            int dst = alloc_reg(cg, 3);
            emit(cg, RH_SL_INSTR(OP_REFLECT, (uint16_t)dst, (uint16_t)arg_regs[0], (uint16_t)arg_regs[1]));
            return dst;
        }
    }

    /* setxcomp/setycomp/setzcomp (2 args, returns nothing but modifies arg0 in place) */
    if (nargs == 2 && strcmp(name, "setxcomp") == 0) {
        emit(cg, RH_SL_INSTR_F(OP_VSETCOMP, 0, (uint16_t)arg_regs[0], (uint16_t)arg_regs[1], 0));
        return arg_regs[0];
    }
    if (nargs == 2 && strcmp(name, "setycomp") == 0) {
        emit(cg, RH_SL_INSTR_F(OP_VSETCOMP, 1, (uint16_t)arg_regs[0], (uint16_t)arg_regs[1], 0));
        return arg_regs[0];
    }
    if (nargs == 2 && strcmp(name, "setzcomp") == 0) {
        emit(cg, RH_SL_INSTR_F(OP_VSETCOMP, 2, (uint16_t)arg_regs[0], (uint16_t)arg_regs[1], 0));
        return arg_regs[0];
    }

    /* setcomp(color, index, value) -- 3 args */
    if (nargs == 3 && strcmp(name, "setcomp") == 0) {
        /* Simplified: treat index as compile-time 0 */
        emit(cg, RH_SL_INSTR_F(OP_VSETCOMP, 0, (uint16_t)arg_regs[0], (uint16_t)arg_regs[2], 0));
        return arg_regs[0];
    }

    /* --- 0-arg lighting functions --- */
    if (nargs == 0 && strcmp(name, "ambient") == 0) {
        int dst = alloc_reg(cg, 3);
        emit(cg, RH_SL_INSTR(OP_AMBIENT, (uint16_t)dst, 0, 0));
        return dst;
    }

    /* --- 1-arg lighting: diffuse(Nf) --- */
    if (nargs == 1 && strcmp(name, "diffuse") == 0) {
        int dst = alloc_reg(cg, 3);
        emit(cg, RH_SL_INSTR(OP_DIFFUSE, (uint16_t)dst, (uint16_t)arg_regs[0], 0));
        return dst;
    }

    /* --- specular(Nf, V, roughness) --- */
    /* VM's OP_SPECULAR takes src1=Nf, src2=roughness. V is computed internally. */
    if (nargs == 3 && strcmp(name, "specular") == 0) {
        int dst = alloc_reg(cg, 3);
        /* arg_regs[0]=Nf, arg_regs[1]=V (discarded), arg_regs[2]=roughness */
        emit(cg, RH_SL_INSTR(OP_SPECULAR, (uint16_t)dst, (uint16_t)arg_regs[0], (uint16_t)arg_regs[2]));
        return dst;
    }

    /* phong -- same as specular for our VM */
    if (nargs == 3 && strcmp(name, "phong") == 0) {
        int dst = alloc_reg(cg, 3);
        emit(cg, RH_SL_INSTR(OP_SPECULAR, (uint16_t)dst, (uint16_t)arg_regs[0], (uint16_t)arg_regs[2]));
        return dst;
    }

    /* --- clamp(x, lo, hi) = max(lo, min(hi, x)) --- */
    if (nargs == 3 && strcmp(name, "clamp") == 0) {
        int t1 = alloc_reg(cg, 1);
        int dst = alloc_reg(cg, 1);
        emit(cg, RH_SL_INSTR(OP_FMIN, (uint16_t)t1, (uint16_t)arg_regs[2], (uint16_t)arg_regs[0]));
        emit(cg, RH_SL_INSTR(OP_FMAX, (uint16_t)dst, (uint16_t)arg_regs[1], (uint16_t)t1));
        return dst;
    }

    /* --- smoothstep(edge0, edge1, x) --- */
    if (nargs == 3 && strcmp(name, "smoothstep") == 0) {
        /* t = clamp((x - edge0) / (edge1 - edge0), 0, 1)
         * result = t * t * (3 - 2*t) */
        int t1 = alloc_reg(cg, 1);
        int t2 = alloc_reg(cg, 1);
        int t3 = alloc_reg(cg, 1);
        int t4 = alloc_reg(cg, 1);
        int c0 = add_const(cg, 0.0f);
        int c1 = add_const(cg, 1.0f);
        int c3 = add_const(cg, 3.0f);
        int c2 = add_const(cg, 2.0f);
        int r0 = alloc_reg(cg, 1);
        int r1 = alloc_reg(cg, 1);
        int r3 = alloc_reg(cg, 1);
        int r2 = alloc_reg(cg, 1);
        emit(cg, RH_SL_INSTR(OP_FCONST, (uint16_t)r0, (uint16_t)c0, 0));
        emit(cg, RH_SL_INSTR(OP_FCONST, (uint16_t)r1, (uint16_t)c1, 0));
        emit(cg, RH_SL_INSTR(OP_FCONST, (uint16_t)r3, (uint16_t)c3, 0));
        emit(cg, RH_SL_INSTR(OP_FCONST, (uint16_t)r2, (uint16_t)c2, 0));
        /* t1 = x - edge0 */
        emit(cg, RH_SL_INSTR(OP_FSUB, (uint16_t)t1, (uint16_t)arg_regs[2], (uint16_t)arg_regs[0]));
        /* t2 = edge1 - edge0 */
        emit(cg, RH_SL_INSTR(OP_FSUB, (uint16_t)t2, (uint16_t)arg_regs[1], (uint16_t)arg_regs[0]));
        /* t3 = t1 / t2 */
        emit(cg, RH_SL_INSTR(OP_FDIV, (uint16_t)t3, (uint16_t)t1, (uint16_t)t2));
        /* clamp: t4 = max(0, min(1, t3)) */
        emit(cg, RH_SL_INSTR(OP_FMIN, (uint16_t)t4, (uint16_t)r1, (uint16_t)t3));
        emit(cg, RH_SL_INSTR(OP_FMAX, (uint16_t)t4, (uint16_t)r0, (uint16_t)t4));
        /* result = t4*t4*(3-2*t4) */
        emit(cg, RH_SL_INSTR(OP_FMUL, (uint16_t)t1, (uint16_t)r2, (uint16_t)t4));  /* t1 = 2*t4 */
        emit(cg, RH_SL_INSTR(OP_FSUB, (uint16_t)t2, (uint16_t)r3, (uint16_t)t1));  /* t2 = 3-2*t4 */
        emit(cg, RH_SL_INSTR(OP_FMUL, (uint16_t)t3, (uint16_t)t4, (uint16_t)t4));  /* t3 = t4*t4 */
        emit(cg, RH_SL_INSTR(OP_FMUL, (uint16_t)t3, (uint16_t)t3, (uint16_t)t2));  /* t3 = t3*t2 */
        return t3;
    }

    /* --- mix(a, b, t) = a*(1-t) + b*t --- */
    if (nargs == 3 && strcmp(name, "mix") == 0) {
        if (node->resolved_type == SL_TYPE_FLOAT) {
            /* float mix */
            int c1 = add_const(cg, 1.0f);
            int r1 = alloc_reg(cg, 1);
            int t1 = alloc_reg(cg, 1);
            int t2 = alloc_reg(cg, 1);
            int dst = alloc_reg(cg, 1);
            emit(cg, RH_SL_INSTR(OP_FCONST, (uint16_t)r1, (uint16_t)c1, 0));
            emit(cg, RH_SL_INSTR(OP_FSUB, (uint16_t)t1, (uint16_t)r1, (uint16_t)arg_regs[2])); /* 1-t */
            emit(cg, RH_SL_INSTR(OP_FMUL, (uint16_t)t1, (uint16_t)arg_regs[0], (uint16_t)t1)); /* a*(1-t) */
            emit(cg, RH_SL_INSTR(OP_FMUL, (uint16_t)t2, (uint16_t)arg_regs[1], (uint16_t)arg_regs[2])); /* b*t */
            emit(cg, RH_SL_INSTR(OP_FADD, (uint16_t)dst, (uint16_t)t1, (uint16_t)t2));
            return dst;
        } else {
            /* color/tuple mix: a*(1-t) + b*t using vector ops */
            int c1 = add_const(cg, 1.0f);
            int r1 = alloc_reg(cg, 1);
            int t1 = alloc_reg(cg, 1);
            int v1 = alloc_reg(cg, 3);
            int v2 = alloc_reg(cg, 3);
            int dst = alloc_reg(cg, 3);
            emit(cg, RH_SL_INSTR(OP_FCONST, (uint16_t)r1, (uint16_t)c1, 0));
            emit(cg, RH_SL_INSTR(OP_FSUB, (uint16_t)t1, (uint16_t)r1, (uint16_t)arg_regs[2])); /* 1-t */
            emit(cg, RH_SL_INSTR(OP_VSMUL, (uint16_t)v1, (uint16_t)arg_regs[0], (uint16_t)t1)); /* a*(1-t) */
            emit(cg, RH_SL_INSTR(OP_VSMUL, (uint16_t)v2, (uint16_t)arg_regs[1], (uint16_t)arg_regs[2])); /* b*t */
            emit(cg, RH_SL_INSTR(OP_VADD, (uint16_t)dst, (uint16_t)v1, (uint16_t)v2));
            return dst;
        }
    }

    /* --- texture(name, s, t) --- */
    if (strcmp(name, "texture") == 0 && nargs == 3) {
        /* Determine string table index from first arg */
        int str_idx = 0;
        const RhSLNode* str_arg = node->u.call.args;
        if (str_arg && str_arg->node_type == SL_NODE_STRING_LIT) {
            str_idx = add_string(cg, str_arg->u.string_lit.value);
        } else if (str_arg && str_arg->node_type == SL_NODE_IDENT &&
                   str_arg->resolved_type == SL_TYPE_STRING) {
            /* String variable (param) -- look up its string table index */
            for (int si = 0; si < cg->num_string_params; si++) {
                if (strcmp(cg->string_param_names[si],
                           str_arg->u.ident.name) == 0) {
                    str_idx = cg->string_param_str_idx[si];
                    break;
                }
            }
        }
        /* Pack s,t into consecutive temp registers */
        int st = alloc_reg(cg, 2);
        emit(cg, RH_SL_INSTR(OP_FMOV, (uint16_t)st, (uint16_t)arg_regs[1], 0));
        emit(cg, RH_SL_INSTR(OP_FMOV, (uint16_t)(st + 1), (uint16_t)arg_regs[2], 0));
        int dst = alloc_reg(cg, 3);
        emit(cg, RH_SL_INSTR(OP_TEXTURE, (uint16_t)dst, (uint16_t)st, (uint16_t)str_idx));
        return dst;
    }

    /* --- shadow(name, P) --- */
    if (strcmp(name, "shadow") == 0 && nargs == 2) {
        int str_idx = 0;
        const RhSLNode* str_arg = node->u.call.args;
        if (str_arg && str_arg->node_type == SL_NODE_STRING_LIT) {
            str_idx = add_string(cg, str_arg->u.string_lit.value);
        } else if (str_arg && str_arg->node_type == SL_NODE_IDENT &&
                   str_arg->resolved_type == SL_TYPE_STRING) {
            for (int si = 0; si < cg->num_string_params; si++) {
                if (strcmp(cg->string_param_names[si],
                           str_arg->u.ident.name) == 0) {
                    str_idx = cg->string_param_str_idx[si];
                    break;
                }
            }
        }
        int dst = alloc_reg(cg, 1);
        emit(cg, RH_SL_INSTR(OP_SHADOW, (uint16_t)dst, (uint16_t)arg_regs[1], (uint16_t)str_idx));
        return dst;
    }

    /* --- environment(name, dir) --- */
    if (strcmp(name, "environment") == 0 && nargs == 2) {
        int str_idx = 0;
        const RhSLNode* str_arg = node->u.call.args;
        if (str_arg && str_arg->node_type == SL_NODE_STRING_LIT) {
            str_idx = add_string(cg, str_arg->u.string_lit.value);
        }
        int dst = alloc_reg(cg, 3);
        emit(cg, RH_SL_INSTR(OP_ENVMAP, (uint16_t)dst, (uint16_t)arg_regs[1], (uint16_t)str_idx));
        return dst;
    }

    /* --- transform/ntransform/vtransform(space, val) --- */
    if (nargs == 2 && strcmp(name, "transform") == 0) {
        int dst = alloc_reg(cg, 3);
        emit(cg, RH_SL_INSTR(OP_TRANSFORM, (uint16_t)dst, (uint16_t)arg_regs[1], 0));
        return dst;
    }
    if (nargs == 2 && strcmp(name, "ntransform") == 0) {
        int dst = alloc_reg(cg, 3);
        emit(cg, RH_SL_INSTR(OP_NTRANSFORM, (uint16_t)dst, (uint16_t)arg_regs[1], 0));
        return dst;
    }
    if (nargs == 2 && strcmp(name, "vtransform") == 0) {
        int dst = alloc_reg(cg, 3);
        emit(cg, RH_SL_INSTR(OP_VTRANSFORM, (uint16_t)dst, (uint16_t)arg_regs[1], 0));
        return dst;
    }

    /* --- trace(P, dir) -> color --- */
    if (nargs == 2 && strcmp(name, "trace") == 0) {
        /* Stub: return black */
        int dst = alloc_reg(cg, 3);
        int ci = add_const(cg, 0.0f);
        emit(cg, RH_SL_INSTR(OP_FCONST, (uint16_t)dst, (uint16_t)ci, 0));
        emit(cg, RH_SL_INSTR(OP_FCONST, (uint16_t)(dst + 1), (uint16_t)ci, 0));
        emit(cg, RH_SL_INSTR(OP_FCONST, (uint16_t)(dst + 2), (uint16_t)ci, 0));
        return dst;
    }

    /* --- printf(format, ...) --- */
    if (strcmp(name, "printf") == 0 && nargs >= 1) {
        const RhSLNode* str_arg = node->u.call.args;
        if (str_arg && str_arg->node_type == SL_NODE_STRING_LIT) {
            int str_idx = add_string(cg, str_arg->u.string_lit.value);
            emit(cg, RH_SL_INSTR(OP_PRINTF, 0, (uint16_t)str_idx, 0));
        }
        return 0; /* void */
    }

    /* --- User-defined function call --- */
    {
        FuncEntry* fe = NULL;
        for (int i = 0; i < cg->num_funcs; i++) {
            if (strcmp(cg->funcs[i].name, name) == 0) {
                fe = &cg->funcs[i];
                break;
            }
        }
        if (fe) {
            /* Copy arguments to function parameter registers.
             * Convention: function params start right after the function's return reg. */
            /* For now: args are already in registers from emit_expr above.
             * We just need to emit OP_CALL to the function's label. */
            emit_jump(cg, OP_CALL, fe->label, 0);

            /* Copy return value from function's return register */
            int nc = type_reg_count(fe->return_type);
            int dst = alloc_reg(cg, nc);
            if (nc == 1) {
                emit(cg, RH_SL_INSTR(OP_FMOV, (uint16_t)dst, (uint16_t)fe->return_reg, 0));
            } else {
                emit(cg, RH_SL_INSTR(OP_VMOV, (uint16_t)dst, (uint16_t)fe->return_reg, 0));
            }
            return dst;
        }
    }

    /* Unknown function -- return 0 */
    cg_error(cg, node->line, "unhandled function '%s'", name);
    return alloc_reg(cg, type_reg_count(node->resolved_type));
}

/* ------------------------------------------------------------------ */
/*  Main expression emitter                                            */
/* ------------------------------------------------------------------ */

static int emit_expr(CodegenState* cg, const RhSLNode* node) {
    if (!node) return 0;

    switch (node->node_type) {

    case SL_NODE_FLOAT_LIT: {
        int dst = alloc_reg(cg, 1);
        int ci = add_const(cg, node->u.float_lit.value);
        emit(cg, RH_SL_INSTR(OP_FCONST, (uint16_t)dst, (uint16_t)ci, 0));
        return dst;
    }

    case SL_NODE_STRING_LIT: {
        /* String literals don't go to registers -- return string table index encoded */
        return add_string(cg, node->u.string_lit.value);
    }

    case SL_NODE_IDENT: {
        CGSymbol* sym = cg_lookup(cg, node->u.ident.name);
        if (sym) return sym->reg;
        cg_error(cg, node->line, "undeclared variable '%s'", node->u.ident.name);
        return 0;
    }

    case SL_NODE_BINOP: {
        int lr = emit_expr(cg, node->u.binop.left);
        int rr = emit_expr(cg, node->u.binop.right);
        RhSLType lt = node->u.binop.left->resolved_type;
        RhSLType rt = node->u.binop.right->resolved_type;
        RhSLType result_type = node->resolved_type;

        /* Dot product */
        if (node->u.binop.op == SL_OP_DOT) {
            int dst = alloc_reg(cg, 1);
            emit(cg, RH_SL_INSTR(OP_DOT, (uint16_t)dst, (uint16_t)lr, (uint16_t)rr));
            return dst;
        }

        /* Cross product */
        if (node->u.binop.op == SL_OP_CROSS) {
            int dst = alloc_reg(cg, 3);
            emit(cg, RH_SL_INSTR(OP_CROSS, (uint16_t)dst, (uint16_t)lr, (uint16_t)rr));
            return dst;
        }

        /* Comparison and logic ops -- always produce float */
        if (node->u.binop.op >= SL_OP_EQ) {
            uint8_t op;
            switch (node->u.binop.op) {
            case SL_OP_EQ:  op = OP_FEQ; break;
            case SL_OP_NE:  op = OP_FNE; break;
            case SL_OP_LT:  op = OP_FLT; break;
            case SL_OP_GT:  op = OP_FGT; break;
            case SL_OP_LE:  op = OP_FLE; break;
            case SL_OP_GE:  op = OP_FGE; break;
            case SL_OP_AND: op = OP_AND; break;
            case SL_OP_OR:  op = OP_OR;  break;
            default:        op = OP_FEQ; break;
            }
            int dst = alloc_reg(cg, 1);
            emit(cg, RH_SL_INSTR(op, (uint16_t)dst, (uint16_t)lr, (uint16_t)rr));
            return dst;
        }

        /* Arithmetic ops */
        int is_lt = is_tuple_type(lt);
        int is_rt = is_tuple_type(rt);

        if (is_lt && is_rt) {
            /* tuple OP tuple */
            uint8_t op;
            switch (node->u.binop.op) {
            case SL_OP_ADD: op = OP_VADD; break;
            case SL_OP_SUB: op = OP_VSUB; break;
            case SL_OP_MUL: op = OP_VMUL; break;
            case SL_OP_DIV: op = OP_VDIV; break;
            default:        op = OP_VADD; break;
            }
            int dst = alloc_reg(cg, 3);
            emit(cg, RH_SL_INSTR(op, (uint16_t)dst, (uint16_t)lr, (uint16_t)rr));
            return dst;
        }

        if (is_lt && rt == SL_TYPE_FLOAT) {
            /* tuple OP float */
            if (node->u.binop.op == SL_OP_MUL) {
                int dst = alloc_reg(cg, 3);
                emit(cg, RH_SL_INSTR(OP_VSMUL, (uint16_t)dst, (uint16_t)lr, (uint16_t)rr));
                return dst;
            }
            if (node->u.binop.op == SL_OP_DIV) {
                int dst = alloc_reg(cg, 3);
                emit(cg, RH_SL_INSTR(OP_VSDIV, (uint16_t)dst, (uint16_t)lr, (uint16_t)rr));
                return dst;
            }
            /* tuple +/- float: promote float to tuple first */
            int promoted = alloc_reg(cg, 3);
            emit(cg, RH_SL_INSTR(OP_FTOV, (uint16_t)promoted, (uint16_t)rr, 0));
            uint8_t op = (node->u.binop.op == SL_OP_ADD) ? OP_VADD : OP_VSUB;
            int dst = alloc_reg(cg, 3);
            emit(cg, RH_SL_INSTR(op, (uint16_t)dst, (uint16_t)lr, (uint16_t)promoted));
            return dst;
        }

        if (lt == SL_TYPE_FLOAT && is_rt) {
            /* float OP tuple */
            if (node->u.binop.op == SL_OP_MUL) {
                int dst = alloc_reg(cg, 3);
                emit(cg, RH_SL_INSTR(OP_VSMUL, (uint16_t)dst, (uint16_t)rr, (uint16_t)lr));
                return dst;
            }
            /* float +/- tuple or float / tuple: promote float first */
            int promoted = alloc_reg(cg, 3);
            emit(cg, RH_SL_INSTR(OP_FTOV, (uint16_t)promoted, (uint16_t)lr, 0));
            uint8_t op;
            switch (node->u.binop.op) {
            case SL_OP_ADD: op = OP_VADD; break;
            case SL_OP_SUB: op = OP_VSUB; break;
            case SL_OP_DIV: op = OP_VDIV; break;
            default:        op = OP_VADD; break;
            }
            int dst = alloc_reg(cg, 3);
            emit(cg, RH_SL_INSTR(op, (uint16_t)dst, (uint16_t)promoted, (uint16_t)rr));
            return dst;
        }

        /* float OP float */
        {
            uint8_t op;
            switch (node->u.binop.op) {
            case SL_OP_ADD: op = OP_FADD; break;
            case SL_OP_SUB: op = OP_FSUB; break;
            case SL_OP_MUL: op = OP_FMUL; break;
            case SL_OP_DIV: op = OP_FDIV; break;
            default:        op = OP_FADD; break;
            }
            int dst = alloc_reg(cg, 1);
            emit(cg, RH_SL_INSTR(op, (uint16_t)dst, (uint16_t)lr, (uint16_t)rr));
            return dst;
        }
        (void)result_type;
    }

    case SL_NODE_UNOP: {
        int operand = emit_expr(cg, node->u.unop.operand);
        RhSLType ot = node->u.unop.operand->resolved_type;

        if (node->u.unop.op == SL_UOP_NEG) {
            if (is_tuple_type(ot)) {
                int dst = alloc_reg(cg, 3);
                emit(cg, RH_SL_INSTR(OP_VNEG, (uint16_t)dst, (uint16_t)operand, 0));
                return dst;
            } else {
                int dst = alloc_reg(cg, 1);
                emit(cg, RH_SL_INSTR(OP_FNEG, (uint16_t)dst, (uint16_t)operand, 0));
                return dst;
            }
        } else { /* SL_UOP_NOT */
            int dst = alloc_reg(cg, 1);
            emit(cg, RH_SL_INSTR(OP_NOT, (uint16_t)dst, (uint16_t)operand, 0));
            return dst;
        }
    }

    case SL_NODE_TERNARY: {
        int cond = emit_expr(cg, node->u.ternary.cond);
        int result_nc = type_reg_count(node->resolved_type);
        int result = alloc_reg(cg, result_nc);

        int else_label = label_new(cg);
        int end_label = label_new(cg);

        emit_jump(cg, OP_JUMP_IFNOT, else_label, (uint16_t)cond);

        /* Then branch */
        int then_reg = emit_expr(cg, node->u.ternary.then_expr);
        if (result_nc == 3)
            emit(cg, RH_SL_INSTR(OP_VMOV, (uint16_t)result, (uint16_t)then_reg, 0));
        else
            emit(cg, RH_SL_INSTR(OP_FMOV, (uint16_t)result, (uint16_t)then_reg, 0));
        emit_jump(cg, OP_JUMP, end_label, 0);

        /* Else branch */
        label_define(cg, else_label);
        int else_reg = emit_expr(cg, node->u.ternary.else_expr);
        if (result_nc == 3)
            emit(cg, RH_SL_INSTR(OP_VMOV, (uint16_t)result, (uint16_t)else_reg, 0));
        else
            emit(cg, RH_SL_INSTR(OP_FMOV, (uint16_t)result, (uint16_t)else_reg, 0));

        label_define(cg, end_label);
        return result;
    }

    case SL_NODE_TYPECAST: {
        int nargs = count_args(node->u.typecast.args);
        RhSLType target = node->u.typecast.type;

        if (nargs == 1) {
            /* Splat: color(f) -> (f, f, f) */
            int src = emit_expr(cg, node->u.typecast.args);
            if (is_tuple_type(target)) {
                if (node->u.typecast.args->resolved_type == SL_TYPE_FLOAT) {
                    int dst = alloc_reg(cg, 3);
                    emit(cg, RH_SL_INSTR(OP_FTOV, (uint16_t)dst, (uint16_t)src, 0));
                    return dst;
                } else {
                    /* tuple -> tuple (same storage) */
                    return src;
                }
            }
            return src;
        }

        if (nargs == 3 && is_tuple_type(target)) {
            /* color(r, g, b) */
            int dst = alloc_reg(cg, 3);
            const RhSLNode* a = node->u.typecast.args;
            int r0 = emit_expr(cg, a); a = a->next;
            int r1 = emit_expr(cg, a); a = a->next;
            int r2 = emit_expr(cg, a);
            emit(cg, RH_SL_INSTR(OP_FMOV, (uint16_t)dst, (uint16_t)r0, 0));
            emit(cg, RH_SL_INSTR(OP_FMOV, (uint16_t)(dst + 1), (uint16_t)r1, 0));
            emit(cg, RH_SL_INSTR(OP_FMOV, (uint16_t)(dst + 2), (uint16_t)r2, 0));
            return dst;
        }

        /* Fallback */
        if (node->u.typecast.args)
            return emit_expr(cg, node->u.typecast.args);
        return 0;
    }

    case SL_NODE_CALL:
        return emit_builtin_call(cg, node);

    case SL_NODE_COMP_ACCESS: {
        int operand = emit_expr(cg, node->u.comp_access.operand);
        int comp = node->u.comp_access.component;
        int dst = alloc_reg(cg, 1);
        emit(cg, RH_SL_INSTR_F(OP_VCOMP, (uint8_t)comp, (uint16_t)dst, (uint16_t)operand, 0));
        return dst;
    }

    case SL_NODE_ARRAY_ACCESS:
        /* Not fully supported -- evaluate array expression */
        return emit_expr(cg, node->u.array_access.array);

    default:
        return 0;
    }
}

/* ------------------------------------------------------------------ */
/*  Statement code generation                                          */
/* ------------------------------------------------------------------ */

static void emit_block(CodegenState* cg, const RhSLNode* node);

static void emit_stmt(CodegenState* cg, const RhSLNode* node) {
    if (!node) return;

    switch (node->node_type) {

    case SL_NODE_VAR_DECL: {
        RhSLType type = node->u.var_decl.type;
        int nc = type_reg_count(type);
        int reg = alloc_reg(cg, nc);
        cg_declare(cg, node->u.var_decl.name, type, reg);

        if (node->u.var_decl.init) {
            int val = emit_expr(cg, node->u.var_decl.init);
            RhSLType init_type = node->u.var_decl.init->resolved_type;

            if (is_tuple_type(type) && init_type == SL_TYPE_FLOAT) {
                /* Promote float to tuple */
                emit(cg, RH_SL_INSTR(OP_FTOV, (uint16_t)reg, (uint16_t)val, 0));
            } else if (is_tuple_type(type)) {
                emit(cg, RH_SL_INSTR(OP_VMOV, (uint16_t)reg, (uint16_t)val, 0));
            } else {
                emit(cg, RH_SL_INSTR(OP_FMOV, (uint16_t)reg, (uint16_t)val, 0));
            }
        }
        break;
    }

    case SL_NODE_ASSIGN: {
        int val = emit_expr(cg, node->u.assign.value);
        RhSLType target_type = node->u.assign.target->resolved_type;
        RhSLType val_type = node->u.assign.value->resolved_type;

        /* Get target register */
        int target_reg = -1;
        if (node->u.assign.target->node_type == SL_NODE_IDENT) {
            CGSymbol* sym = cg_lookup(cg, node->u.assign.target->u.ident.name);
            if (sym) target_reg = sym->reg;
        } else if (node->u.assign.target->node_type == SL_NODE_COMP_ACCESS) {
            /* Assign to component: e.g., xcomp(v) = expr */
            int operand_reg = emit_expr(cg, node->u.assign.target->u.comp_access.operand);
            int comp = node->u.assign.target->u.comp_access.component;
            emit(cg, RH_SL_INSTR_F(OP_VSETCOMP, (uint8_t)comp, (uint16_t)operand_reg, (uint16_t)val, 0));
            break;
        }

        if (target_reg < 0) break;

        if (is_tuple_type(target_type) && val_type == SL_TYPE_FLOAT) {
            emit(cg, RH_SL_INSTR(OP_FTOV, (uint16_t)target_reg, (uint16_t)val, 0));
        } else if (is_tuple_type(target_type)) {
            emit(cg, RH_SL_INSTR(OP_VMOV, (uint16_t)target_reg, (uint16_t)val, 0));
        } else {
            emit(cg, RH_SL_INSTR(OP_FMOV, (uint16_t)target_reg, (uint16_t)val, 0));
        }
        break;
    }

    case SL_NODE_COMPOUND_ASSIGN: {
        int val = emit_expr(cg, node->u.compound_assign.value);
        RhSLType target_type = node->u.compound_assign.target->resolved_type;
        RhSLType val_type = node->u.compound_assign.value->resolved_type;

        int target_reg = -1;
        if (node->u.compound_assign.target->node_type == SL_NODE_IDENT) {
            CGSymbol* sym = cg_lookup(cg, node->u.compound_assign.target->u.ident.name);
            if (sym) target_reg = sym->reg;
        }
        if (target_reg < 0) break;

        /* If value is float but target is tuple, promote */
        int effective_val = val;
        if (is_tuple_type(target_type) && val_type == SL_TYPE_FLOAT) {
            effective_val = alloc_reg(cg, 3);
            emit(cg, RH_SL_INSTR(OP_FTOV, (uint16_t)effective_val, (uint16_t)val, 0));
            val_type = target_type;
        }

        if (is_tuple_type(target_type) && is_tuple_type(val_type)) {
            uint8_t op;
            switch (node->u.compound_assign.op) {
            case SL_COMPOUND_ADD: op = OP_VADD; break;
            case SL_COMPOUND_SUB: op = OP_VSUB; break;
            case SL_COMPOUND_MUL: op = OP_VMUL; break;
            case SL_COMPOUND_DIV: op = OP_VDIV; break;
            default:              op = OP_VADD; break;
            }
            emit(cg, RH_SL_INSTR(op, (uint16_t)target_reg, (uint16_t)target_reg, (uint16_t)effective_val));
        } else if (is_tuple_type(target_type) && val_type == SL_TYPE_FLOAT) {
            /* tuple *= float etc. */
            if (node->u.compound_assign.op == SL_COMPOUND_MUL) {
                emit(cg, RH_SL_INSTR(OP_VSMUL, (uint16_t)target_reg, (uint16_t)target_reg, (uint16_t)val));
            } else if (node->u.compound_assign.op == SL_COMPOUND_DIV) {
                emit(cg, RH_SL_INSTR(OP_VSDIV, (uint16_t)target_reg, (uint16_t)target_reg, (uint16_t)val));
            } else {
                int promoted = alloc_reg(cg, 3);
                emit(cg, RH_SL_INSTR(OP_FTOV, (uint16_t)promoted, (uint16_t)val, 0));
                uint8_t op = (node->u.compound_assign.op == SL_COMPOUND_ADD) ? OP_VADD : OP_VSUB;
                emit(cg, RH_SL_INSTR(op, (uint16_t)target_reg, (uint16_t)target_reg, (uint16_t)promoted));
            }
        } else {
            uint8_t op;
            switch (node->u.compound_assign.op) {
            case SL_COMPOUND_ADD: op = OP_FADD; break;
            case SL_COMPOUND_SUB: op = OP_FSUB; break;
            case SL_COMPOUND_MUL: op = OP_FMUL; break;
            case SL_COMPOUND_DIV: op = OP_FDIV; break;
            default:              op = OP_FADD; break;
            }
            emit(cg, RH_SL_INSTR(op, (uint16_t)target_reg, (uint16_t)target_reg, (uint16_t)effective_val));
        }
        break;
    }

    case SL_NODE_IF: {
        int cond = emit_expr(cg, node->u.if_stmt.cond);
        if (node->u.if_stmt.else_body) {
            int else_label = label_new(cg);
            int end_label = label_new(cg);
            emit_jump(cg, OP_JUMP_IFNOT, else_label, (uint16_t)cond);
            emit_stmt(cg, node->u.if_stmt.then_body);
            emit_jump(cg, OP_JUMP, end_label, 0);
            label_define(cg, else_label);
            emit_stmt(cg, node->u.if_stmt.else_body);
            label_define(cg, end_label);
        } else {
            int end_label = label_new(cg);
            emit_jump(cg, OP_JUMP_IFNOT, end_label, (uint16_t)cond);
            emit_stmt(cg, node->u.if_stmt.then_body);
            label_define(cg, end_label);
        }
        break;
    }

    case SL_NODE_WHILE: {
        int loop_label = label_new(cg);
        int end_label = label_new(cg);
        int cont_label = loop_label; /* continue goes to loop start */

        /* Push loop context */
        if (cg->loop_depth < MAX_LOOPS) {
            cg->loops[cg->loop_depth].break_label = end_label;
            cg->loops[cg->loop_depth].continue_label = cont_label;
            cg->loop_depth++;
        }

        label_define(cg, loop_label);
        int cond = emit_expr(cg, node->u.while_stmt.cond);
        emit_jump(cg, OP_JUMP_IFNOT, end_label, (uint16_t)cond);
        emit_stmt(cg, node->u.while_stmt.body);
        emit_jump(cg, OP_JUMP, loop_label, 0);
        label_define(cg, end_label);

        if (cg->loop_depth > 0) cg->loop_depth--;
        break;
    }

    case SL_NODE_FOR: {
        int loop_label = label_new(cg);
        int end_label = label_new(cg);
        int cont_label = label_new(cg);

        push_scope(cg);

        /* Init */
        if (node->u.for_stmt.init)
            emit_stmt(cg, node->u.for_stmt.init);

        /* Push loop context */
        if (cg->loop_depth < MAX_LOOPS) {
            cg->loops[cg->loop_depth].break_label = end_label;
            cg->loops[cg->loop_depth].continue_label = cont_label;
            cg->loop_depth++;
        }

        label_define(cg, loop_label);

        /* Condition */
        if (node->u.for_stmt.cond) {
            int cond = emit_expr(cg, node->u.for_stmt.cond);
            emit_jump(cg, OP_JUMP_IFNOT, end_label, (uint16_t)cond);
        }

        /* Body */
        emit_stmt(cg, node->u.for_stmt.body);

        /* Continue target: increment */
        label_define(cg, cont_label);
        if (node->u.for_stmt.inc)
            emit_stmt(cg, node->u.for_stmt.inc);

        emit_jump(cg, OP_JUMP, loop_label, 0);
        label_define(cg, end_label);

        if (cg->loop_depth > 0) cg->loop_depth--;
        pop_scope(cg);
        break;
    }

    case SL_NODE_BREAK: {
        if (cg->loop_depth > 0) {
            emit_jump(cg, OP_JUMP, cg->loops[cg->loop_depth - 1].break_label, 0);
        }
        break;
    }

    case SL_NODE_CONTINUE: {
        if (cg->loop_depth > 0) {
            emit_jump(cg, OP_JUMP, cg->loops[cg->loop_depth - 1].continue_label, 0);
        }
        break;
    }

    case SL_NODE_RETURN: {
        if (node->u.ret.value) {
            /* For user functions, emit value and RET.
             * The function's return register should have been set up by the
             * function emission code. For now we just emit RET. */
            (void)emit_expr(cg, node->u.ret.value);
        }
        emit(cg, RH_SL_INSTR(OP_RET, 0, 0, 0));
        break;
    }

    case SL_NODE_BLOCK:
        emit_block(cg, node);
        break;

    case SL_NODE_ILLUMINANCE: {
        int after_end = label_new(cg);
        int body_start_label = label_new(cg);

        /* Evaluate position and normal for illuminance */
        int pos_reg = R_P;
        int norm_reg = R_N;
        if (node->u.illuminance.position)
            pos_reg = emit_expr(cg, node->u.illuminance.position);
        if (node->u.illuminance.normal)
            norm_reg = emit_expr(cg, node->u.illuminance.normal);

        /* ILLUMINANCE_BEGIN: dst=after_end, src1=P_reg, src2=N_reg */
        emit_jump(cg, OP_ILLUMINANCE_BEGIN, after_end, (uint16_t)pos_reg);
        /* Patch src2 manually for N_reg */
        {
            int addr = current_addr(cg) - 1;
            uint64_t instr = cg->code[addr];
            uint8_t op = RH_SL_DECODE_OP(instr);
            uint8_t flags = RH_SL_DECODE_FLAGS(instr);
            uint16_t dst = RH_SL_DECODE_DST(instr);
            uint16_t src1 = RH_SL_DECODE_SRC1(instr);
            (void)flags;
            cg->code[addr] = RH_SL_INSTR(op, dst, src1, (uint16_t)norm_reg);
        }

        /* Body */
        label_define(cg, body_start_label);
        if (node->u.illuminance.body)
            emit_stmt(cg, node->u.illuminance.body);

        /* ILLUMINANCE_END: dst=body_start, jumps back if more lights */
        emit_jump(cg, OP_ILLUMINANCE_END, body_start_label, 0);

        label_define(cg, after_end);
        break;
    }

    case SL_NODE_ILLUMINATE: {
        int after_end = label_new(cg);
        int body_start_label = label_new(cg);

        int pos_reg = 0;
        if (node->u.illuminate.position)
            pos_reg = emit_expr(cg, node->u.illuminate.position);

        /* 3-arg illuminate(from, axis, angle): emit explicit cone check */
        if (node->u.illuminate.axis && node->u.illuminate.angle) {
            int axis_reg = emit_expr(cg, node->u.illuminate.axis);
            int angle_reg = emit_expr(cg, node->u.illuminate.angle);
            /* L_dir = Ps - from */
            int tmp_l = alloc_reg(cg, 3);
            emit(cg, RH_SL_INSTR(OP_VSUB, (uint16_t)tmp_l, (uint16_t)R_PS, (uint16_t)pos_reg));
            /* normalize L_dir */
            int tmp_ln = alloc_reg(cg, 3);
            emit(cg, RH_SL_INSTR(OP_NORMALIZE, (uint16_t)tmp_ln, (uint16_t)tmp_l, 0));
            /* cos_angle = dot(L_norm, axis) */
            int tmp_cos = alloc_reg(cg, 1);
            emit(cg, RH_SL_INSTR(OP_DOT, (uint16_t)tmp_cos, (uint16_t)tmp_ln, (uint16_t)axis_reg));
            /* actual_angle = acos(cos_angle) */
            int tmp_angle = alloc_reg(cg, 1);
            emit(cg, RH_SL_INSTR(OP_FACOS, (uint16_t)tmp_angle, (uint16_t)tmp_cos, 0));
            /* test: actual_angle > coneangle? */
            int tmp_cmp = alloc_reg(cg, 1);
            emit(cg, RH_SL_INSTR(OP_FGT, (uint16_t)tmp_cmp, (uint16_t)tmp_angle, (uint16_t)angle_reg));
            /* skip illuminate body if outside cone */
            emit_jump(cg, OP_JUMP_IF, after_end, (uint16_t)tmp_cmp);
        }

        emit_jump(cg, OP_ILLUMINATE_BEGIN, after_end, (uint16_t)pos_reg);

        label_define(cg, body_start_label);
        if (node->u.illuminate.body)
            emit_stmt(cg, node->u.illuminate.body);

        emit_jump(cg, OP_ILLUMINATE_END, body_start_label, 0);
        label_define(cg, after_end);
        break;
    }

    case SL_NODE_SOLAR: {
        int after_end = label_new(cg);
        int body_start_label = label_new(cg);

        int axis_reg = 0;
        if (node->u.solar.axis) {
            axis_reg = emit_expr(cg, node->u.solar.axis);
            /* If angle is present and nonzero, emit cone check */
            if (node->u.solar.angle) {
                int angle_reg = emit_expr(cg, node->u.solar.angle);
                /* L_dir = normalize(Ps - 0) -- direction from origin to Ps */
                int tmp_ln = alloc_reg(cg, 3);
                emit(cg, RH_SL_INSTR(OP_NORMALIZE, (uint16_t)tmp_ln, (uint16_t)R_PS, 0));
                /* neg_axis = -axis (solar L = -axis) */
                int neg_axis = alloc_reg(cg, 3);
                emit(cg, RH_SL_INSTR(OP_VNEG, (uint16_t)neg_axis, (uint16_t)axis_reg, 0));
                /* cos_angle = dot(Ps_norm, neg_axis) */
                int tmp_cos = alloc_reg(cg, 1);
                emit(cg, RH_SL_INSTR(OP_DOT, (uint16_t)tmp_cos, (uint16_t)tmp_ln, (uint16_t)neg_axis));
                /* actual_angle = acos(cos_angle) */
                int tmp_angle = alloc_reg(cg, 1);
                emit(cg, RH_SL_INSTR(OP_FACOS, (uint16_t)tmp_angle, (uint16_t)tmp_cos, 0));
                /* test: actual_angle > cone_angle? */
                int tmp_cmp = alloc_reg(cg, 1);
                emit(cg, RH_SL_INSTR(OP_FGT, (uint16_t)tmp_cmp, (uint16_t)tmp_angle, (uint16_t)angle_reg));
                emit_jump(cg, OP_JUMP_IF, after_end, (uint16_t)tmp_cmp);
            }
        }

        emit_jump(cg, OP_SOLAR_BEGIN, after_end, (uint16_t)axis_reg);

        label_define(cg, body_start_label);
        if (node->u.solar.body)
            emit_stmt(cg, node->u.solar.body);

        emit_jump(cg, OP_SOLAR_END, body_start_label, 0);
        label_define(cg, after_end);
        break;
    }

    default:
        /* Expression statement (e.g., function call) */
        emit_expr(cg, node);
        break;
    }
}

static void emit_block(CodegenState* cg, const RhSLNode* node) {
    if (!node || node->node_type != SL_NODE_BLOCK) return;
    push_scope(cg);
    for (const RhSLNode* s = node->u.block.stmts; s; s = s->next) {
        emit_stmt(cg, s);
    }
    pop_scope(cg);
}

/* ------------------------------------------------------------------ */
/*  User function emission                                             */
/* ------------------------------------------------------------------ */

static void emit_function(CodegenState* cg, const RhSLNode* fn) {
    if (!fn || fn->node_type != SL_NODE_FUNCTION) return;

    /* Register function in the func table */
    if (cg->num_funcs >= MAX_FUNCS) return;
    FuncEntry* fe = &cg->funcs[cg->num_funcs++];
    sl_strcpy(fe->name, sizeof(fe->name), fn->u.function.name);
    fe->return_type = fn->u.function.return_type;
    fe->label = label_new(cg);

    /* Allocate return register */
    int ret_nc = type_reg_count(fn->u.function.return_type);
    fe->return_reg = alloc_reg(cg, ret_nc > 0 ? ret_nc : 1);

    /* Define function label */
    label_define(cg, fe->label);

    push_scope(cg);

    /* Declare formals (allocate registers for them) */
    for (const RhSLNode* f = fn->u.function.formals; f; f = f->next) {
        if (f->node_type == SL_NODE_FORMAL) {
            int nc = type_reg_count(f->u.formal.type);
            int reg = alloc_reg(cg, nc);
            cg_declare(cg, f->u.formal.name, f->u.formal.type, reg);
        }
    }

    /* Emit function body */
    if (fn->u.function.body)
        emit_block(cg, fn->u.function.body);

    /* Emit RET at end (safety net) */
    emit(cg, RH_SL_INSTR(OP_RET, 0, 0, 0));

    pop_scope(cg);
}

/* ------------------------------------------------------------------ */
/*  Shader parameter info conversion                                   */
/* ------------------------------------------------------------------ */

static int sl_type_to_param_type(RhSLType t) {
    switch (t) {
    case SL_TYPE_FLOAT:  return RH_SL_TYPE_FLOAT;
    case SL_TYPE_COLOR:  return RH_SL_TYPE_COLOR;
    case SL_TYPE_POINT:  return RH_SL_TYPE_POINT;
    case SL_TYPE_VECTOR: return RH_SL_TYPE_VECTOR;
    case SL_TYPE_NORMAL: return RH_SL_TYPE_NORMAL;
    case SL_TYPE_STRING: return RH_SL_TYPE_STRING;
    default:             return RH_SL_TYPE_FLOAT;
    }
}

static int shader_type_to_vm(RhSLShaderType t) {
    switch (t) {
    case SL_SHADER_SURFACE:      return RH_SL_SHADER_SURFACE;
    case SL_SHADER_LIGHT:        return RH_SL_SHADER_LIGHT;
    case SL_SHADER_DISPLACEMENT: return RH_SL_SHADER_DISPLACEMENT;
    case SL_SHADER_VOLUME:       return RH_SL_SHADER_VOLUME;
    default:                     return RH_SL_SHADER_SURFACE;
    }
}

/* ------------------------------------------------------------------ */
/*  Evaluate constant expression for parameter defaults                */
/* ------------------------------------------------------------------ */

static float eval_const_float(const RhSLNode* node) {
    if (!node) return 0.0f;
    if (node->node_type == SL_NODE_FLOAT_LIT)
        return node->u.float_lit.value;
    if (node->node_type == SL_NODE_UNOP && node->u.unop.op == SL_UOP_NEG)
        return -eval_const_float(node->u.unop.operand);
    return 0.0f;
}

/* ------------------------------------------------------------------ */
/*  Top-level entry point                                              */
/* ------------------------------------------------------------------ */

RhSLProgram* rh_sl_codegen(const RhSLNode* shader, RhSLCodegenErrors* err_out) {
    if (!shader || shader->node_type != SL_NODE_SHADER)
        return NULL;

    /* Initialize codegen state */
    CodegenState cg;
    memset(&cg, 0, sizeof(cg));
    cg.err = err_out;
    if (err_out) err_out->num_errors = 0;
    cg.shader_type = shader->u.shader.shader_type;

    /* Start register allocation after builtins */
    cg.next_reg = R_PARAMS_START;
    cg.max_reg = R_PARAMS_START;

    /* Populate builtin symbols */
    push_scope(&cg);
    populate_builtin_symbols(&cg);

    /* Process shader parameters */
    int num_params = 0;
    RhSLParamInfo param_info[RH_SL_MAX_PARAMS];
    memset(param_info, 0, sizeof(param_info));

    /* Temporary storage for default values */
    float default_values[RH_SL_MAX_PARAMS * 3]; /* max 3 components per param */
    int default_offsets[RH_SL_MAX_PARAMS];
    int total_default_floats = 0;

    for (const RhSLNode* p = shader->u.shader.params; p; p = p->next) {
        if (p->node_type != SL_NODE_PARAM) continue;
        if (num_params >= RH_SL_MAX_PARAMS) break;

        int nc = type_reg_count(p->u.param.type);
        int reg = alloc_reg(&cg, nc);

        sl_strcpy(param_info[num_params].name, sizeof(param_info[num_params].name),
                  p->u.param.name);
        param_info[num_params].type = sl_type_to_param_type(p->u.param.type);
        param_info[num_params].reg = reg;
        param_info[num_params].num_components = nc;

        /* Evaluate default value */
        default_offsets[num_params] = total_default_floats;
        if (p->u.param.type == SL_TYPE_STRING) {
            /* String param: add default string to string table, store index */
            int str_idx = 0;
            if (p->u.param.default_val &&
                p->u.param.default_val->node_type == SL_NODE_STRING_LIT) {
                str_idx = add_string(&cg, p->u.param.default_val->u.string_lit.value);
            } else {
                str_idx = add_string(&cg, "");
            }
            param_info[num_params].default_idx = str_idx;
            /* String params have 0 float components in const pool */
            param_info[num_params].num_components = 0;
            /* Track string param for texture() codegen */
            if (cg.num_string_params < RH_SL_MAX_PARAMS) {
                sl_strcpy(cg.string_param_names[cg.num_string_params],
                          sizeof(cg.string_param_names[0]), p->u.param.name);
                cg.string_param_str_idx[cg.num_string_params] = str_idx;
                cg.num_string_params++;
            }
        } else if (p->u.param.default_val) {
            if (p->u.param.default_val->node_type == SL_NODE_TYPECAST && nc == 3) {
                /* color(r,g,b) or similar */
                const RhSLNode* a = p->u.param.default_val->u.typecast.args;
                int nargs = count_args(a);
                if (nargs == 3) {
                    default_values[total_default_floats++] = eval_const_float(a); a = a->next;
                    default_values[total_default_floats++] = eval_const_float(a); a = a->next;
                    default_values[total_default_floats++] = eval_const_float(a);
                } else if (nargs == 1) {
                    float v = eval_const_float(a);
                    default_values[total_default_floats++] = v;
                    default_values[total_default_floats++] = v;
                    default_values[total_default_floats++] = v;
                } else {
                    for (int i = 0; i < nc; i++)
                        default_values[total_default_floats++] = 0.0f;
                }
            } else {
                float v = eval_const_float(p->u.param.default_val);
                default_values[total_default_floats++] = v;
                for (int i = 1; i < nc; i++)
                    default_values[total_default_floats++] = v;
            }
        } else {
            for (int i = 0; i < nc; i++)
                default_values[total_default_floats++] = 0.0f;
        }

        cg_declare(&cg, p->u.param.name, p->u.param.type, reg);
        num_params++;
    }

    /* Build constant pool with default values first */
    for (int i = 0; i < total_default_floats; i++) {
        cg.consts[cg.const_count++] = default_values[i];
    }

    /* Set default_idx in param_info now that we know const pool positions.
     * Skip string params -- their default_idx was already set to a string table index. */
    for (int i = 0; i < num_params; i++) {
        if (param_info[i].type != RH_SL_TYPE_STRING)
            param_info[i].default_idx = default_offsets[i];
    }

    /* Emit user functions (with jump over each) */
    int has_functions = (shader->u.shader.functions != NULL);
    int body_label = -1;
    if (has_functions) {
        body_label = label_new(&cg);
        emit_jump(&cg, OP_JUMP, body_label, 0);

        for (const RhSLNode* fn = shader->u.shader.functions; fn; fn = fn->next) {
            emit_function(&cg, fn);
        }

        label_define(&cg, body_label);
    }

    /* Emit shader body */
    if (shader->u.shader.body)
        emit_block(&cg, shader->u.shader.body);

    /* Emit HALT */
    emit(&cg, RH_SL_INSTR(OP_HALT, 0, 0, 0));

    /* Resolve forward jumps */
    resolve_jumps(&cg);

    pop_scope(&cg);

    /* Build the RhSLProgram */
    RhSLProgram* prog = rh_sl_program_create();
    if (!prog) return NULL;

    prog->shader_type = shader_type_to_vm(shader->u.shader.shader_type);
    sl_strcpy(prog->shader_name, sizeof(prog->shader_name), shader->u.shader.name);

    /* Copy code */
    prog->code_len = cg.code_len;
    prog->code = malloc(sizeof(uint64_t) * (size_t)cg.code_len);
    memcpy(prog->code, cg.code, sizeof(uint64_t) * (size_t)cg.code_len);

    /* Copy constant pool */
    prog->const_count = cg.const_count;
    prog->const_pool = malloc(sizeof(float) * (size_t)cg.const_count);
    memcpy(prog->const_pool, cg.consts, sizeof(float) * (size_t)cg.const_count);

    /* Copy string table */
    prog->string_count = cg.string_count;
    if (cg.string_count > 0) {
        prog->string_table = malloc(sizeof(char*) * (size_t)cg.string_count);
        for (int i = 0; i < cg.string_count; i++) {
            size_t len = strlen(cg.strings[i]) + 1;
            prog->string_table[i] = malloc(len);
            memcpy(prog->string_table[i], cg.strings[i], len);
        }
    }

    /* Register requirements */
    prog->num_regs = cg.max_reg;

    /* Copy param info */
    prog->num_params = num_params;
    memcpy(prog->params, param_info, sizeof(RhSLParamInfo) * (size_t)num_params);

    /* Free temp strings */
    for (int i = 0; i < cg.string_count; i++)
        free(cg.strings[i]);

    return prog;
}
