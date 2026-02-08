#ifndef RH_PFM_H
#define RH_PFM_H

/**
 * Load a PFM (Portable Float Map) image file.
 *
 * Supports both "PF" (3-channel RGB) and "Pf" (1-channel grayscale).
 * Returns interleaved float data in row-major order (top-to-bottom).
 *
 * @param filename  Path to PFM file
 * @param w         Output: image width
 * @param h         Output: image height
 * @param channels  Output: number of channels (1 or 3)
 * @return          Allocated float array (caller must free), or NULL on error
 */
float* rh_pfm_load(const char* filename, int* w, int* h, int* channels);

#endif /* RH_PFM_H */
