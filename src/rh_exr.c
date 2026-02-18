#include "rh_exr.h"
#include "xpt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* stb_image exposes zlib decompression when STB_IMAGE_IMPLEMENTATION is defined */
extern char* stbi_zlib_decode_malloc(const char* buffer, int len, int* outlen);

/* --- Low-level I/O helpers --- */

static int read_u8(FILE* fp, unsigned char* out) {
    int c = fgetc(fp);
    if (c == EOF) return 0;
    *out = (unsigned char)c;
    return 1;
}

static int read_u32_le(FILE* fp, unsigned int* out) {
    unsigned char b[4];
    if (fread(b, 1, 4, fp) != 4) return 0;
    *out = (unsigned int)b[0] | ((unsigned int)b[1] << 8) |
           ((unsigned int)b[2] << 16) | ((unsigned int)b[3] << 24);
    return 1;
}

static int read_i32_le(FILE* fp, int* out) {
    unsigned int u;
    if (!read_u32_le(fp, &u)) return 0;
    memcpy(out, &u, sizeof(int));
    return 1;
}

static int read_u64_le(FILE* fp, unsigned long long* out) {
    unsigned char b[8];
    if (fread(b, 1, 8, fp) != 8) return 0;
    *out = 0;
    for (int i = 7; i >= 0; i--) {
        *out = (*out << 8) | b[i];
    }
    return 1;
}

/* --- Half-float to float conversion --- */

static float half_to_float(unsigned short h) {
    unsigned int sign = (h >> 15) & 1;
    unsigned int exp  = (h >> 10) & 0x1F;
    unsigned int mant = h & 0x3FF;
    unsigned int f;

    if (exp == 0) {
        if (mant == 0) {
            /* Signed zero */
            f = sign << 31;
        } else {
            /* Denormalized: convert to normalized float */
            float val = (float)mant / 1024.0f;
            val *= (1.0f / 16384.0f); /* 2^-14 */
            if (sign) val = -val;
            return val;
        }
    } else if (exp == 31) {
        /* Inf / NaN */
        f = (sign << 31) | 0x7F800000 | (mant << 13);
    } else {
        /* Normalized */
        f = (sign << 31) | ((exp + 112) << 23) | (mant << 13);
    }

    float result;
    memcpy(&result, &f, sizeof(float));
    return result;
}

/* --- EXR channel info --- */

#define EXR_MAX_CHANNELS 8

typedef struct {
    char name[64];
    int pixel_type;  /* 0=UINT, 1=HALF, 2=FLOAT */
    int x_sampling;
    int y_sampling;
} ExrChannelInfo;

float* rh_exr_load(const char* filename, int* out_w, int* out_h, int* out_channels) {
    FILE* fp = fopen(filename, "rb");
    if (!fp) {
        xpt_error("rh.exr", "Could not open EXR file '%s'", filename);
        return NULL;
    }

    /* Check magic number: 0x762F3101 (little-endian) */
    unsigned int magic;
    if (!read_u32_le(fp, &magic) || magic != 0x762F3101u) {
        xpt_error("rh.exr", "Invalid EXR magic in '%s'", filename);
        fclose(fp);
        return NULL;
    }

    /* Version field */
    unsigned int version;
    if (!read_u32_le(fp, &version)) {
        xpt_error("rh.exr", "Could not read EXR version in '%s'", filename);
        fclose(fp);
        return NULL;
    }

    /* Check for tiled flag (bit 9) — we don't support tiled */
    if (version & 0x200) {
        xpt_error("rh.exr", "Tiled EXR not supported in '%s'", filename);
        fclose(fp);
        return NULL;
    }

    /* Parse header attributes */
    int compression = -1;
    int data_x_min = 0, data_y_min = 0, data_x_max = 0, data_y_max = 0;
    ExrChannelInfo channels[EXR_MAX_CHANNELS];
    int num_channels = 0;
    int header_done = 0;

    while (!header_done) {
        /* Read attribute name (null-terminated string) */
        char attr_name[256];
        int ni = 0;
        unsigned char ch;
        while (read_u8(fp, &ch) && ch != 0 && ni < 255) {
            attr_name[ni++] = (char)ch;
        }
        attr_name[ni] = '\0';

        if (ni == 0) {
            /* Empty name = end of header */
            header_done = 1;
            break;
        }

        /* Read attribute type (null-terminated string) */
        char attr_type[256];
        int ti = 0;
        while (read_u8(fp, &ch) && ch != 0 && ti < 255) {
            attr_type[ti++] = (char)ch;
        }
        attr_type[ti] = '\0';

        /* Read attribute size */
        unsigned int attr_size;
        if (!read_u32_le(fp, &attr_size)) {
            fclose(fp);
            return NULL;
        }

        long attr_start = ftell(fp);

        if (strcmp(attr_name, "compression") == 0 && strcmp(attr_type, "compression") == 0) {
            if (!read_u8(fp, &ch)) { fclose(fp); return NULL; }
            compression = (int)ch;
        } else if (strcmp(attr_name, "dataWindow") == 0 && strcmp(attr_type, "box2i") == 0) {
            if (!read_i32_le(fp, &data_x_min) || !read_i32_le(fp, &data_y_min) ||
                !read_i32_le(fp, &data_x_max) || !read_i32_le(fp, &data_y_max)) {
                fclose(fp);
                return NULL;
            }
        } else if (strcmp(attr_name, "channels") == 0 && strcmp(attr_type, "chlist") == 0) {
            /* Parse channel list */
            num_channels = 0;
            long chlist_end = attr_start + (long)attr_size;

            while (ftell(fp) < chlist_end && num_channels < EXR_MAX_CHANNELS) {
                /* Channel name (null-terminated) */
                int ci = 0;
                while (read_u8(fp, &ch) && ch != 0 && ci < 63) {
                    channels[num_channels].name[ci++] = (char)ch;
                }
                channels[num_channels].name[ci] = '\0';

                if (ci == 0) break; /* End of channel list */

                /* pixel type (4 bytes), pLinear (1 byte), reserved (3 bytes),
                   xSampling (4 bytes), ySampling (4 bytes) */
                int ptype;
                unsigned char plinear, reserved[3];
                if (!read_i32_le(fp, &ptype)) { fclose(fp); return NULL; }
                if (!read_u8(fp, &plinear)) { fclose(fp); return NULL; }
                if (fread(reserved, 1, 3, fp) != 3) { fclose(fp); return NULL; }
                if (!read_i32_le(fp, &channels[num_channels].x_sampling)) { fclose(fp); return NULL; }
                if (!read_i32_le(fp, &channels[num_channels].y_sampling)) { fclose(fp); return NULL; }

                channels[num_channels].pixel_type = ptype;
                num_channels++;
            }
        }

        /* Seek to end of attribute data */
        fseek(fp, attr_start + (long)attr_size, SEEK_SET);
    }

    /* Validate parsed data */
    if (compression != 0 && compression != 3) {
        xpt_error("rh.exr", "Unsupported EXR compression %d in '%s' (only NONE=0, ZIP=3)",
                  compression, filename);
        fclose(fp);
        return NULL;
    }

    int width = data_x_max - data_x_min + 1;
    int height = data_y_max - data_y_min + 1;
    if (width <= 0 || height <= 0) {
        xpt_error("rh.exr", "Invalid EXR dimensions %dx%d in '%s'", width, height, filename);
        fclose(fp);
        return NULL;
    }

    /* Map channels to RGBA output indices.
       EXR stores channels in alphabetical order (A, B, G, R typically). */
    int ch_to_rgba[EXR_MAX_CHANNELS];
    for (int i = 0; i < num_channels; i++) {
        ch_to_rgba[i] = -1;
        if (strcmp(channels[i].name, "R") == 0) ch_to_rgba[i] = 0;
        else if (strcmp(channels[i].name, "G") == 0) ch_to_rgba[i] = 1;
        else if (strcmp(channels[i].name, "B") == 0) ch_to_rgba[i] = 2;
        else if (strcmp(channels[i].name, "A") == 0) ch_to_rgba[i] = 3;
    }

    /* Calculate bytes per channel per pixel for scanline data */
    int bytes_per_pixel_channel[EXR_MAX_CHANNELS];
    int scanline_uncompressed_size = 0;
    for (int i = 0; i < num_channels; i++) {
        int bpp = (channels[i].pixel_type == 1) ? 2 : 4; /* HALF=2, FLOAT/UINT=4 */
        bytes_per_pixel_channel[i] = bpp;
        scanline_uncompressed_size += bpp * width;
    }

    /* Determine scanlines per chunk */
    int lines_per_chunk = 1;
    if (compression == 3) lines_per_chunk = 16; /* ZIP uses 16-line chunks */

    /* Allocate output: always RGBA */
    int out_ch = 4;
    size_t npixels = (size_t)width * (size_t)height;
    float* output = (float*)calloc(npixels * (size_t)out_ch, sizeof(float));
    if (!output) {
        fclose(fp);
        return NULL;
    }

    /* Initialize alpha to 1.0 (in case file has no A channel) */
    for (size_t i = 0; i < npixels; i++) {
        output[i * 4 + 3] = 1.0f;
    }

    /* Read offset table */
    int num_chunks = (height + lines_per_chunk - 1) / lines_per_chunk;
    unsigned long long* offsets = (unsigned long long*)malloc((size_t)num_chunks * sizeof(unsigned long long));
    if (!offsets) {
        free(output);
        fclose(fp);
        return NULL;
    }
    for (int i = 0; i < num_chunks; i++) {
        if (!read_u64_le(fp, &offsets[i])) {
            free(offsets);
            free(output);
            fclose(fp);
            return NULL;
        }
    }

    /* Read scanline data chunks */
    for (int chunk = 0; chunk < num_chunks; chunk++) {
        fseek(fp, (long)offsets[chunk], SEEK_SET);

        int y_coord;
        unsigned int chunk_data_size;
        if (!read_i32_le(fp, &y_coord) || !read_u32_le(fp, &chunk_data_size)) {
            free(offsets);
            free(output);
            fclose(fp);
            return NULL;
        }

        int first_line = y_coord - data_y_min;
        int chunk_lines = lines_per_chunk;
        if (first_line + chunk_lines > height) {
            chunk_lines = height - first_line;
        }

        unsigned char* raw_data = NULL;
        int raw_size = 0;

        if (compression == 0) {
            /* NO_COMPRESSION: data is raw */
            raw_data = (unsigned char*)malloc(chunk_data_size);
            if (!raw_data || fread(raw_data, 1, chunk_data_size, fp) != chunk_data_size) {
                free(raw_data);
                free(offsets);
                free(output);
                fclose(fp);
                return NULL;
            }
            raw_size = (int)chunk_data_size;
        } else {
            /* ZIP compression */
            unsigned char* compressed = (unsigned char*)malloc(chunk_data_size);
            if (!compressed || fread(compressed, 1, chunk_data_size, fp) != chunk_data_size) {
                free(compressed);
                free(offsets);
                free(output);
                fclose(fp);
                return NULL;
            }

            raw_data = (unsigned char*)stbi_zlib_decode_malloc(
                (const char*)compressed, (int)chunk_data_size, &raw_size);
            free(compressed);

            if (!raw_data) {
                xpt_error("rh.exr", "ZIP decompression failed in EXR '%s'", filename);
                free(offsets);
                free(output);
                fclose(fp);
                return NULL;
            }

            /* EXR ZIP uses a predictor: reconstruct interleaved byte ordering.
               After zlib decompression, data is reordered:
               first all even-indexed bytes, then all odd-indexed bytes.
               Then delta-decoded. */
            int total_bytes = raw_size;

            /* Delta decode */
            for (int i = 1; i < total_bytes; i++) {
                raw_data[i] = (unsigned char)(raw_data[i] + raw_data[i - 1]);
            }

            /* Undo interleave: separate even/odd bytes into proper order */
            unsigned char* reorder = (unsigned char*)malloc((size_t)total_bytes);
            if (!reorder) {
                free(raw_data);
                free(offsets);
                free(output);
                fclose(fp);
                return NULL;
            }

            int half = (total_bytes + 1) / 2;
            for (int i = 0; i < total_bytes; i++) {
                int src_idx;
                if (i % 2 == 0) {
                    src_idx = i / 2;
                } else {
                    src_idx = half + i / 2;
                }
                if (src_idx < total_bytes) {
                    reorder[i] = raw_data[src_idx];
                }
            }

            free(raw_data);
            raw_data = reorder;
        }

        /* Parse scanline pixel data.
           EXR stores data per-channel-per-scanline (channel-planar within each scanline).
           For each scanline: ch0_pixel0, ch0_pixel1, ..., ch1_pixel0, ch1_pixel1, ... */
        int offset = 0;
        for (int line = 0; line < chunk_lines; line++) {
            int y = first_line + line;
            if (y < 0 || y >= height) {
                /* Skip this line's data */
                for (int ch = 0; ch < num_channels; ch++) {
                    offset += bytes_per_pixel_channel[ch] * width;
                }
                continue;
            }

            for (int ch = 0; ch < num_channels; ch++) {
                int rgba_idx = ch_to_rgba[ch];
                int bpp = bytes_per_pixel_channel[ch];

                for (int x = 0; x < width; x++) {
                    float val = 0.0f;

                    if (offset + bpp <= raw_size) {
                        if (channels[ch].pixel_type == 1) {
                            /* HALF float */
                            unsigned short hval = (unsigned short)raw_data[offset] |
                                                  ((unsigned short)raw_data[offset + 1] << 8);
                            val = half_to_float(hval);
                        } else if (channels[ch].pixel_type == 2) {
                            /* FLOAT */
                            memcpy(&val, &raw_data[offset], sizeof(float));
                        }
                    }

                    if (rgba_idx >= 0 && rgba_idx < 4) {
                        output[((size_t)y * (size_t)width + (size_t)x) * 4 + rgba_idx] = val;
                    }

                    offset += bpp;
                }
            }
        }

        free(raw_data);
    }

    free(offsets);
    fclose(fp);

    *out_w = width;
    *out_h = height;
    *out_channels = out_ch;
    return output;
}
