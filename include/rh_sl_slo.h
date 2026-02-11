#ifndef RH_SL_SLO_H
#define RH_SL_SLO_H

/*
 * .slo file format reader/writer for compiled shading language programs.
 *
 * Binary format (sequential, host byte order):
 *
 *   Header:
 *     magic              4 bytes  "RHSL"
 *     version            uint16   (1)
 *     shader_type        uint8
 *     shader_name_len    uint16   (including NUL)
 *     num_params         uint16
 *     num_registers      uint16
 *     code_length        uint32   (number of uint64_t instructions)
 *     const_pool_length  uint32   (number of floats)
 *     string_table_count uint16
 *
 *   Shader name:         shader_name_len bytes (NUL-terminated)
 *
 *   Parameter table (num_params entries):
 *     name_len           uint16
 *     name               name_len bytes (NUL-terminated)
 *     type               uint8
 *     register_index     uint16
 *     default_value_idx  uint32
 *     num_components     uint8
 *
 *   Code section:        code_length * 8 bytes
 *   Constant pool:       const_pool_length * 4 bytes
 *   String table:        for each: str_len (uint16) + str (NUL-terminated)
 */

#include "rh_sl_vm.h"

#define RH_SL_SLO_MAGIC   0x4C535852  /* "RHSL" as bytes R,H,S,L */
#define RH_SL_SLO_VERSION 1

/* Write a compiled program to a .slo file. Returns 0 on success, -1 on error. */
int rh_sl_slo_write(const char* filename, const RhSLProgram* prog);

/* Read a .slo file into a new RhSLProgram. Returns NULL on error. */
RhSLProgram* rh_sl_slo_read(const char* filename);

#endif /* RH_SL_SLO_H */
