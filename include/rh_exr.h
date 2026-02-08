#ifndef RH_EXR_H
#define RH_EXR_H

/**
 * Load a minimal OpenEXR image file.
 *
 * Supports:
 * - Scanline format only (no tiled)
 * - NO_COMPRESSION (0) and ZIP compression (3)
 * - HALF (1) and FLOAT (2) pixel types
 * - Channels mapped by name: R, G, B, A
 *
 * Returns interleaved RGBA float data in row-major order (top-to-bottom).
 *
 * @param filename  Path to EXR file
 * @param w         Output: image width
 * @param h         Output: image height
 * @param channels  Output: number of channels (always 4 for RGBA)
 * @return          Allocated float array (caller must free), or NULL on error
 */
float* rh_exr_load(const char* filename, int* w, int* h, int* channels);

#endif /* RH_EXR_H */
