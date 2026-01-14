#ifndef RH_TEXTURE_H
#define RH_TEXTURE_H

#include "rh_config.h"

/**
 * Texture channel format specifier.
 */
typedef enum {
    RH_TEX_AUTO  = 0,   /**< Detect format from file */
    RH_TEX_GREY  = 1,   /**< 1 channel (grayscale) */
    RH_TEX_RGB   = 3,   /**< 3 channels (RGB) */
    RH_TEX_RGBA  = 4    /**< 4 channels (RGBA) */
} RhTextureFormat;

/**
 * Single mipmap level data.
 */
typedef struct {
    unsigned int width;
    unsigned int height;
    unsigned char* data;    /**< Row-major pixel data, width * height * channels bytes */
} RhMipLevel;

/**
 * Texture with complete mipmap pyramid.
 */
typedef struct {
    unsigned int base_width;    /**< Original image width */
    unsigned int base_height;   /**< Original image height */
    unsigned int channels;      /**< Number of channels (1, 3, or 4) */
    unsigned int num_levels;    /**< Number of mip levels (including base) */
    RhMipLevel* levels;         /**< Array of mip levels, [0] is base (largest) */
} RhTexture;

/**
 * Load a PNG texture and generate mipmaps.
 *
 * @param filename  Path to PNG file
 * @param format    Desired format (RH_TEX_AUTO to detect from file)
 * @return          Texture with mipmaps, or NULL on error
 */
RhTexture* rh_texture_load(const char* filename, RhTextureFormat format);

/**
 * Destroy a texture and free all memory.
 *
 * @param tex  Texture to destroy (NULL is safe)
 */
void rh_texture_destroy(RhTexture* tex);

/**
 * Calculate number of mip levels for given dimensions.
 *
 * @param width   Image width
 * @param height  Image height
 * @return        Number of mip levels
 */
unsigned int rh_texture_mip_count(unsigned int width, unsigned int height);

/**
 * Get a texel from a specific mip level with clamped coordinates.
 *
 * @param tex    Texture to sample
 * @param level  Mip level (0 = full resolution)
 * @param x      X coordinate (clamped to valid range)
 * @param y      Y coordinate (clamped to valid range)
 * @param out    Output buffer (must hold tex->channels bytes)
 */
void rh_texture_get_texel(const RhTexture* tex, unsigned int level,
                          int x, int y, unsigned char* out);

#endif /* RH_TEXTURE_H */
