#ifndef RIB_OUTPUT_H
#define RIB_OUTPUT_H

#include "ri.h"
#include "ri_callbacks.h"
#include <stdio.h>

// Initialize RIB output to a file
// binary_mode: 0 = ASCII (human readable), 1 = binary (compact)
// Returns 0 on success, -1 on error
int rib_output_begin(const char* filename, int binary_mode);

// Initialize RIB output to an existing stream
int rib_output_begin_stream(FILE* stream, int binary_mode);

// Finalize RIB output and close file (if opened by rib_output_begin)
void rib_output_end(void);

// Get the callback table for RIB output
// Use this with the RIB parser to write RIB instead of rendering
RiCallbacks* rib_output_get_callbacks(void);

#endif // RIB_OUTPUT_H
