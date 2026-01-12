// catrib - Parse a RIB file and write it back out (potentially in different format)
#include "rib_parse.h"
#include "rib_output.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char* progname) {
    fprintf(stderr, "Usage: %s [options] <input.rib>\n", progname);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -o <file>   Output to file (default: stdout)\n");
    fprintf(stderr, "  -ascii      Force ASCII output (default)\n");
    fprintf(stderr, "  -binary     Force binary output (not yet implemented)\n");
    fprintf(stderr, "  -h          Show this help\n");
}

int main(int argc, char** argv) {
    const char* input_file = NULL;
    const char* output_file = NULL;
    int binary_mode = 0;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -o requires an argument\n");
                return 1;
            }
            output_file = argv[++i];
        } else if (strcmp(argv[i], "-ascii") == 0) {
            binary_mode = 0;
        } else if (strcmp(argv[i], "-binary") == 0) {
            binary_mode = 1;
            fprintf(stderr, "Warning: Binary output not yet implemented, using ASCII\n");
            binary_mode = 0;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        } else {
            if (input_file) {
                fprintf(stderr, "Error: Multiple input files not supported\n");
                return 1;
            }
            input_file = argv[i];
        }
    }

    if (!input_file) {
        fprintf(stderr, "Error: No input file specified\n");
        print_usage(argv[0]);
        return 1;
    }

    // Initialize RIB output
    if (rib_output_begin(output_file, binary_mode) < 0) {
        fprintf(stderr, "Error: Failed to open output\n");
        return 1;
    }

    // Get the output callbacks
    RiCallbacks* callbacks = rib_output_get_callbacks();

    // Create parser with output callbacks
    RibParser* parser = rib_parser_create(callbacks);
    if (!parser) {
        fprintf(stderr, "Error: Failed to create parser\n");
        rib_output_end();
        return 1;
    }

    // Parse the input file
    int result = rib_parser_parse_file(parser, input_file);

    if (result != 0) {
        const char* err = rib_parser_get_error(parser);
        if (err) {
            fprintf(stderr, "Parse error: %s\n", err);
        }
    }

    rib_parser_destroy(parser);
    rib_output_end();

    return result;
}
