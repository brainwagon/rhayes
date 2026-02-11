/*
 * rh_sl_slo.c -- .slo binary file writer and reader for compiled shaders.
 *
 * Serializes RhSLProgram to/from disk in a sequential binary format.
 * Uses host byte order (not portable across architectures).
 */

#include "rh_sl_slo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Helper: write a value of a given type. Returns 0 on success, -1 on error. */
#define WRITE_VAL(f, val) \
    (fwrite(&(val), sizeof(val), 1, (f)) == 1 ? 0 : -1)

#define READ_VAL(f, val) \
    (fread(&(val), sizeof(val), 1, (f)) == 1 ? 0 : -1)

/* ------------------------------------------------------------------ */
/*  Writer                                                             */
/* ------------------------------------------------------------------ */

int rh_sl_slo_write(const char* filename, const RhSLProgram* prog) {
    if (!filename || !prog) return -1;

    FILE* f = fopen(filename, "wb");
    if (!f) return -1;

    /* --- Header --- */
    /* Magic: write "RHSL" as 4 raw bytes */
    if (fwrite("RHSL", 1, 4, f) != 4) goto fail;

    uint16_t version = RH_SL_SLO_VERSION;
    if (WRITE_VAL(f, version) < 0) goto fail;

    uint8_t shader_type = (uint8_t)prog->shader_type;
    if (WRITE_VAL(f, shader_type) < 0) goto fail;

    uint16_t name_len = (uint16_t)(strlen(prog->shader_name) + 1);
    if (WRITE_VAL(f, name_len) < 0) goto fail;

    uint16_t num_params = (uint16_t)prog->num_params;
    if (WRITE_VAL(f, num_params) < 0) goto fail;

    uint16_t num_regs = (uint16_t)prog->num_regs;
    if (WRITE_VAL(f, num_regs) < 0) goto fail;

    uint32_t code_len = (uint32_t)prog->code_len;
    if (WRITE_VAL(f, code_len) < 0) goto fail;

    uint32_t const_count = (uint32_t)prog->const_count;
    if (WRITE_VAL(f, const_count) < 0) goto fail;

    uint16_t string_count = (uint16_t)prog->string_count;
    if (WRITE_VAL(f, string_count) < 0) goto fail;

    /* --- Shader name --- */
    if (fwrite(prog->shader_name, 1, name_len, f) != name_len) goto fail;

    /* --- Parameter table --- */
    for (int i = 0; i < prog->num_params; i++) {
        const RhSLParamInfo* p = &prog->params[i];

        uint16_t pname_len = (uint16_t)(strlen(p->name) + 1);
        if (WRITE_VAL(f, pname_len) < 0) goto fail;
        if (fwrite(p->name, 1, pname_len, f) != pname_len) goto fail;

        uint8_t ptype = (uint8_t)p->type;
        if (WRITE_VAL(f, ptype) < 0) goto fail;

        uint16_t preg = (uint16_t)p->reg;
        if (WRITE_VAL(f, preg) < 0) goto fail;

        uint32_t pdefault = (uint32_t)p->default_idx;
        if (WRITE_VAL(f, pdefault) < 0) goto fail;

        uint8_t pcomps = (uint8_t)p->num_components;
        if (WRITE_VAL(f, pcomps) < 0) goto fail;
    }

    /* --- Code section --- */
    if (prog->code_len > 0 && prog->code) {
        if (fwrite(prog->code, sizeof(uint64_t), prog->code_len, f)
            != (size_t)prog->code_len) goto fail;
    }

    /* --- Constant pool --- */
    if (prog->const_count > 0 && prog->const_pool) {
        if (fwrite(prog->const_pool, sizeof(float), prog->const_count, f)
            != (size_t)prog->const_count) goto fail;
    }

    /* --- String table --- */
    for (int i = 0; i < prog->string_count; i++) {
        const char* s = prog->string_table[i];
        uint16_t slen = (uint16_t)(strlen(s) + 1);
        if (WRITE_VAL(f, slen) < 0) goto fail;
        if (fwrite(s, 1, slen, f) != slen) goto fail;
    }

    fclose(f);
    return 0;

fail:
    fclose(f);
    return -1;
}

/* ------------------------------------------------------------------ */
/*  Reader                                                             */
/* ------------------------------------------------------------------ */

RhSLProgram* rh_sl_slo_read(const char* filename) {
    if (!filename) return NULL;

    FILE* f = fopen(filename, "rb");
    if (!f) return NULL;

    /* --- Read and validate header --- */
    char magic[4];
    if (fread(magic, 1, 4, f) != 4) goto fail_file;
    if (memcmp(magic, "RHSL", 4) != 0) goto fail_file;

    uint16_t version;
    if (READ_VAL(f, version) < 0) goto fail_file;
    if (version != RH_SL_SLO_VERSION) goto fail_file;

    uint8_t shader_type;
    if (READ_VAL(f, shader_type) < 0) goto fail_file;

    uint16_t name_len;
    if (READ_VAL(f, name_len) < 0) goto fail_file;
    if (name_len == 0 || name_len > 64) goto fail_file;

    uint16_t num_params;
    if (READ_VAL(f, num_params) < 0) goto fail_file;
    if (num_params > RH_SL_MAX_PARAMS) goto fail_file;

    uint16_t num_regs;
    if (READ_VAL(f, num_regs) < 0) goto fail_file;

    uint32_t code_len;
    if (READ_VAL(f, code_len) < 0) goto fail_file;

    uint32_t const_count;
    if (READ_VAL(f, const_count) < 0) goto fail_file;

    uint16_t string_count;
    if (READ_VAL(f, string_count) < 0) goto fail_file;

    /* --- Allocate program --- */
    RhSLProgram* prog = rh_sl_program_create();
    if (!prog) goto fail_file;

    prog->shader_type = shader_type;
    prog->num_regs = num_regs;
    prog->num_params = num_params;
    prog->code_len = (int)code_len;
    prog->const_count = (int)const_count;
    prog->string_count = (int)string_count;

    /* --- Shader name --- */
    if (fread(prog->shader_name, 1, name_len, f) != name_len) goto fail_prog;
    prog->shader_name[name_len - 1] = '\0';  /* ensure NUL */

    /* --- Parameter table --- */
    for (int i = 0; i < (int)num_params; i++) {
        RhSLParamInfo* p = &prog->params[i];

        uint16_t pname_len;
        if (READ_VAL(f, pname_len) < 0) goto fail_prog;
        if (pname_len == 0 || pname_len > 64) goto fail_prog;
        if (fread(p->name, 1, pname_len, f) != pname_len) goto fail_prog;
        p->name[pname_len - 1] = '\0';

        uint8_t ptype;
        if (READ_VAL(f, ptype) < 0) goto fail_prog;
        p->type = ptype;

        uint16_t preg;
        if (READ_VAL(f, preg) < 0) goto fail_prog;
        p->reg = preg;

        uint32_t pdefault;
        if (READ_VAL(f, pdefault) < 0) goto fail_prog;
        p->default_idx = (int)pdefault;

        uint8_t pcomps;
        if (READ_VAL(f, pcomps) < 0) goto fail_prog;
        p->num_components = pcomps;
    }

    /* --- Code section --- */
    if (code_len > 0) {
        prog->code = malloc(code_len * sizeof(uint64_t));
        if (!prog->code) goto fail_prog;
        if (fread(prog->code, sizeof(uint64_t), code_len, f) != code_len)
            goto fail_prog;
    }

    /* --- Constant pool --- */
    if (const_count > 0) {
        prog->const_pool = malloc(const_count * sizeof(float));
        if (!prog->const_pool) goto fail_prog;
        if (fread(prog->const_pool, sizeof(float), const_count, f) != const_count)
            goto fail_prog;
    }

    /* --- String table --- */
    if (string_count > 0) {
        prog->string_table = malloc(string_count * sizeof(char*));
        if (!prog->string_table) goto fail_prog;
        /* Zero so cleanup is safe on partial read */
        memset(prog->string_table, 0, string_count * sizeof(char*));

        for (int i = 0; i < (int)string_count; i++) {
            uint16_t slen;
            if (READ_VAL(f, slen) < 0) goto fail_prog;
            if (slen == 0) goto fail_prog;

            prog->string_table[i] = malloc(slen);
            if (!prog->string_table[i]) goto fail_prog;
            if (fread(prog->string_table[i], 1, slen, f) != slen) goto fail_prog;
            prog->string_table[i][slen - 1] = '\0';
        }
    }

    fclose(f);
    return prog;

fail_prog:
    rh_sl_program_free(prog);
fail_file:
    fclose(f);
    return NULL;
}
