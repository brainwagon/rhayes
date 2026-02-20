#include "rh_sl_sema.h"
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

/* Safe string copy (no strncpy truncation warnings) */
static void sl_strcpy(char* dst, size_t dstsz, const char* src) {
    size_t len = strlen(src);
    if (len >= dstsz) len = dstsz - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* ------------------------------------------------------------------ */
/*  Error reporting                                                    */
/* ------------------------------------------------------------------ */

static void sema_error(RhSLSema* sema, int line, const char* fmt, ...) {
    if (sema->num_errors >= 32) return;
    char* dst = sema->errors[sema->num_errors];
    int off = snprintf(dst, 256, "line %d: ", line);
    if (off < 0) off = 0;
    if (off < 256) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(dst + off, (size_t)(256 - off), fmt, ap);
        va_end(ap);
    }
    sema->num_errors++;
}

/* ------------------------------------------------------------------ */
/*  Scope management                                                   */
/* ------------------------------------------------------------------ */

static void push_scope(RhSLSema* sema) {
    if (sema->scope_depth >= SL_MAX_SCOPES) return;
    sema->scope_starts[sema->scope_depth] = sema->num_symbols;
    sema->scope_depth++;
}

static void pop_scope(RhSLSema* sema) {
    if (sema->scope_depth <= 0) return;
    sema->scope_depth--;
    sema->num_symbols = sema->scope_starts[sema->scope_depth];
}

static RhSLSymbol* declare_symbol(RhSLSema* sema, const char* name,
                                  RhSLType type, int line) {
    /* Check for same-scope redeclaration */
    int scope_start = (sema->scope_depth > 0)
        ? sema->scope_starts[sema->scope_depth - 1] : 0;
    for (int i = sema->num_symbols - 1; i >= scope_start; i--) {
        if (strcmp(sema->symbols[i].name, name) == 0) {
            sema_error(sema, line, "redeclaration of '%s'", name);
            return NULL;
        }
    }
    if (sema->num_symbols >= SL_MAX_SYMBOLS) {
        sema_error(sema, line, "too many symbols");
        return NULL;
    }
    RhSLSymbol* sym = &sema->symbols[sema->num_symbols++];
    memset(sym, 0, sizeof(*sym));
    sl_strcpy(sym->name, sizeof(sym->name), name);
    sym->type = type;
    sym->scope_depth = sema->scope_depth;
    sym->is_writable = 1; /* default writable */
    return sym;
}

static RhSLSymbol* lookup_symbol(RhSLSema* sema, const char* name) {
    for (int i = sema->num_symbols - 1; i >= 0; i--) {
        if (strcmp(sema->symbols[i].name, name) == 0)
            return &sema->symbols[i];
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Built-in globals                                                   */
/* ------------------------------------------------------------------ */

/* Shader type bitmask for which shader types a builtin is available */
#define BMASK_SURFACE  (1 << SL_SHADER_SURFACE)
#define BMASK_LIGHT    (1 << SL_SHADER_LIGHT)
#define BMASK_DISPLACE (1 << SL_SHADER_DISPLACEMENT)
#define BMASK_VOLUME   (1 << SL_SHADER_VOLUME)
#define BMASK_ALL      (BMASK_SURFACE | BMASK_LIGHT | BMASK_DISPLACE | BMASK_VOLUME)

typedef struct {
    const char* name;
    RhSLType type;
    int writable;
    int shader_mask;
} BuiltinGlobal;

static const BuiltinGlobal builtin_globals[] = {
    /* Surface / general globals */
    {"P",    SL_TYPE_POINT,  1, BMASK_ALL},
    {"N",    SL_TYPE_NORMAL, 1, BMASK_SURFACE | BMASK_DISPLACE | BMASK_VOLUME},
    {"Ng",   SL_TYPE_NORMAL, 0, BMASK_SURFACE | BMASK_DISPLACE | BMASK_VOLUME},
    {"I",    SL_TYPE_VECTOR, 0, BMASK_SURFACE | BMASK_DISPLACE | BMASK_VOLUME},
    {"E",    SL_TYPE_POINT,  0, BMASK_ALL},
    {"Cs",   SL_TYPE_COLOR,  1, BMASK_SURFACE | BMASK_VOLUME},
    {"Os",   SL_TYPE_COLOR,  1, BMASK_SURFACE | BMASK_VOLUME},
    {"Ci",   SL_TYPE_COLOR,  1, BMASK_SURFACE | BMASK_LIGHT | BMASK_VOLUME},
    {"Oi",   SL_TYPE_COLOR,  1, BMASK_SURFACE | BMASK_VOLUME},
    {"s",    SL_TYPE_FLOAT,  1, BMASK_SURFACE | BMASK_VOLUME},
    {"t",    SL_TYPE_FLOAT,  1, BMASK_SURFACE | BMASK_VOLUME},
    {"u",    SL_TYPE_FLOAT,  0, BMASK_SURFACE | BMASK_DISPLACE | BMASK_VOLUME},
    {"v",    SL_TYPE_FLOAT,  0, BMASK_SURFACE | BMASK_DISPLACE | BMASK_VOLUME},
    {"du",   SL_TYPE_FLOAT,  0, BMASK_SURFACE | BMASK_DISPLACE | BMASK_VOLUME},
    {"dv",   SL_TYPE_FLOAT,  0, BMASK_SURFACE | BMASK_DISPLACE | BMASK_VOLUME},
    {"L",    SL_TYPE_VECTOR, 0, BMASK_SURFACE | BMASK_VOLUME},
    {"Cl",   SL_TYPE_COLOR,  0, BMASK_SURFACE | BMASK_VOLUME},
    {"dPdu", SL_TYPE_VECTOR, 0, BMASK_SURFACE | BMASK_DISPLACE},
    {"dPdv", SL_TYPE_VECTOR, 0, BMASK_SURFACE | BMASK_DISPLACE},

    /* Light shader globals */
    {"Ps",   SL_TYPE_POINT,  0, BMASK_LIGHT},
    {"Pw",   SL_TYPE_POINT,  0, BMASK_LIGHT},
    {"N",    SL_TYPE_NORMAL, 0, BMASK_LIGHT},
    {"L",    SL_TYPE_VECTOR, 1, BMASK_LIGHT},
    {"Cl",   SL_TYPE_COLOR,  1, BMASK_LIGHT},

    /* Displacement extras */
    {"s",    SL_TYPE_FLOAT,  0, BMASK_DISPLACE},
    {"t",    SL_TYPE_FLOAT,  0, BMASK_DISPLACE},

    {NULL, SL_TYPE_VOID, 0, 0}
};

static void populate_builtins(RhSLSema* sema) {
    int mask = 1 << sema->shader_type;
    for (int i = 0; builtin_globals[i].name != NULL; i++) {
        if (!(builtin_globals[i].shader_mask & mask)) continue;
        /* Skip if already declared (handles duplicates in table) */
        if (lookup_symbol(sema, builtin_globals[i].name)) continue;
        RhSLSymbol* sym = declare_symbol(sema, builtin_globals[i].name,
                                         builtin_globals[i].type, 0);
        if (sym) {
            sym->is_builtin = 1;
            sym->is_writable = builtin_globals[i].writable;
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Built-in functions                                                 */
/* ------------------------------------------------------------------ */

#define MAX_BUILTIN_PARAMS 6

typedef struct {
    const char* name;
    RhSLType return_type;
    int num_params;
    RhSLType param_types[MAX_BUILTIN_PARAMS];
    int is_variadic;
} BuiltinFunc;

static const BuiltinFunc builtin_functions[] = {
    /* Math: 1-arg float->float */
    {"abs",      SL_TYPE_FLOAT, 1, {SL_TYPE_FLOAT}, 0},
    {"ceil",     SL_TYPE_FLOAT, 1, {SL_TYPE_FLOAT}, 0},
    {"floor",    SL_TYPE_FLOAT, 1, {SL_TYPE_FLOAT}, 0},
    {"round",    SL_TYPE_FLOAT, 1, {SL_TYPE_FLOAT}, 0},
    {"sqrt",     SL_TYPE_FLOAT, 1, {SL_TYPE_FLOAT}, 0},
    {"exp",      SL_TYPE_FLOAT, 1, {SL_TYPE_FLOAT}, 0},
    {"log",      SL_TYPE_FLOAT, 1, {SL_TYPE_FLOAT}, 0},
    {"sign",     SL_TYPE_FLOAT, 1, {SL_TYPE_FLOAT}, 0},
    {"sin",      SL_TYPE_FLOAT, 1, {SL_TYPE_FLOAT}, 0},
    {"cos",      SL_TYPE_FLOAT, 1, {SL_TYPE_FLOAT}, 0},
    {"tan",      SL_TYPE_FLOAT, 1, {SL_TYPE_FLOAT}, 0},
    {"asin",     SL_TYPE_FLOAT, 1, {SL_TYPE_FLOAT}, 0},
    {"acos",     SL_TYPE_FLOAT, 1, {SL_TYPE_FLOAT}, 0},
    {"radians",  SL_TYPE_FLOAT, 1, {SL_TYPE_FLOAT}, 0},
    {"degrees",  SL_TYPE_FLOAT, 1, {SL_TYPE_FLOAT}, 0},

    /* Math: 1-arg float->float (also works as atan(y,x)) */
    {"atan",     SL_TYPE_FLOAT, 1, {SL_TYPE_FLOAT}, 0},
    {"atan",     SL_TYPE_FLOAT, 2, {SL_TYPE_FLOAT, SL_TYPE_FLOAT}, 0},

    /* Math: 2-arg float->float */
    {"min",      SL_TYPE_FLOAT, 2, {SL_TYPE_FLOAT, SL_TYPE_FLOAT}, 0},
    {"max",      SL_TYPE_FLOAT, 2, {SL_TYPE_FLOAT, SL_TYPE_FLOAT}, 0},
    {"mod",      SL_TYPE_FLOAT, 2, {SL_TYPE_FLOAT, SL_TYPE_FLOAT}, 0},
    {"pow",      SL_TYPE_FLOAT, 2, {SL_TYPE_FLOAT, SL_TYPE_FLOAT}, 0},
    {"step",     SL_TYPE_FLOAT, 2, {SL_TYPE_FLOAT, SL_TYPE_FLOAT}, 0},

    /* Math: 3-arg float->float */
    {"clamp",    SL_TYPE_FLOAT, 3, {SL_TYPE_FLOAT, SL_TYPE_FLOAT, SL_TYPE_FLOAT}, 0},
    {"smoothstep", SL_TYPE_FLOAT, 3, {SL_TYPE_FLOAT, SL_TYPE_FLOAT, SL_TYPE_FLOAT}, 0},

    /* Mix overloads */
    {"mix",      SL_TYPE_FLOAT, 3, {SL_TYPE_FLOAT, SL_TYPE_FLOAT, SL_TYPE_FLOAT}, 0},
    {"mix",      SL_TYPE_COLOR, 3, {SL_TYPE_COLOR, SL_TYPE_COLOR, SL_TYPE_FLOAT}, 0},

    /* Geometric: dot product overloads */
    {"dot",      SL_TYPE_FLOAT, 2, {SL_TYPE_VECTOR, SL_TYPE_VECTOR}, 0},
    {"dot",      SL_TYPE_FLOAT, 2, {SL_TYPE_NORMAL, SL_TYPE_NORMAL}, 0},
    {"dot",      SL_TYPE_FLOAT, 2, {SL_TYPE_VECTOR, SL_TYPE_NORMAL}, 0},
    {"dot",      SL_TYPE_FLOAT, 2, {SL_TYPE_NORMAL, SL_TYPE_VECTOR}, 0},

    /* Geometric */
    {"cross",       SL_TYPE_VECTOR, 2, {SL_TYPE_VECTOR, SL_TYPE_VECTOR}, 0},
    {"length",      SL_TYPE_FLOAT,  1, {SL_TYPE_VECTOR}, 0},
    {"normalize",   SL_TYPE_VECTOR, 1, {SL_TYPE_VECTOR}, 0},
    {"normalize",   SL_TYPE_NORMAL, 1, {SL_TYPE_NORMAL}, 0},
    {"distance",    SL_TYPE_FLOAT,  2, {SL_TYPE_POINT, SL_TYPE_POINT}, 0},
    {"faceforward", SL_TYPE_VECTOR, 2, {SL_TYPE_VECTOR, SL_TYPE_VECTOR}, 0},
    {"faceforward", SL_TYPE_NORMAL, 2, {SL_TYPE_NORMAL, SL_TYPE_VECTOR}, 0},
    {"reflect",     SL_TYPE_VECTOR, 2, {SL_TYPE_VECTOR, SL_TYPE_VECTOR}, 0},

    /* Component access */
    {"xcomp",    SL_TYPE_FLOAT, 1, {SL_TYPE_VECTOR}, 0},
    {"ycomp",    SL_TYPE_FLOAT, 1, {SL_TYPE_VECTOR}, 0},
    {"zcomp",    SL_TYPE_FLOAT, 1, {SL_TYPE_VECTOR}, 0},
    {"xcomp",    SL_TYPE_FLOAT, 1, {SL_TYPE_POINT}, 0},
    {"ycomp",    SL_TYPE_FLOAT, 1, {SL_TYPE_POINT}, 0},
    {"zcomp",    SL_TYPE_FLOAT, 1, {SL_TYPE_POINT}, 0},
    {"setxcomp", SL_TYPE_VOID,  2, {SL_TYPE_VECTOR, SL_TYPE_FLOAT}, 0},
    {"setycomp", SL_TYPE_VOID,  2, {SL_TYPE_VECTOR, SL_TYPE_FLOAT}, 0},
    {"setzcomp", SL_TYPE_VOID,  2, {SL_TYPE_VECTOR, SL_TYPE_FLOAT}, 0},

    /* Special geometric */
    {"calculatenormal", SL_TYPE_NORMAL, 1, {SL_TYPE_POINT}, 0},
    {"area",            SL_TYPE_FLOAT,  1, {SL_TYPE_POINT}, 0},

    /* Color component access */
    {"comp",    SL_TYPE_FLOAT, 2, {SL_TYPE_COLOR, SL_TYPE_FLOAT}, 0},
    {"setcomp", SL_TYPE_VOID,  3, {SL_TYPE_COLOR, SL_TYPE_FLOAT, SL_TYPE_FLOAT}, 0},

    /* Illumination */
    {"ambient",  SL_TYPE_COLOR, 0, {SL_TYPE_VOID}, 0},
    {"diffuse",  SL_TYPE_COLOR, 1, {SL_TYPE_NORMAL}, 0},
    {"specular", SL_TYPE_COLOR, 3, {SL_TYPE_NORMAL, SL_TYPE_VECTOR, SL_TYPE_FLOAT}, 0},
    {"phong",    SL_TYPE_COLOR, 3, {SL_TYPE_NORMAL, SL_TYPE_VECTOR, SL_TYPE_FLOAT}, 0},
    {"trace",    SL_TYPE_COLOR, 2, {SL_TYPE_POINT, SL_TYPE_VECTOR}, 0},

    /* Texture */
    {"texture",     SL_TYPE_COLOR,  3, {SL_TYPE_STRING, SL_TYPE_FLOAT, SL_TYPE_FLOAT}, 1},
    {"shadow",      SL_TYPE_FLOAT,  2, {SL_TYPE_STRING, SL_TYPE_POINT}, 0},
    {"environment", SL_TYPE_COLOR,  2, {SL_TYPE_STRING, SL_TYPE_VECTOR}, 0},

    /* Noise */
    {"noise",     SL_TYPE_FLOAT, 1, {SL_TYPE_FLOAT}, 0},
    {"noise",     SL_TYPE_FLOAT, 2, {SL_TYPE_FLOAT, SL_TYPE_FLOAT}, 0},
    {"noise",     SL_TYPE_FLOAT, 1, {SL_TYPE_POINT}, 0},
    {"pnoise",    SL_TYPE_FLOAT, 2, {SL_TYPE_FLOAT, SL_TYPE_FLOAT}, 0},
    {"cellnoise", SL_TYPE_FLOAT, 1, {SL_TYPE_FLOAT}, 0},

    /* Transforms */
    {"transform",  SL_TYPE_POINT,  2, {SL_TYPE_STRING, SL_TYPE_POINT}, 0},
    {"transform",  SL_TYPE_POINT,  3, {SL_TYPE_STRING, SL_TYPE_STRING, SL_TYPE_POINT}, 0},
    {"ntransform", SL_TYPE_NORMAL, 2, {SL_TYPE_STRING, SL_TYPE_NORMAL}, 0},
    {"ntransform", SL_TYPE_NORMAL, 3, {SL_TYPE_STRING, SL_TYPE_STRING, SL_TYPE_NORMAL}, 0},
    {"vtransform", SL_TYPE_VECTOR, 2, {SL_TYPE_STRING, SL_TYPE_VECTOR}, 0},
    {"vtransform", SL_TYPE_VECTOR, 3, {SL_TYPE_STRING, SL_TYPE_STRING, SL_TYPE_VECTOR}, 0},

    /* Misc */
    {"printf", SL_TYPE_VOID, 1, {SL_TYPE_STRING}, 1}, /* variadic */

    {NULL, SL_TYPE_VOID, 0, {SL_TYPE_VOID}, 0}
};

/* ------------------------------------------------------------------ */
/*  Type utilities                                                     */
/* ------------------------------------------------------------------ */

static int is_tuple_type(RhSLType t) {
    return t == SL_TYPE_COLOR || t == SL_TYPE_POINT ||
           t == SL_TYPE_VECTOR || t == SL_TYPE_NORMAL;
}

static int can_promote(RhSLType from, RhSLType to) {
    if (from == to) return 1;
    /* float promotes to any tuple type */
    if (from == SL_TYPE_FLOAT && is_tuple_type(to)) return 1;
    /* tuple types are interchangeable (point/vector/normal/color) */
    if (is_tuple_type(from) && is_tuple_type(to)) return 1;
    return 0;
}

/* Resolve binary operation result type.
 * Returns SL_TYPE_VOID on incompatible types. */
static RhSLType resolve_binop_type(RhSLBinOp op, RhSLType left, RhSLType right) {
    /* Comparisons and logic always produce float */
    switch (op) {
    case SL_OP_EQ: case SL_OP_NE: case SL_OP_LT: case SL_OP_GT:
    case SL_OP_LE: case SL_OP_GE: case SL_OP_AND: case SL_OP_OR:
        return SL_TYPE_FLOAT;

    case SL_OP_DOT:
        /* dot product requires tuple args, produces float */
        if (is_tuple_type(left) && is_tuple_type(right))
            return SL_TYPE_FLOAT;
        return SL_TYPE_VOID;

    case SL_OP_CROSS:
        /* cross product requires tuple args, produces vector */
        if (is_tuple_type(left) && is_tuple_type(right))
            return SL_TYPE_VECTOR;
        return SL_TYPE_VOID;

    case SL_OP_SUB:
        /* point - point = vector */
        if (left == SL_TYPE_POINT && right == SL_TYPE_POINT)
            return SL_TYPE_VECTOR;
        /* fall through to arithmetic */
        /* FALLTHROUGH */
    case SL_OP_ADD: case SL_OP_MUL: case SL_OP_DIV:
        /* float op float -> float */
        if (left == SL_TYPE_FLOAT && right == SL_TYPE_FLOAT)
            return SL_TYPE_FLOAT;
        /* tuple op tuple -> left type (tuple types interchangeable) */
        if (is_tuple_type(left) && is_tuple_type(right))
            return left;
        /* float op tuple -> tuple type */
        if (left == SL_TYPE_FLOAT && is_tuple_type(right))
            return right;
        /* tuple op float -> tuple type */
        if (is_tuple_type(left) && right == SL_TYPE_FLOAT)
            return left;
        return SL_TYPE_VOID;
    }
    return SL_TYPE_VOID;
}

/* ------------------------------------------------------------------ */
/*  Forward declarations for mutually recursive walker                 */
/* ------------------------------------------------------------------ */

static RhSLType analyze_expr(RhSLSema* sema, RhSLNode* node);
static void analyze_stmt(RhSLSema* sema, RhSLNode* node);
static void analyze_block(RhSLSema* sema, RhSLNode* node);

/* ------------------------------------------------------------------ */
/*  Writability check                                                  */
/* ------------------------------------------------------------------ */

static void check_writable(RhSLSema* sema, RhSLNode* target) {
    if (!target) return;
    const char* name = NULL;
    if (target->node_type == SL_NODE_IDENT)
        name = target->u.ident.name;
    else if (target->node_type == SL_NODE_ARRAY_ACCESS &&
             target->u.array_access.array &&
             target->u.array_access.array->node_type == SL_NODE_IDENT)
        name = target->u.array_access.array->u.ident.name;
    if (!name) return;

    RhSLSymbol* sym = lookup_symbol(sema, name);
    if (sym && sym->is_builtin && !sym->is_writable) {
        sema_error(sema, target->line, "cannot write to read-only variable '%s'", name);
    }
}

/* ------------------------------------------------------------------ */
/*  Function resolution                                                */
/* ------------------------------------------------------------------ */

static const BuiltinFunc* resolve_builtin(const char* name, int nargs,
                                          RhSLType* arg_types) {
    const BuiltinFunc* best = NULL;
    int best_score = -1;

    for (int i = 0; builtin_functions[i].name != NULL; i++) {
        const BuiltinFunc* bf = &builtin_functions[i];
        if (strcmp(bf->name, name) != 0) continue;

        /* For variadic, just need at least num_params args */
        if (bf->is_variadic) {
            if (nargs < bf->num_params) continue;
            return bf; /* variadic match */
        }

        if (bf->num_params != nargs) continue;

        /* Score: exact=2, promotable=1, incompatible=fail */
        int score = 0;
        int ok = 1;
        for (int j = 0; j < nargs; j++) {
            if (arg_types[j] == bf->param_types[j]) {
                score += 2;
            } else if (can_promote(arg_types[j], bf->param_types[j])) {
                score += 1;
            } else {
                ok = 0;
                break;
            }
        }
        if (!ok) continue;
        if (score > best_score) {
            best_score = score;
            best = bf;
        }
    }
    return best;
}

/* ------------------------------------------------------------------ */
/*  Expression analysis                                                */
/* ------------------------------------------------------------------ */

static RhSLType analyze_call(RhSLSema* sema, RhSLNode* node) {
    const char* name = node->u.call.name;

    /* Analyze arguments */
    int nargs = 0;
    RhSLType arg_types[MAX_BUILTIN_PARAMS];
    for (RhSLNode* a = node->u.call.args; a; a = a->next) {
        RhSLType t = analyze_expr(sema, a);
        if (nargs < MAX_BUILTIN_PARAMS) arg_types[nargs] = t;
        nargs++;
    }

    /* Try built-in */
    const BuiltinFunc* bf = resolve_builtin(name, nargs, arg_types);
    if (bf) {
        node->resolved_type = bf->return_type;
        return bf->return_type;
    }

    /* Try user-defined functions (score-based overload resolution) */
    RhSLNode* best_fn = NULL;
    int best_score = -1;
    for (RhSLNode* fn = sema->functions; fn; fn = fn->next) {
        if (fn->node_type != SL_NODE_FUNCTION) continue;
        if (strcmp(fn->u.function.name, name) != 0) continue;
        int nformals = rh_sl_node_count(fn->u.function.formals);
        if (nformals != nargs) continue;
        int score = 0, ok = 1, j = 0;
        for (RhSLNode* f = fn->u.function.formals; f && j < nargs;
             f = f->next, j++) {
            if (f->node_type != SL_NODE_FORMAL) continue;
            RhSLType ft = f->u.formal.type;
            if (j < MAX_BUILTIN_PARAMS && arg_types[j] == ft) score += 2;
            else if (j < MAX_BUILTIN_PARAMS && can_promote(arg_types[j], ft)) score += 1;
            else { ok = 0; break; }
        }
        if (!ok) continue;
        if (score > best_score) { best_score = score; best_fn = fn; }
    }
    if (best_fn) {
        node->resolved_type = best_fn->u.function.return_type;
        return best_fn->u.function.return_type;
    }

    sema_error(sema, node->line, "unknown function '%s' with %d argument(s)", name, nargs);
    node->resolved_type = SL_TYPE_FLOAT;
    return SL_TYPE_FLOAT;
}

static RhSLType analyze_expr(RhSLSema* sema, RhSLNode* node) {
    if (!node) return SL_TYPE_VOID;

    switch (node->node_type) {
    case SL_NODE_FLOAT_LIT:
        node->resolved_type = SL_TYPE_FLOAT;
        return SL_TYPE_FLOAT;

    case SL_NODE_STRING_LIT:
        node->resolved_type = SL_TYPE_STRING;
        return SL_TYPE_STRING;

    case SL_NODE_IDENT: {
        RhSLSymbol* sym = lookup_symbol(sema, node->u.ident.name);
        if (!sym) {
            sema_error(sema, node->line, "undeclared variable '%s'",
                       node->u.ident.name);
            node->resolved_type = SL_TYPE_FLOAT;
            return SL_TYPE_FLOAT;
        }
        node->resolved_type = sym->type;
        return sym->type;
    }

    case SL_NODE_BINOP: {
        RhSLType lt = analyze_expr(sema, node->u.binop.left);
        RhSLType rt = analyze_expr(sema, node->u.binop.right);
        RhSLType result = resolve_binop_type(node->u.binop.op, lt, rt);
        if (result == SL_TYPE_VOID) {
            sema_error(sema, node->line, "incompatible types in binary op: %s and %s",
                       rh_sl_type_name(lt), rh_sl_type_name(rt));
            result = SL_TYPE_FLOAT;
        }
        node->resolved_type = result;
        return result;
    }

    case SL_NODE_UNOP: {
        RhSLType ot = analyze_expr(sema, node->u.unop.operand);
        if (node->u.unop.op == SL_UOP_NEG) {
            /* Negation preserves float or tuple type */
            if (ot != SL_TYPE_FLOAT && !is_tuple_type(ot)) {
                sema_error(sema, node->line, "cannot negate type %s",
                           rh_sl_type_name(ot));
                ot = SL_TYPE_FLOAT;
            }
            node->resolved_type = ot;
            return ot;
        } else { /* SL_UOP_NOT */
            node->resolved_type = SL_TYPE_FLOAT;
            return SL_TYPE_FLOAT;
        }
    }

    case SL_NODE_TERNARY: {
        analyze_expr(sema, node->u.ternary.cond);
        RhSLType tt = analyze_expr(sema, node->u.ternary.then_expr);
        RhSLType et = analyze_expr(sema, node->u.ternary.else_expr);
        /* Prefer then type if promotable */
        if (tt == et) {
            node->resolved_type = tt;
        } else if (can_promote(et, tt)) {
            node->resolved_type = tt;
        } else if (can_promote(tt, et)) {
            node->resolved_type = et;
        } else {
            sema_error(sema, node->line, "incompatible types in ternary: %s and %s",
                       rh_sl_type_name(tt), rh_sl_type_name(et));
            node->resolved_type = tt;
        }
        return node->resolved_type;
    }

    case SL_NODE_TYPECAST: {
        /* Type constructor: color(r,g,b), point(x,y,z), etc. */
        int nargs = 0;
        for (RhSLNode* a = node->u.typecast.args; a; a = a->next) {
            analyze_expr(sema, a);
            nargs++;
        }
        /* Valid: 1 arg (splat) or 3 args (components) */
        if (nargs != 1 && nargs != 3) {
            sema_error(sema, node->line, "type constructor '%s' requires 1 or 3 arguments, got %d",
                       rh_sl_type_name(node->u.typecast.type), nargs);
        }
        node->resolved_type = node->u.typecast.type;
        return node->u.typecast.type;
    }

    case SL_NODE_CALL:
        return analyze_call(sema, node);

    case SL_NODE_ARRAY_ACCESS: {
        RhSLType at = analyze_expr(sema, node->u.array_access.array);
        analyze_expr(sema, node->u.array_access.index);
        RhSLType elem = is_tuple_type(at) ? SL_TYPE_FLOAT : at;
        node->resolved_type = elem;
        return elem;
    }

    case SL_NODE_COMP_ACCESS: {
        RhSLType ot = analyze_expr(sema, node->u.comp_access.operand);
        if (!is_tuple_type(ot)) {
            sema_error(sema, node->line, "component access requires tuple type, got %s",
                       rh_sl_type_name(ot));
        }
        node->resolved_type = SL_TYPE_FLOAT;
        return SL_TYPE_FLOAT;
    }

    default:
        node->resolved_type = SL_TYPE_VOID;
        return SL_TYPE_VOID;
    }
}

/* ------------------------------------------------------------------ */
/*  Statement analysis                                                 */
/* ------------------------------------------------------------------ */

static void analyze_stmt(RhSLSema* sema, RhSLNode* node) {
    if (!node) return;

    switch (node->node_type) {
    case SL_NODE_VAR_DECL: {
        if (node->u.var_decl.init) {
            RhSLType init_t = analyze_expr(sema, node->u.var_decl.init);
            if (!can_promote(init_t, node->u.var_decl.type) &&
                init_t != SL_TYPE_VOID) {
                sema_error(sema, node->line, "cannot initialize %s with %s",
                           rh_sl_type_name(node->u.var_decl.type),
                           rh_sl_type_name(init_t));
            }
        }
        RhSLSymbol* sym = declare_symbol(sema, node->u.var_decl.name,
                                         node->u.var_decl.type, node->line);
        if (sym) sym->storage = node->u.var_decl.storage;
        node->resolved_type = node->u.var_decl.type;
        break;
    }

    case SL_NODE_ASSIGN: {
        RhSLType target_t = analyze_expr(sema, node->u.assign.target);
        RhSLType value_t = analyze_expr(sema, node->u.assign.value);
        check_writable(sema, node->u.assign.target);
        if (!can_promote(value_t, target_t) && value_t != SL_TYPE_VOID) {
            sema_error(sema, node->line, "cannot assign %s to %s",
                       rh_sl_type_name(value_t), rh_sl_type_name(target_t));
        }
        break;
    }

    case SL_NODE_COMPOUND_ASSIGN: {
        RhSLType target_t = analyze_expr(sema, node->u.compound_assign.target);
        RhSLType value_t = analyze_expr(sema, node->u.compound_assign.value);
        check_writable(sema, node->u.compound_assign.target);
        if (!can_promote(value_t, target_t) && value_t != SL_TYPE_VOID) {
            sema_error(sema, node->line, "incompatible types in compound assignment");
        }
        break;
    }

    case SL_NODE_IF:
        analyze_expr(sema, node->u.if_stmt.cond);
        analyze_stmt(sema, node->u.if_stmt.then_body);
        if (node->u.if_stmt.else_body)
            analyze_stmt(sema, node->u.if_stmt.else_body);
        break;

    case SL_NODE_WHILE:
        analyze_expr(sema, node->u.while_stmt.cond);
        sema->in_loop++;
        analyze_stmt(sema, node->u.while_stmt.body);
        sema->in_loop--;
        break;

    case SL_NODE_FOR:
        push_scope(sema);
        if (node->u.for_stmt.init)
            analyze_stmt(sema, node->u.for_stmt.init);
        if (node->u.for_stmt.cond)
            analyze_expr(sema, node->u.for_stmt.cond);
        sema->in_loop++;
        if (node->u.for_stmt.body)
            analyze_stmt(sema, node->u.for_stmt.body);
        sema->in_loop--;
        if (node->u.for_stmt.inc)
            analyze_stmt(sema, node->u.for_stmt.inc);
        pop_scope(sema);
        break;

    case SL_NODE_RETURN:
        if (node->u.ret.value) {
            RhSLType ret_t = analyze_expr(sema, node->u.ret.value);
            if (sema->current_return_type != SL_TYPE_VOID &&
                !can_promote(ret_t, sema->current_return_type)) {
                sema_error(sema, node->line, "return type mismatch: expected %s, got %s",
                           rh_sl_type_name(sema->current_return_type),
                           rh_sl_type_name(ret_t));
            }
        }
        break;

    case SL_NODE_BREAK:
        if (sema->in_loop <= 0) {
            sema_error(sema, node->line, "'break' outside of loop");
        }
        break;

    case SL_NODE_CONTINUE:
        if (sema->in_loop <= 0) {
            sema_error(sema, node->line, "'continue' outside of loop");
        }
        break;

    case SL_NODE_BLOCK:
        analyze_block(sema, node);
        break;

    case SL_NODE_ILLUMINANCE:
        if (sema->shader_type != SL_SHADER_SURFACE &&
            sema->shader_type != SL_SHADER_VOLUME) {
            sema_error(sema, node->line, "'illuminance' only allowed in surface/volume shaders");
        }
        if (node->u.illuminance.position)
            analyze_expr(sema, node->u.illuminance.position);
        if (node->u.illuminance.normal)
            analyze_expr(sema, node->u.illuminance.normal);
        if (node->u.illuminance.angle)
            analyze_expr(sema, node->u.illuminance.angle);
        sema->in_illuminance++;
        if (node->u.illuminance.body)
            analyze_stmt(sema, node->u.illuminance.body);
        sema->in_illuminance--;
        break;

    case SL_NODE_ILLUMINATE:
        if (sema->shader_type != SL_SHADER_LIGHT) {
            sema_error(sema, node->line, "'illuminate' only allowed in light shaders");
        }
        if (node->u.illuminate.position)
            analyze_expr(sema, node->u.illuminate.position);
        if (node->u.illuminate.axis)
            analyze_expr(sema, node->u.illuminate.axis);
        if (node->u.illuminate.angle)
            analyze_expr(sema, node->u.illuminate.angle);
        if (node->u.illuminate.body)
            analyze_stmt(sema, node->u.illuminate.body);
        break;

    case SL_NODE_SOLAR:
        if (sema->shader_type != SL_SHADER_LIGHT) {
            sema_error(sema, node->line, "'solar' only allowed in light shaders");
        }
        if (node->u.solar.axis)
            analyze_expr(sema, node->u.solar.axis);
        if (node->u.solar.angle)
            analyze_expr(sema, node->u.solar.angle);
        if (node->u.solar.body)
            analyze_stmt(sema, node->u.solar.body);
        break;

    default:
        /* Expression statement (calls, etc.) */
        analyze_expr(sema, node);
        break;
    }
}

static void analyze_block(RhSLSema* sema, RhSLNode* node) {
    if (!node || node->node_type != SL_NODE_BLOCK) return;
    push_scope(sema);
    for (RhSLNode* s = node->u.block.stmts; s; s = s->next) {
        analyze_stmt(sema, s);
    }
    pop_scope(sema);
}

/* ------------------------------------------------------------------ */
/*  Function analysis                                                  */
/* ------------------------------------------------------------------ */

static void analyze_function(RhSLSema* sema, RhSLNode* fn) {
    if (!fn || fn->node_type != SL_NODE_FUNCTION) return;

    RhSLType saved_return = sema->current_return_type;
    sema->current_return_type = fn->u.function.return_type;

    push_scope(sema);

    /* Declare formals */
    for (RhSLNode* f = fn->u.function.formals; f; f = f->next) {
        if (f->node_type == SL_NODE_FORMAL) {
            declare_symbol(sema, f->u.formal.name, f->u.formal.type, f->line);
        }
    }

    /* Analyze body */
    if (fn->u.function.body)
        analyze_block(sema, fn->u.function.body);

    pop_scope(sema);
    sema->current_return_type = saved_return;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

void rh_sl_sema_init(RhSLSema* sema) {
    memset(sema, 0, sizeof(*sema));
}

int rh_sl_sema_analyze(RhSLSema* sema, RhSLNode* shader) {
    if (!shader || shader->node_type != SL_NODE_SHADER) return -1;

    memset(sema, 0, sizeof(*sema));
    sema->shader_type = shader->u.shader.shader_type;
    sema->functions = shader->u.shader.functions;
    sema->current_return_type = SL_TYPE_VOID;

    /* Global scope: builtins + shader params */
    push_scope(sema);
    populate_builtins(sema);

    /* Declare shader parameters */
    for (RhSLNode* p = shader->u.shader.params; p; p = p->next) {
        if (p->node_type == SL_NODE_PARAM) {
            if (p->u.param.default_val)
                analyze_expr(sema, p->u.param.default_val);
            RhSLSymbol* sym = declare_symbol(sema, p->u.param.name,
                                             p->u.param.type, p->line);
            if (sym) {
                sym->storage = p->u.param.storage;
                sym->is_output = p->u.param.is_output;
            }
        }
    }

    /* Analyze user-defined functions */
    for (RhSLNode* fn = sema->functions; fn; fn = fn->next) {
        analyze_function(sema, fn);
    }

    /* Analyze shader body */
    if (shader->u.shader.body)
        analyze_block(sema, shader->u.shader.body);

    pop_scope(sema);

    return (sema->num_errors > 0) ? -1 : 0;
}
