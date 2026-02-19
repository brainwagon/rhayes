/*
 * rh_sldis.c - Shader bytecode disassembler
 *
 * Reads a .slo file and prints human-readable disassembly with symbolic
 * register names, jump labels, and constant/string references.
 *
 * Usage: sldis <file.slo>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rh_sl_slo.h"
#include "rh_sl_opcodes.h"
#include "rh_sl_vm.h"
#include "xpt.h"

/* --- Opcode metadata --- */

typedef enum {
    FMT_NONE,       /* no operands (halt, ret) */
    FMT_DST,        /* dst only (ambient) */
    FMT_DST_SRC1,   /* dst, src1 (unary float/tuple ops) */
    FMT_DST_SRC12,  /* dst, src1, src2 (binary ops) */
    FMT_FCONST,     /* dst, const_pool[src1] */
    FMT_VCONST,     /* dst, const_pool[src1..src1+2] */
    FMT_FMOV,       /* dst, src1 (float move) */
    FMT_VMOV,       /* dst, src1 (tuple move) */
    FMT_FTOV,       /* dst(tuple), src1(float) */
    FMT_JUMP,       /* dst = target PC */
    FMT_JUMP_COND,  /* dst = target PC, src1 = condition */
    FMT_ILLUM_BEGIN, /* illuminance/illuminate begin */
    FMT_ILLUM_END,  /* illuminance/illuminate end */
    FMT_TEXTURE,    /* dst, src1, string_table[src2] */
    FMT_NOISE_1F,   /* dst(float), src1(float) */
    FMT_NOISE_2F,   /* dst(float), src1(float), src2(float) */
    FMT_NOISE_3D,   /* dst(float), src1(point) */
    FMT_TRANSFORM,  /* dst, src1, string_table[src2] */
    FMT_PRINTF,     /* string_table[src1] */
    FMT_CALL,       /* dst = target PC */
    FMT_DU_DV,      /* dst(float), src1(float) */
    FMT_AREA,       /* dst(float), src1(point) */
    FMT_VCOMP,      /* dst(float), src1(tuple), flags=component */
    FMT_VSETCOMP,   /* dst(tuple)[flags], src1(float) */
    FMT_SPECULAR,   /* dst, src1(Nf), src2(roughness) */
    FMT_SOLAR_BEGIN, /* solar begin: dst=end_pc, src1=axis, src2=angle */
} OpcodeFormat;

typedef struct {
    const char *name;
    OpcodeFormat fmt;
} OpcodeInfo;

static OpcodeInfo opcode_table[256];

static void init_opcode_table(void)
{
    int i;
    for (i = 0; i < 256; i++) {
        opcode_table[i].name = NULL;
        opcode_table[i].fmt = FMT_NONE;
    }

    /* Float arithmetic (binary) */
    opcode_table[OP_FADD]   = (OpcodeInfo){"fadd",   FMT_DST_SRC12};
    opcode_table[OP_FSUB]   = (OpcodeInfo){"fsub",   FMT_DST_SRC12};
    opcode_table[OP_FMUL]   = (OpcodeInfo){"fmul",   FMT_DST_SRC12};
    opcode_table[OP_FDIV]   = (OpcodeInfo){"fdiv",   FMT_DST_SRC12};
    opcode_table[OP_FMOD]   = (OpcodeInfo){"fmod",   FMT_DST_SRC12};
    opcode_table[OP_FPOW]   = (OpcodeInfo){"fpow",   FMT_DST_SRC12};
    opcode_table[OP_FMIN]   = (OpcodeInfo){"fmin",   FMT_DST_SRC12};
    opcode_table[OP_FMAX]   = (OpcodeInfo){"fmax",   FMT_DST_SRC12};
    opcode_table[OP_FATAN2] = (OpcodeInfo){"fatan2", FMT_DST_SRC12};

    /* Float arithmetic (unary) */
    opcode_table[OP_FNEG]   = (OpcodeInfo){"fneg",   FMT_DST_SRC1};
    opcode_table[OP_FABS]   = (OpcodeInfo){"fabs",   FMT_DST_SRC1};
    opcode_table[OP_FSQRT]  = (OpcodeInfo){"fsqrt",  FMT_DST_SRC1};
    opcode_table[OP_FFLOOR] = (OpcodeInfo){"ffloor", FMT_DST_SRC1};
    opcode_table[OP_FCEIL]  = (OpcodeInfo){"fceil",  FMT_DST_SRC1};
    opcode_table[OP_FSIGN]  = (OpcodeInfo){"fsign",  FMT_DST_SRC1};
    opcode_table[OP_FROUND] = (OpcodeInfo){"fround", FMT_DST_SRC1};

    /* Trig/transcendental (unary) */
    opcode_table[OP_FSIN]   = (OpcodeInfo){"fsin",   FMT_DST_SRC1};
    opcode_table[OP_FCOS]   = (OpcodeInfo){"fcos",   FMT_DST_SRC1};
    opcode_table[OP_FTAN]   = (OpcodeInfo){"ftan",   FMT_DST_SRC1};
    opcode_table[OP_FASIN]  = (OpcodeInfo){"fasin",  FMT_DST_SRC1};
    opcode_table[OP_FACOS]  = (OpcodeInfo){"facos",  FMT_DST_SRC1};
    opcode_table[OP_FATAN]  = (OpcodeInfo){"fatan",  FMT_DST_SRC1};
    opcode_table[OP_FEXP]   = (OpcodeInfo){"fexp",   FMT_DST_SRC1};
    opcode_table[OP_FLOG]   = (OpcodeInfo){"flog",   FMT_DST_SRC1};

    /* Tuple ops (binary) */
    opcode_table[OP_VADD]   = (OpcodeInfo){"vadd",   FMT_DST_SRC12};
    opcode_table[OP_VSUB]   = (OpcodeInfo){"vsub",   FMT_DST_SRC12};
    opcode_table[OP_VMUL]   = (OpcodeInfo){"vmul",   FMT_DST_SRC12};
    opcode_table[OP_VDIV]   = (OpcodeInfo){"vdiv",   FMT_DST_SRC12};
    opcode_table[OP_VSMUL]  = (OpcodeInfo){"vsmul",  FMT_DST_SRC12};
    opcode_table[OP_VSDIV]  = (OpcodeInfo){"vsdiv",  FMT_DST_SRC12};
    opcode_table[OP_DOT]    = (OpcodeInfo){"dot",    FMT_DST_SRC12};
    opcode_table[OP_CROSS]  = (OpcodeInfo){"cross",  FMT_DST_SRC12};
    opcode_table[OP_FACEFORWARD] = (OpcodeInfo){"faceforward", FMT_DST_SRC12};
    opcode_table[OP_REFLECT] = (OpcodeInfo){"reflect", FMT_DST_SRC12};

    /* Tuple ops (unary) */
    opcode_table[OP_VNEG]      = (OpcodeInfo){"vneg",      FMT_DST_SRC1};
    opcode_table[OP_LENGTH]    = (OpcodeInfo){"length",    FMT_DST_SRC1};
    opcode_table[OP_NORMALIZE] = (OpcodeInfo){"normalize", FMT_DST_SRC1};
    opcode_table[OP_DISTANCE]  = (OpcodeInfo){"distance",  FMT_DST_SRC12};

    /* Component access */
    opcode_table[OP_VCOMP]    = (OpcodeInfo){"vcomp",    FMT_VCOMP};
    opcode_table[OP_VSETCOMP] = (OpcodeInfo){"vsetcomp", FMT_VSETCOMP};

    /* Comparison/logic */
    opcode_table[OP_FEQ]  = (OpcodeInfo){"feq",  FMT_DST_SRC12};
    opcode_table[OP_FNE]  = (OpcodeInfo){"fne",  FMT_DST_SRC12};
    opcode_table[OP_FLT]  = (OpcodeInfo){"flt",  FMT_DST_SRC12};
    opcode_table[OP_FLE]  = (OpcodeInfo){"fle",  FMT_DST_SRC12};
    opcode_table[OP_FGT]  = (OpcodeInfo){"fgt",  FMT_DST_SRC12};
    opcode_table[OP_FGE]  = (OpcodeInfo){"fge",  FMT_DST_SRC12};
    opcode_table[OP_AND]  = (OpcodeInfo){"and",  FMT_DST_SRC12};
    opcode_table[OP_OR]   = (OpcodeInfo){"or",   FMT_DST_SRC12};
    opcode_table[OP_NOT]  = (OpcodeInfo){"not",  FMT_DST_SRC1};

    /* Data movement */
    opcode_table[OP_FMOV]   = (OpcodeInfo){"fmov",   FMT_FMOV};
    opcode_table[OP_VMOV]   = (OpcodeInfo){"vmov",   FMT_VMOV};
    opcode_table[OP_FCONST] = (OpcodeInfo){"fconst", FMT_FCONST};
    opcode_table[OP_VCONST] = (OpcodeInfo){"vconst", FMT_VCONST};
    opcode_table[OP_FTOV]   = (OpcodeInfo){"ftov",   FMT_FTOV};

    /* Control flow */
    opcode_table[OP_JUMP]       = (OpcodeInfo){"jump",       FMT_JUMP};
    opcode_table[OP_JUMP_IF]    = (OpcodeInfo){"jump_if",    FMT_JUMP_COND};
    opcode_table[OP_JUMP_IFNOT] = (OpcodeInfo){"jump_ifnot", FMT_JUMP_COND};

    /* Lighting */
    opcode_table[OP_ILLUMINANCE_BEGIN] = (OpcodeInfo){"illuminance_begin", FMT_ILLUM_BEGIN};
    opcode_table[OP_ILLUMINANCE_END]   = (OpcodeInfo){"illuminance_end",   FMT_ILLUM_END};
    opcode_table[OP_ILLUMINATE_BEGIN]  = (OpcodeInfo){"illuminate_begin",  FMT_ILLUM_BEGIN};
    opcode_table[OP_ILLUMINATE_END]    = (OpcodeInfo){"illuminate_end",    FMT_ILLUM_END};
    opcode_table[OP_SOLAR_BEGIN]       = (OpcodeInfo){"solar_begin",       FMT_SOLAR_BEGIN};
    opcode_table[OP_SOLAR_END]         = (OpcodeInfo){"solar_end",         FMT_ILLUM_END};
    opcode_table[OP_AMBIENT]           = (OpcodeInfo){"ambient",           FMT_DST};
    opcode_table[OP_DIFFUSE]           = (OpcodeInfo){"diffuse",           FMT_DST_SRC1};
    opcode_table[OP_SPECULAR]          = (OpcodeInfo){"specular",          FMT_SPECULAR};

    /* Texture/noise */
    opcode_table[OP_TEXTURE]   = (OpcodeInfo){"texture",   FMT_TEXTURE};
    opcode_table[OP_SHADOW]    = (OpcodeInfo){"shadow",    FMT_TEXTURE};
    opcode_table[OP_ENVMAP]    = (OpcodeInfo){"envmap",    FMT_TEXTURE};
    opcode_table[OP_NOISE1]    = (OpcodeInfo){"noise1",    FMT_NOISE_1F};
    opcode_table[OP_NOISE2]    = (OpcodeInfo){"noise2",    FMT_NOISE_2F};
    opcode_table[OP_NOISE3]    = (OpcodeInfo){"noise3",    FMT_NOISE_3D};
    opcode_table[OP_PNOISE]    = (OpcodeInfo){"pnoise",    FMT_NOISE_2F};
    opcode_table[OP_CELLNOISE] = (OpcodeInfo){"cellnoise", FMT_NOISE_1F};

    /* Special ops */
    opcode_table[OP_TRANSFORM]      = (OpcodeInfo){"transform",      FMT_TRANSFORM};
    opcode_table[OP_NTRANSFORM]     = (OpcodeInfo){"ntransform",     FMT_TRANSFORM};
    opcode_table[OP_VTRANSFORM]     = (OpcodeInfo){"vtransform",     FMT_TRANSFORM};
    opcode_table[OP_TRANSFORM_INV]  = (OpcodeInfo){"transform_inv",  FMT_TRANSFORM};
    opcode_table[OP_NTRANSFORM_INV] = (OpcodeInfo){"ntransform_inv", FMT_TRANSFORM};
    opcode_table[OP_VTRANSFORM_INV] = (OpcodeInfo){"vtransform_inv", FMT_TRANSFORM};
    opcode_table[OP_DU]             = (OpcodeInfo){"du",             FMT_DU_DV};
    opcode_table[OP_DV]             = (OpcodeInfo){"dv",             FMT_DU_DV};
    opcode_table[OP_AREA]           = (OpcodeInfo){"area",           FMT_AREA};
    opcode_table[OP_CALCNORMAL]     = (OpcodeInfo){"calcnormal",    FMT_DST_SRC1};
    opcode_table[OP_PRINTF]         = (OpcodeInfo){"printf",         FMT_PRINTF};

    /* Function calls */
    opcode_table[OP_CALL] = (OpcodeInfo){"call", FMT_CALL};
    opcode_table[OP_RET]  = (OpcodeInfo){"ret",  FMT_NONE};

    /* Termination */
    opcode_table[OP_HALT] = (OpcodeInfo){"halt", FMT_NONE};
}

/* --- Builtin register names --- */

typedef struct {
    uint16_t reg;
    const char *name;
    int width;  /* 1=float, 3=tuple */
} BuiltinReg;

static const BuiltinReg builtin_regs[] = {
    { R_P,    "P",    3 },
    { R_N,    "N",    3 },
    { R_NG,   "Ng",   3 },
    { R_I,    "I",    3 },
    { R_E,    "E",    3 },
    { R_CS,   "Cs",   3 },
    { R_OS,   "Os",   3 },
    { R_CI,   "Ci",   3 },
    { R_OI,   "Oi",   3 },
    { R_S,    "s",    1 },
    { R_T,    "t",    1 },
    { R_U,    "u",    1 },
    { R_V,    "v",    1 },
    { R_DU,   "du",   1 },
    { R_DV,   "dv",   1 },
    { R_L,    "L",    3 },
    { R_CL,   "Cl",   3 },
    { R_PW,   "Pw",   3 },
    { R_PS,   "Ps",   3 },
    { R_DPDU, "dPdu", 3 },
    { R_DPDV, "dPdv", 3 },
    { 0, NULL, 0 }
};

static const char *reg_name(uint16_t reg, const RhSLProgram *prog, char *buf)
{
    int i;

    /* Check builtins */
    for (i = 0; builtin_regs[i].name; i++) {
        if (reg == builtin_regs[i].reg)
            return builtin_regs[i].name;
    }

    /* Check params */
    for (i = 0; i < prog->num_params; i++) {
        if ((uint16_t)prog->params[i].reg == reg)
            return prog->params[i].name;
    }

    /* Temporary */
    sprintf(buf, "r%u", reg);
    return buf;
}

/* --- Jump labels --- */

#define MAX_LABELS 256

static uint16_t label_pcs[MAX_LABELS];
static int num_labels;

static void collect_labels(const RhSLProgram *prog)
{
    int i, j;
    num_labels = 0;

    for (i = 0; i < prog->code_len; i++) {
        uint64_t instr = prog->code[i];
        uint8_t op = RH_SL_DECODE_OP(instr);
        uint16_t target = 0;
        int has_target = 0;

        switch (op) {
        case OP_JUMP:
        case OP_JUMP_IF:
        case OP_JUMP_IFNOT:
        case OP_CALL:
        case OP_ILLUMINANCE_BEGIN:
        case OP_ILLUMINATE_BEGIN:
        case OP_SOLAR_BEGIN:
            target = RH_SL_DECODE_DST(instr);
            has_target = 1;
            break;
        case OP_ILLUMINANCE_END:
        case OP_ILLUMINATE_END:
        case OP_SOLAR_END:
            target = RH_SL_DECODE_DST(instr);
            has_target = 1;
            break;
        default:
            break;
        }

        if (has_target && num_labels < MAX_LABELS) {
            /* Check for duplicates */
            int found = 0;
            for (j = 0; j < num_labels; j++) {
                if (label_pcs[j] == target) { found = 1; break; }
            }
            if (!found)
                label_pcs[num_labels++] = target;
        }
    }

    /* Sort labels by PC */
    for (i = 0; i < num_labels - 1; i++) {
        for (j = i + 1; j < num_labels; j++) {
            if (label_pcs[j] < label_pcs[i]) {
                uint16_t tmp = label_pcs[i];
                label_pcs[i] = label_pcs[j];
                label_pcs[j] = tmp;
            }
        }
    }
}

static int find_label(uint16_t pc)
{
    int i;
    for (i = 0; i < num_labels; i++) {
        if (label_pcs[i] == pc)
            return i;
    }
    return -1;
}

/* --- Shader type name --- */

static const char *shader_type_name(int type)
{
    switch (type) {
    case RH_SL_SHADER_SURFACE:      return "surface";
    case RH_SL_SHADER_LIGHT:        return "light";
    case RH_SL_SHADER_DISPLACEMENT: return "displacement";
    case RH_SL_SHADER_VOLUME:       return "volume";
    default:                         return "unknown";
    }
}

static const char *param_type_name(int type)
{
    switch (type) {
    case RH_SL_TYPE_FLOAT:  return "float";
    case RH_SL_TYPE_COLOR:  return "color";
    case RH_SL_TYPE_POINT:  return "point";
    case RH_SL_TYPE_VECTOR: return "vector";
    case RH_SL_TYPE_NORMAL: return "normal";
    case RH_SL_TYPE_MATRIX: return "matrix";
    case RH_SL_TYPE_STRING: return "string";
    default:                 return "?";
    }
}

/* --- Header printing --- */

static void print_header(const RhSLProgram *prog)
{
    int i;

    printf("; Shader: %s \"%s\"\n", shader_type_name(prog->shader_type),
           prog->shader_name);
    printf("; Registers: %d\n", prog->num_regs);

    printf("; Parameters: %d\n", prog->num_params);
    for (i = 0; i < prog->num_params; i++) {
        const RhSLParamInfo *p = &prog->params[i];
        printf(";   [%d]  %s %s = ", p->reg, param_type_name(p->type),
               p->name);
        if (p->num_components == 1) {
            if (p->default_idx >= 0 && p->default_idx < prog->const_count)
                printf("%g", (double)prog->const_pool[p->default_idx]);
            else
                printf("?");
        } else {
            int j;
            printf("(");
            for (j = 0; j < p->num_components; j++) {
                int idx = p->default_idx + j;
                if (j > 0) printf(", ");
                if (idx >= 0 && idx < prog->const_count)
                    printf("%g", (double)prog->const_pool[idx]);
                else
                    printf("?");
            }
            printf(")");
        }
        printf("\n");
    }

    printf("; Constants: %d floats\n", prog->const_count);
    for (i = 0; i < prog->const_count; i++)
        printf(";   [%d] %g\n", i, (double)prog->const_pool[i]);

    printf("; Strings: %d entries\n", prog->string_count);
    for (i = 0; i < prog->string_count; i++)
        printf(";   [%d] \"%s\"\n", i, prog->string_table[i]);

    printf("; Code: %d instructions\n", prog->code_len);
    printf("\n");
}

/* --- Instruction disassembly --- */

static void disassemble(const RhSLProgram *prog)
{
    int i;
    char buf_d[16], buf_s1[16], buf_s2[16];

    for (i = 0; i < prog->code_len; i++) {
        uint64_t instr = prog->code[i];
        uint8_t  op    = RH_SL_DECODE_OP(instr);
        uint8_t  flags = RH_SL_DECODE_FLAGS(instr);
        uint16_t dst   = RH_SL_DECODE_DST(instr);
        uint16_t src1  = RH_SL_DECODE_SRC1(instr);
        uint16_t src2  = RH_SL_DECODE_SRC2(instr);
        int lbl;
        const OpcodeInfo *info = &opcode_table[op];
        const char *mnem = info->name ? info->name : "???";
        const char *rd, *rs1, *rs2;

        /* Print label if this PC is a jump target */
        lbl = find_label((uint16_t)i);
        if (lbl >= 0)
            printf("L%d:\n", lbl);

        /* Print address and hex dump */
        printf("  %04x  %02x.%02x.%04x.%04x.%04x    %-15s",
               i, op, flags, dst, src1, src2, mnem);

        rd  = reg_name(dst,  prog, buf_d);
        rs1 = reg_name(src1, prog, buf_s1);
        rs2 = reg_name(src2, prog, buf_s2);

        switch (info->fmt) {
        case FMT_NONE:
            break;

        case FMT_DST:
            printf("%s", rd);
            break;

        case FMT_DST_SRC1:
            printf("%s, %s", rd, rs1);
            break;

        case FMT_DST_SRC12:
            printf("%s, %s, %s", rd, rs1, rs2);
            break;

        case FMT_FCONST:
            if (src1 < (uint16_t)prog->const_count)
                printf("%s, %g", rd, (double)prog->const_pool[src1]);
            else
                printf("%s, const[%u]", rd, src1);
            break;

        case FMT_VCONST:
            if (src1 + 2 < (uint16_t)prog->const_count)
                printf("%s, (%g, %g, %g)", rd,
                       (double)prog->const_pool[src1],
                       (double)prog->const_pool[src1 + 1],
                       (double)prog->const_pool[src1 + 2]);
            else
                printf("%s, const[%u..%u]", rd, src1, src1 + 2);
            break;

        case FMT_FMOV:
            printf("%s, %s", rd, rs1);
            break;

        case FMT_VMOV:
            printf("%s, %s", rd, rs1);
            break;

        case FMT_FTOV:
            printf("%s, %s", rd, rs1);
            break;

        case FMT_JUMP:
            lbl = find_label(dst);
            if (lbl >= 0)
                printf("L%d", lbl);
            else
                printf("%04x", dst);
            break;

        case FMT_JUMP_COND:
            lbl = find_label(dst);
            if (lbl >= 0)
                printf("L%d, %s", lbl, rs1);
            else
                printf("%04x, %s", dst, rs1);
            break;

        case FMT_ILLUM_BEGIN:
            lbl = find_label(dst);
            if (lbl >= 0)
                printf("L%d, %s, %s", lbl, rs1, rs2);
            else
                printf("%04x, %s, %s", dst, rs1, rs2);
            break;

        case FMT_SOLAR_BEGIN:
            lbl = find_label(dst);
            if (lbl >= 0)
                printf("L%d, %s, %s", lbl, rs1, rs2);
            else
                printf("%04x, %s, %s", dst, rs1, rs2);
            break;

        case FMT_ILLUM_END:
            lbl = find_label(dst);
            if (lbl >= 0)
                printf("L%d", lbl);
            else
                printf("%04x", dst);
            break;

        case FMT_TEXTURE:
            if (src2 < (uint16_t)prog->string_count)
                printf("%s, %s, \"%s\"", rd, rs1, prog->string_table[src2]);
            else
                printf("%s, %s, str[%u]", rd, rs1, src2);
            if (flags == 1) printf(" [filter_explicit]");
            else if (flags == 2) printf(" [4point]");
            break;

        case FMT_NOISE_1F:
            printf("%s, %s", rd, rs1);
            break;

        case FMT_NOISE_2F:
            printf("%s, %s, %s", rd, rs1, rs2);
            break;

        case FMT_NOISE_3D:
            printf("%s, %s", rd, rs1);
            break;

        case FMT_TRANSFORM:
            if (src2 < (uint16_t)prog->string_count)
                printf("%s, %s, \"%s\"", rd, rs1, prog->string_table[src2]);
            else
                printf("%s, %s, str[%u]", rd, rs1, src2);
            break;

        case FMT_PRINTF:
            if (src1 < (uint16_t)prog->string_count)
                printf("\"%s\"", prog->string_table[src1]);
            else
                printf("str[%u]", src1);
            break;

        case FMT_CALL:
            lbl = find_label(dst);
            if (lbl >= 0)
                printf("L%d", lbl);
            else
                printf("%04x", dst);
            break;

        case FMT_DU_DV:
            printf("%s, %s", rd, rs1);
            break;

        case FMT_AREA:
            printf("%s, %s", rd, rs1);
            break;

        case FMT_VCOMP:
            printf("%s, %s[%d]", rd, rs1, flags & 0x3);
            break;

        case FMT_VSETCOMP:
            printf("%s[%d], %s", rd, flags & 0x3, rs1);
            break;

        case FMT_SPECULAR:
            printf("%s, %s, %s", rd, rs1, rs2);
            break;
        }

        printf("\n");
    }
}

/* --- Main --- */

int main(int argc, char **argv)
{
    RhSLProgram *prog;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file.slo>\n", argv[0]);
        return 1;
    }

    xpt_init();
    xpt_set_level("", XPT_LEVEL_WARN);

    init_opcode_table();

    prog = rh_sl_slo_read(argv[1]);
    if (!prog) {
        xpt_error("sl.compiler", "failed to read '%s'", argv[1]);
        return 1;
    }

    print_header(prog);
    collect_labels(prog);
    disassemble(prog);

    rh_sl_program_free(prog);
    return 0;
}
