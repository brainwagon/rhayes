#ifndef RH_SL_AST_H
#define RH_SL_AST_H

/*
 * AST node types for the RenderMan Shading Language parser.
 *
 * Each node has a type tag and a union of type-specific fields.
 * Child nodes are stored as pointers.  Lists (parameter lists,
 * statement blocks, argument lists) use a linked-list via `next`.
 */

#include "rh_sl_lex.h"

/* ------------------------------------------------------------------ */
/*  AST node type enumeration                                          */
/* ------------------------------------------------------------------ */

typedef enum {
    SL_NODE_SHADER,         /* Top-level shader definition */
    SL_NODE_FUNCTION,       /* User-defined function */
    SL_NODE_PARAM,          /* Shader parameter (with default value) */
    SL_NODE_FORMAL,         /* Function formal parameter (no default) */
    SL_NODE_VAR_DECL,       /* Local variable declaration */
    SL_NODE_BLOCK,          /* { stmt; stmt; ... } */
    SL_NODE_ASSIGN,         /* lhs = rhs */
    SL_NODE_COMPOUND_ASSIGN,/* lhs += rhs, etc. */
    SL_NODE_BINOP,          /* binary operator (a + b, a * b, etc.) */
    SL_NODE_UNOP,           /* unary operator (-x, !x) */
    SL_NODE_TERNARY,        /* cond ? a : b */
    SL_NODE_CALL,           /* function/builtin call */
    SL_NODE_IF,             /* if (cond) then_body [else else_body] */
    SL_NODE_WHILE,          /* while (cond) body */
    SL_NODE_FOR,            /* for (init; cond; inc) body */
    SL_NODE_RETURN,         /* return [expr] */
    SL_NODE_BREAK,          /* break */
    SL_NODE_CONTINUE,       /* continue */
    SL_NODE_ILLUMINANCE,    /* illuminance(P, N, angle) body */
    SL_NODE_ILLUMINATE,     /* illuminate(from [, axis, angle]) body */
    SL_NODE_SOLAR,          /* solar([axis, angle]) body */
    SL_NODE_IDENT,          /* variable reference */
    SL_NODE_FLOAT_LIT,      /* numeric literal */
    SL_NODE_STRING_LIT,     /* string literal */
    SL_NODE_TYPECAST,       /* type constructor: color(r,g,b) */
    SL_NODE_ARRAY_ACCESS,   /* array[index] */
    SL_NODE_COMP_ACCESS     /* xcomp/ycomp/zcomp -- component access */
} RhSLNodeType;

/* ------------------------------------------------------------------ */
/*  Data type representation                                           */
/* ------------------------------------------------------------------ */

typedef enum {
    SL_TYPE_VOID = 0,
    SL_TYPE_FLOAT,
    SL_TYPE_COLOR,
    SL_TYPE_POINT,
    SL_TYPE_VECTOR,
    SL_TYPE_NORMAL,
    SL_TYPE_MATRIX,
    SL_TYPE_STRING
} RhSLType;

typedef enum {
    SL_STORAGE_DEFAULT = 0,
    SL_STORAGE_UNIFORM,
    SL_STORAGE_VARYING
} RhSLStorage;

/* ------------------------------------------------------------------ */
/*  Binary / unary operator tags                                       */
/* ------------------------------------------------------------------ */

typedef enum {
    SL_OP_ADD,          /* + */
    SL_OP_SUB,          /* - */
    SL_OP_MUL,          /* * */
    SL_OP_DIV,          /* / */
    SL_OP_DOT,          /* . (dot product) */
    SL_OP_CROSS,        /* ^ (cross product) */
    SL_OP_EQ,           /* == */
    SL_OP_NE,           /* != */
    SL_OP_LT,           /* < */
    SL_OP_GT,           /* > */
    SL_OP_LE,           /* <= */
    SL_OP_GE,           /* >= */
    SL_OP_AND,          /* && */
    SL_OP_OR            /* || */
} RhSLBinOp;

typedef enum {
    SL_UOP_NEG,         /* - (negate) */
    SL_UOP_NOT          /* ! (logical not) */
} RhSLUnaryOp;

typedef enum {
    SL_COMPOUND_ADD,    /* += */
    SL_COMPOUND_SUB,    /* -= */
    SL_COMPOUND_MUL,    /* *= */
    SL_COMPOUND_DIV     /* /= */
} RhSLCompoundOp;

/* ------------------------------------------------------------------ */
/*  Shader type (top-level declaration)                                */
/* ------------------------------------------------------------------ */

typedef enum {
    SL_SHADER_SURFACE = 1,
    SL_SHADER_LIGHT,
    SL_SHADER_DISPLACEMENT,
    SL_SHADER_VOLUME
} RhSLShaderType;

/* ------------------------------------------------------------------ */
/*  AST node                                                           */
/* ------------------------------------------------------------------ */

#define SL_MAX_NAME 64

typedef struct RhSLNode RhSLNode;

struct RhSLNode {
    RhSLNodeType node_type;
    int line;                       /* Source line for error reporting */
    RhSLType resolved_type;         /* Set by semantic analysis */

    /* Linked list for sibling nodes (params, stmts, args) */
    RhSLNode* next;

    union {
        /* SL_NODE_SHADER */
        struct {
            RhSLShaderType shader_type;
            char name[SL_MAX_NAME];
            RhSLNode* params;       /* Linked list of SL_NODE_PARAM */
            RhSLNode* body;         /* SL_NODE_BLOCK */
            RhSLNode* functions;    /* Linked list of SL_NODE_FUNCTION (preceding the shader) */
        } shader;

        /* SL_NODE_FUNCTION */
        struct {
            RhSLType return_type;
            char name[SL_MAX_NAME];
            RhSLNode* formals;      /* Linked list of SL_NODE_FORMAL */
            RhSLNode* body;         /* SL_NODE_BLOCK */
        } function;

        /* SL_NODE_PARAM */
        struct {
            RhSLType type;
            RhSLStorage storage;
            int is_output;
            char name[SL_MAX_NAME];
            RhSLNode* default_val;  /* Expression for default value */
            char space[SL_MAX_NAME]; /* Coordinate space (e.g., "shader") or "" */
            int array_size;         /* 0 = not array, >0 = fixed array */
        } param;

        /* SL_NODE_FORMAL (function parameter) */
        struct {
            RhSLType type;
            RhSLStorage storage;
            int is_output;
            char name[SL_MAX_NAME];
        } formal;

        /* SL_NODE_VAR_DECL */
        struct {
            RhSLType type;
            RhSLStorage storage;
            char name[SL_MAX_NAME];
            RhSLNode* init;         /* Initializer expression (or NULL) */
            char space[SL_MAX_NAME]; /* Coordinate space or "" */
            int array_size;         /* 0 = not array */
        } var_decl;

        /* SL_NODE_BLOCK */
        struct {
            RhSLNode* stmts;        /* Linked list of statements */
        } block;

        /* SL_NODE_ASSIGN */
        struct {
            RhSLNode* target;       /* LHS (ident or array_access) */
            RhSLNode* value;        /* RHS expression */
        } assign;

        /* SL_NODE_COMPOUND_ASSIGN */
        struct {
            RhSLCompoundOp op;
            RhSLNode* target;
            RhSLNode* value;
        } compound_assign;

        /* SL_NODE_BINOP */
        struct {
            RhSLBinOp op;
            RhSLNode* left;
            RhSLNode* right;
        } binop;

        /* SL_NODE_UNOP */
        struct {
            RhSLUnaryOp op;
            RhSLNode* operand;
        } unop;

        /* SL_NODE_TERNARY */
        struct {
            RhSLNode* cond;
            RhSLNode* then_expr;
            RhSLNode* else_expr;
        } ternary;

        /* SL_NODE_CALL */
        struct {
            char name[SL_MAX_NAME];
            RhSLNode* args;         /* Linked list of argument expressions */
        } call;

        /* SL_NODE_IF */
        struct {
            RhSLNode* cond;
            RhSLNode* then_body;
            RhSLNode* else_body;    /* NULL if no else */
        } if_stmt;

        /* SL_NODE_WHILE */
        struct {
            RhSLNode* cond;
            RhSLNode* body;
        } while_stmt;

        /* SL_NODE_FOR */
        struct {
            RhSLNode* init;         /* Init statement (assign or var_decl) */
            RhSLNode* cond;         /* Condition expression */
            RhSLNode* inc;          /* Increment (assign or compound_assign) */
            RhSLNode* body;
        } for_stmt;

        /* SL_NODE_RETURN */
        struct {
            RhSLNode* value;        /* Return expression (or NULL) */
        } ret;

        /* SL_NODE_ILLUMINANCE */
        struct {
            RhSLNode* position;     /* P (point) */
            RhSLNode* normal;       /* N or axis (vector/normal) -- can be NULL */
            RhSLNode* angle;        /* Cone angle -- can be NULL (defaults to PI/2) */
            RhSLNode* body;
        } illuminance;

        /* SL_NODE_ILLUMINATE */
        struct {
            RhSLNode* position;     /* from (point) */
            RhSLNode* axis;         /* Spot direction (or NULL for omni) */
            RhSLNode* angle;        /* Spot cone angle (or NULL) */
            RhSLNode* body;
        } illuminate;

        /* SL_NODE_SOLAR */
        struct {
            RhSLNode* axis;         /* Direction (or NULL for all-directions) */
            RhSLNode* angle;        /* Cone angle (or NULL) */
            RhSLNode* body;
        } solar;

        /* SL_NODE_IDENT */
        struct {
            char name[SL_MAX_NAME];
        } ident;

        /* SL_NODE_FLOAT_LIT */
        struct {
            float value;
        } float_lit;

        /* SL_NODE_STRING_LIT */
        struct {
            char value[RH_SL_MAX_TOKEN_LEN];
        } string_lit;

        /* SL_NODE_TYPECAST -- e.g., color(r, g, b) or point "world" (x, y, z) */
        struct {
            RhSLType type;
            char space[SL_MAX_NAME]; /* Coordinate space or "" */
            RhSLNode* args;          /* Linked list of component expressions */
        } typecast;

        /* SL_NODE_ARRAY_ACCESS -- e.g., arr[i] */
        struct {
            RhSLNode* array;        /* Expression yielding array */
            RhSLNode* index;        /* Index expression */
        } array_access;

        /* SL_NODE_COMP_ACCESS -- e.g., xcomp(v) */
        struct {
            int component;          /* 0=x, 1=y, 2=z */
            RhSLNode* operand;
        } comp_access;
    } u;
};

/* ------------------------------------------------------------------ */
/*  AST allocation and cleanup                                         */
/* ------------------------------------------------------------------ */

/* Allocate a new AST node (zeroed). */
RhSLNode* rh_sl_node_alloc(RhSLNodeType type, int line);

/* Free an AST node and all its children recursively. */
void rh_sl_node_free(RhSLNode* node);

/* Append a node to the end of a linked list.  Returns the new head. */
RhSLNode* rh_sl_node_append(RhSLNode* list, RhSLNode* node);

/* Count nodes in a linked list. */
int rh_sl_node_count(const RhSLNode* list);

/* Return human-readable name for a data type. */
const char* rh_sl_type_name(RhSLType type);

#endif /* RH_SL_AST_H */
