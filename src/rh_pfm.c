#include "rh_pfm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Check if this machine is little-endian */
static int is_little_endian(void) {
    unsigned int x = 1;
    return *((unsigned char*)&x) == 1;
}

/* Byte-swap a 32-bit value */
static void swap4(void* p) {
    unsigned char* b = (unsigned char*)p;
    unsigned char t;
    t = b[0]; b[0] = b[3]; b[3] = t;
    t = b[1]; b[1] = b[2]; b[2] = t;
}

float* rh_pfm_load(const char* filename, int* out_w, int* out_h, int* out_channels) {
    FILE* fp = fopen(filename, "rb");
    if (!fp) {
        fprintf(stderr, "Error: Could not open PFM file '%s'\n", filename);
        return NULL;
    }

    /* Read magic: "PF" = 3-channel RGB, "Pf" = 1-channel grayscale */
    char magic[3];
    if (!fgets(magic, sizeof(magic), fp)) {
        fprintf(stderr, "Error: Could not read PFM header from '%s'\n", filename);
        fclose(fp);
        return NULL;
    }

    int channels;
    if (magic[0] == 'P' && magic[1] == 'F') {
        channels = 3;
    } else if (magic[0] == 'P' && magic[1] == 'f') {
        channels = 1;
    } else {
        fprintf(stderr, "Error: Invalid PFM magic in '%s'\n", filename);
        fclose(fp);
        return NULL;
    }

    /* Skip newline after magic */
    int c = fgetc(fp);
    while (c == '\r') c = fgetc(fp);
    if (c != '\n') ungetc(c, fp);

    /* Read width and height */
    int width, height;
    if (fscanf(fp, "%d %d", &width, &height) != 2 || width <= 0 || height <= 0) {
        fprintf(stderr, "Error: Invalid PFM dimensions in '%s'\n", filename);
        fclose(fp);
        return NULL;
    }

    /* Skip to scale line */
    c = fgetc(fp);
    while (c == '\r' || c == '\n') c = fgetc(fp);
    ungetc(c, fp);

    /* Read scale factor: sign indicates endianness
       Positive = big-endian, Negative = little-endian */
    float scale;
    if (fscanf(fp, "%f", &scale) != 1) {
        fprintf(stderr, "Error: Invalid PFM scale in '%s'\n", filename);
        fclose(fp);
        return NULL;
    }

    int file_is_le = (scale < 0.0f);
    float abs_scale = (scale < 0.0f) ? -scale : scale;
    (void)abs_scale; /* Scale factor is typically 1.0, used for normalization if needed */

    /* Skip single whitespace char after scale */
    fgetc(fp);

    /* Read raw float data */
    size_t npixels = (size_t)width * (size_t)height;
    size_t nfloats = npixels * (size_t)channels;
    float* raw = (float*)malloc(nfloats * sizeof(float));
    if (!raw) {
        fprintf(stderr, "Error: Could not allocate PFM data for '%s'\n", filename);
        fclose(fp);
        return NULL;
    }

    if (fread(raw, sizeof(float), nfloats, fp) != nfloats) {
        fprintf(stderr, "Error: Short read of PFM data from '%s'\n", filename);
        free(raw);
        fclose(fp);
        return NULL;
    }
    fclose(fp);

    /* Byte-swap if file endianness differs from machine */
    int machine_is_le = is_little_endian();
    if (file_is_le != machine_is_le) {
        for (size_t i = 0; i < nfloats; i++) {
            swap4(&raw[i]);
        }
    }

    /* PFM stores rows bottom-to-top; flip to top-to-bottom */
    size_t row_size = (size_t)width * (size_t)channels;
    float* row_tmp = (float*)malloc(row_size * sizeof(float));
    if (!row_tmp) {
        free(raw);
        return NULL;
    }
    for (int y = 0; y < height / 2; y++) {
        int y2 = height - 1 - y;
        memcpy(row_tmp, &raw[y * row_size], row_size * sizeof(float));
        memcpy(&raw[y * row_size], &raw[y2 * row_size], row_size * sizeof(float));
        memcpy(&raw[y2 * row_size], row_tmp, row_size * sizeof(float));
    }
    free(row_tmp);

    *out_w = width;
    *out_h = height;
    *out_channels = channels;
    return raw;
}
