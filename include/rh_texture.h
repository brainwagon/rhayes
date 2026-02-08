#ifndef RH_TEXTURE_H
#define RH_TEXTURE_H

#include "rh_config.h"
#include "rh_image.h"  /* For RhColor */

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
    float* data;    /**< Row-major pixel data, width * height * channels floats */
} RhMipLevel;

/**
 * Texture with complete mipmap pyramid.
 */
typedef struct {
    unsigned int base_width;    /**< Original image width */
    unsigned int base_height;   /**< Original image height */
    unsigned int channels;      /**< Number of channels (1, 3, or 4) */
    unsigned int num_levels;    /**< Number of mip levels (including base) */
    int is_hdr;                 /**< Non-zero if loaded from HDR source */
    RhMipLevel* levels;         /**< Array of mip levels, [0] is base (largest) */
} RhTexture;

/**
 * Load a texture and generate mipmaps.
 *
 * @param filename  Path to image file
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
 * Sample texture with bilinear filtering at specified mip level.
 *
 * @param tex    Texture to sample
 * @param level  Mip level (0 = full resolution, clamped to valid range)
 * @param u      Normalized u coordinate [0,1] (wrapped to repeat)
 * @param v      Normalized v coordinate [0,1] (wrapped to repeat)
 * @return       Interpolated color (RGB)
 */
RhColor rh_texture_sample_bilinear(const RhTexture* tex, unsigned int level,
                                   float u, float v);

/**
 * Sample texture with automatic mip level selection based on derivatives.
 *
 * @param tex    Texture to sample
 * @param u      Normalized u coordinate [0,1]
 * @param v      Normalized v coordinate [0,1]
 * @param du     Derivative of u (filter width in u direction)
 * @param dv     Derivative of v (filter width in v direction)
 * @return       Filtered color
 */
RhColor rh_texture_sample(const RhTexture* tex, float u, float v,
                          float du, float dv);

/**
 * Sample texture with alpha, using automatic mip level selection.
 *
 * @param tex        Texture to sample
 * @param u          Normalized u coordinate [0,1]
 * @param v          Normalized v coordinate [0,1]
 * @param du         Derivative of u (filter width in u direction)
 * @param dv         Derivative of v (filter width in v direction)
 * @param out_alpha  If non-NULL, receives interpolated alpha (1.0 for non-alpha textures)
 * @return           Filtered color (RGB)
 */
RhColor rh_texture_sample_with_alpha(const RhTexture* tex, float u, float v,
                                     float du, float dv, float* out_alpha);

#endif /* RH_TEXTURE_H */
