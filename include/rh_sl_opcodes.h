#ifndef RH_SL_OPCODES_H
#define RH_SL_OPCODES_H

/*
 * Shading Language Bytecode Instruction Set
 *
 * Each instruction is an 8-byte (uint64_t) word:
 *   bits 63-56: opcode  (8 bits)
 *   bits 55-48: flags   (8 bits)
 *   bits 47-32: dst     (16 bits) - destination register
 *   bits 31-16: src1    (16 bits) - first source register or immediate index
 *   bits 15-0:  src2    (16 bits) - second source register or immediate index
 *
 * Flags bits:
 *   bits 0-4: component index (for VCOMP/VSETCOMP) or sub-opcode
 *   bits 5-7: reserved
 *
 * Tuple registers (color/point/vector/normal) occupy 3 consecutive float
 * register slots: reg+0 = x/r, reg+1 = y/g, reg+2 = z/b.
 */

#include <stdint.h>

/* --- Instruction encoding/decoding macros --- */

#define RH_SL_ENCODE(op, flags, dst, src1, src2)  \
    ( ((uint64_t)(op)    << 56) | \
      ((uint64_t)(flags) << 48) | \
      ((uint64_t)(dst)   << 32) | \
      ((uint64_t)(src1)  << 16) | \
      ((uint64_t)(src2)  <<  0) )

#define RH_SL_INSTR(op, dst, src1, src2) \
    RH_SL_ENCODE((op), 0, (dst), (src1), (src2))

#define RH_SL_INSTR_F(op, flags, dst, src1, src2) \
    RH_SL_ENCODE((op), (flags), (dst), (src1), (src2))

#define RH_SL_DECODE_OP(instr)    ((uint8_t)(((instr) >> 56) & 0xFF))
#define RH_SL_DECODE_FLAGS(instr) ((uint8_t)(((instr) >> 48) & 0xFF))
#define RH_SL_DECODE_DST(instr)   ((uint16_t)(((instr) >> 32) & 0xFFFF))
#define RH_SL_DECODE_SRC1(instr)  ((uint16_t)(((instr) >> 16) & 0xFFFF))
#define RH_SL_DECODE_SRC2(instr)  ((uint16_t)(((instr) >>  0) & 0xFFFF))

/* --- Opcode enumeration --- */

typedef enum {
    /* 0x01-0x0F: Float arithmetic */
    OP_FADD    = 0x01,  /* dst = src1 + src2 */
    OP_FSUB    = 0x02,  /* dst = src1 - src2 */
    OP_FMUL    = 0x03,  /* dst = src1 * src2 */
    OP_FDIV    = 0x04,  /* dst = src1 / src2 */
    OP_FNEG    = 0x05,  /* dst = -src1 */
    OP_FABS    = 0x06,  /* dst = abs(src1) */
    OP_FMOD    = 0x07,  /* dst = fmod(src1, src2) */
    OP_FPOW    = 0x08,  /* dst = pow(src1, src2) */
    OP_FSQRT   = 0x09,  /* dst = sqrt(src1) */
    OP_FMIN    = 0x0A,  /* dst = min(src1, src2) */
    OP_FMAX    = 0x0B,  /* dst = max(src1, src2) */
    OP_FFLOOR  = 0x0C,  /* dst = floor(src1) */
    OP_FCEIL   = 0x0D,  /* dst = ceil(src1) */
    OP_FSIGN   = 0x0E,  /* dst = sign(src1) */
    OP_FROUND  = 0x0F,  /* dst = round(src1) */

    /* 0x10-0x18: Trigonometric / transcendental */
    OP_FSIN    = 0x10,  /* dst = sin(src1) */
    OP_FCOS    = 0x11,  /* dst = cos(src1) */
    OP_FTAN    = 0x12,  /* dst = tan(src1) */
    OP_FASIN   = 0x13,  /* dst = asin(src1) */
    OP_FACOS   = 0x14,  /* dst = acos(src1) */
    OP_FATAN   = 0x15,  /* dst = atan(src1) */
    OP_FATAN2  = 0x16,  /* dst = atan2(src1, src2) */
    OP_FEXP    = 0x17,  /* dst = exp(src1) */
    OP_FLOG    = 0x18,  /* dst = log(src1) */

    /* 0x20-0x2F: Tuple operations (color/point/vector/normal) */
    OP_VADD       = 0x20,  /* dst = src1 + src2  (3-component) */
    OP_VSUB       = 0x21,  /* dst = src1 - src2 */
    OP_VMUL       = 0x22,  /* dst = src1 * src2  (component-wise) */
    OP_VDIV       = 0x23,  /* dst = src1 / src2  (component-wise) */
    OP_VNEG       = 0x24,  /* dst = -src1 */
    OP_VSMUL      = 0x25,  /* dst(tuple) = src1(tuple) * src2(float) */
    OP_VSDIV      = 0x26,  /* dst(tuple) = src1(tuple) / src2(float) */
    OP_DOT        = 0x27,  /* dst(float) = dot(src1, src2) */
    OP_CROSS      = 0x28,  /* dst = cross(src1, src2) */
    OP_LENGTH     = 0x29,  /* dst(float) = length(src1) */
    OP_NORMALIZE  = 0x2A,  /* dst = normalize(src1) */
    OP_DISTANCE   = 0x2B,  /* dst(float) = distance(src1, src2) */
    OP_FACEFORWARD = 0x2C, /* dst = faceforward(src1, src2) */
    OP_REFLECT    = 0x2D,  /* dst = reflect(src1, src2) */
    OP_VCOMP      = 0x2E,  /* dst(float) = src1[flags & 0x3] */
    OP_VSETCOMP   = 0x2F,  /* dst[flags & 0x3] = src1(float) */

    /* 0x30-0x38: Comparison and logic */
    OP_FEQ     = 0x30,  /* dst = (src1 == src2) ? 1.0 : 0.0 */
    OP_FNE     = 0x31,  /* dst = (src1 != src2) ? 1.0 : 0.0 */
    OP_FLT     = 0x32,  /* dst = (src1 < src2)  ? 1.0 : 0.0 */
    OP_FLE     = 0x33,  /* dst = (src1 <= src2) ? 1.0 : 0.0 */
    OP_FGT     = 0x34,  /* dst = (src1 > src2)  ? 1.0 : 0.0 */
    OP_FGE     = 0x35,  /* dst = (src1 >= src2) ? 1.0 : 0.0 */
    OP_AND     = 0x36,  /* dst = (src1 && src2) ? 1.0 : 0.0 */
    OP_OR      = 0x37,  /* dst = (src1 || src2) ? 1.0 : 0.0 */
    OP_NOT     = 0x38,  /* dst = (!src1) ? 1.0 : 0.0 */

    /* 0x40-0x44: Data movement */
    OP_FMOV    = 0x40,  /* dst = src1 (float copy) */
    OP_VMOV    = 0x41,  /* dst = src1 (tuple copy, 3 floats) */
    OP_FCONST  = 0x42,  /* dst = const_pool[src1] */
    OP_VCONST  = 0x43,  /* dst = const_pool[src1..src1+2] */
    OP_FTOV    = 0x44,  /* dst(tuple) = (src1, src1, src1) */

    /* 0x50-0x52: Control flow */
    OP_JUMP       = 0x50,  /* PC = dst (unconditional) */
    OP_JUMP_IF    = 0x51,  /* if (regs[src1] != 0) PC = dst */
    OP_JUMP_IFNOT = 0x52,  /* if (regs[src1] == 0) PC = dst */

    /* 0x60-0x68: Lighting */
    OP_ILLUMINANCE_BEGIN = 0x60, /* Begin illuminance loop */
    OP_ILLUMINANCE_END   = 0x61, /* Advance light; jump back if more */
    OP_ILLUMINATE_BEGIN  = 0x62, /* Begin illuminate block (light shader) */
    OP_ILLUMINATE_END    = 0x63, /* End illuminate block */
    OP_SOLAR_BEGIN       = 0x64, /* Begin solar block */
    OP_SOLAR_END         = 0x65, /* End solar block */
    OP_AMBIENT           = 0x66, /* dst = ambient() */
    OP_DIFFUSE           = 0x67, /* dst = diffuse(src1=Nf) */
    OP_SPECULAR          = 0x68, /* dst = specular(src1=Nf, src2=roughness) */

    /* 0x70-0x77: Texture and noise */
    OP_TEXTURE   = 0x70,  /* dst = texture(string_table[src2], s, t) */
    OP_SHADOW    = 0x71,  /* dst = shadow(string_table[src2], P) */
    OP_ENVMAP    = 0x72,  /* dst = environment(string_table[src2], dir) */
    OP_NOISE1    = 0x73,  /* dst = noise(src1) -- 1D */
    OP_NOISE2    = 0x74,  /* dst = noise(src1, src2) -- 2D */
    OP_NOISE3    = 0x75,  /* dst = noise(src1(point)) -- 3D */
    OP_PNOISE    = 0x76,  /* dst = pnoise(src1, src2) */
    OP_CELLNOISE = 0x77,  /* dst = cellnoise(src1) */

    /* 0x90-0x9F: Special operations */
    OP_TRANSFORM  = 0x90, /* dst = transform("space", src1); src2 = string index */
    OP_NTRANSFORM = 0x91, /* dst = ntransform("space", src1); src2 = string index */
    OP_VTRANSFORM = 0x92, /* dst = vtransform("space", src1); src2 = string index */
    OP_DU         = 0x93, /* dst = Du(src1) */
    OP_DV         = 0x94, /* dst = Dv(src1) */
    OP_AREA       = 0x95, /* dst = area(P) */
    OP_CALCNORMAL = 0x96, /* dst = calculatenormal(P) */
    OP_PRINTF     = 0x97, /* printf(string_table[src1], ...) */
    OP_TRANSFORM_INV  = 0x98, /* dst = transform from named space to current */
    OP_NTRANSFORM_INV = 0x99, /* dst = ntransform from named space to current */
    OP_VTRANSFORM_INV = 0x9A, /* dst = vtransform from named space to current */

    /* 0xA0-0xA1: Function calls */
    OP_CALL    = 0xA0,  /* Push PC; PC = dst; args already in registers */
    OP_RET     = 0xA1,  /* Pop PC (return from function) */

    /* 0xFF: Termination */
    OP_HALT    = 0xFF   /* End shader execution */
} RhSLOpcode;


/*
 * Fixed register assignments for built-in global variables.
 *
 * Tuple types (point, normal, vector, color) occupy 3 consecutive slots.
 * E.g., R_P occupies registers R_P, R_P+1, R_P+2 for x, y, z.
 */
enum {
    /* Surface position (camera space) - point, RW */
    R_P      = 0,   /* regs[0..2] = P.x, P.y, P.z */

    /* Surface normal (camera space) - normal, RW */
    R_N      = 3,   /* regs[3..5] = N.x, N.y, N.z */

    /* Geometric normal - normal, R */
    R_NG     = 6,   /* regs[6..8] = Ng.x, Ng.y, Ng.z */

    /* Incident ray direction - vector, R */
    R_I      = 9,   /* regs[9..11] = I.x, I.y, I.z */

    /* Camera position - point, R */
    R_E      = 12,  /* regs[12..14] = E.x, E.y, E.z */

    /* Surface color - color, RW */
    R_CS     = 15,  /* regs[15..17] = Cs.r, Cs.g, Cs.b */

    /* Surface opacity - color, RW */
    R_OS     = 18,  /* regs[18..20] = Os.r, Os.g, Os.b */

    /* Output incident color - color, RW */
    R_CI     = 21,  /* regs[21..23] = Ci.r, Ci.g, Ci.b */

    /* Output incident opacity - color, RW */
    R_OI     = 24,  /* regs[24..26] = Oi.r, Oi.g, Oi.b */

    /* Texture coordinates - float, RW */
    R_S      = 27,  /* regs[27] = s */
    R_T      = 28,  /* regs[28] = t */

    /* Parametric coordinates - float, R */
    R_U      = 29,  /* regs[29] = u */
    R_V      = 30,  /* regs[30] = v */

    /* Parametric derivatives - float, R */
    R_DU     = 31,  /* regs[31] = du */
    R_DV     = 32,  /* regs[32] = dv */

    /* Light direction (set during illuminance loop) - vector, R */
    R_L      = 33,  /* regs[33..35] = L.x, L.y, L.z */

    /* Light color (set during illuminance loop) - color, R */
    R_CL     = 36,  /* regs[36..38] = Cl.r, Cl.g, Cl.b */

    /* World-space position (for shadow lookup) - point, R */
    R_PW     = 39,  /* regs[39..41] = P_world.x, P_world.y, P_world.z */

    /* Surface point being illuminated (light shaders) - point, R */
    R_PS     = 42,  /* regs[42..44] = Ps.x, Ps.y, Ps.z */

    /* dPdu, dPdv derivatives - vector, R */
    R_DPDU   = 45,  /* regs[45..47] = dPdu.x, dPdu.y, dPdu.z */
    R_DPDV   = 48,  /* regs[48..50] = dPdv.x, dPdv.y, dPdv.z */

    /* First register available for shader parameters */
    R_PARAMS_START = 51
};

/* Shader type constants */
#define RH_SL_SHADER_SURFACE       1
#define RH_SL_SHADER_LIGHT         2
#define RH_SL_SHADER_DISPLACEMENT  3
#define RH_SL_SHADER_VOLUME        4

/* Maximum nesting depth for function calls */
#define RH_SL_MAX_CALL_DEPTH  16

/* Parameter type constants (for .slo file format) */
#define RH_SL_TYPE_FLOAT   1
#define RH_SL_TYPE_COLOR   2
#define RH_SL_TYPE_POINT   3
#define RH_SL_TYPE_VECTOR  4
#define RH_SL_TYPE_NORMAL  5
#define RH_SL_TYPE_MATRIX  6
#define RH_SL_TYPE_STRING  7

#endif /* RH_SL_OPCODES_H */
