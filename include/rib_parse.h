#ifndef RIB_PARSE_H
#define RIB_PARSE_H

#include "ri.h"
#include "ri_callbacks.h"
#include <stdio.h>

// Opaque parser handle
typedef struct RibParser RibParser;

// Create a RIB parser with the specified callback table
// Returns NULL on error
RibParser* rib_parser_create(RiCallbacks* callbacks);

// Destroy a RIB parser and free resources
void rib_parser_destroy(RibParser* parser);

// Parse a RIB file
// Returns 0 on success, non-zero on error
int rib_parser_parse_file(RibParser* parser, const char* filename);

// Parse from an already-opened stream
int rib_parser_parse_stream(RibParser* parser, FILE* stream);

// Enable ReadArchive expansion (inline included files instead of passing through)
void rib_parser_set_expand_archives(RibParser* parser, int expand);

// Get the last error message (NULL if no error)
const char* rib_parser_get_error(RibParser* parser);

// Get the current line number (useful for error reporting)
int rib_parser_get_line(RibParser* parser);

#endif // RIB_PARSE_H
