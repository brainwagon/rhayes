#ifndef RH_NOISE_H
#define RH_NOISE_H

/*
 * Perlin noise functions for the shading language VM.
 *
 * All functions return values in [0,1] per RenderMan spec.
 */

float rh_noise1(float x);
float rh_noise2(float x, float y);
float rh_noise3(float x, float y, float z);
float rh_pnoise1(float x, float period);
float rh_cellnoise1(float x);

#endif /* RH_NOISE_H */
