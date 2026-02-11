#include "rh_noise.h"
#include <math.h>

/* ------------------------------------------------------------------ */
/*  Improved Perlin noise (Ken Perlin, 2002)                           */
/* ------------------------------------------------------------------ */

static const int perm[512] = {
    151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,
    140,36,103,30,69,142,8,99,37,240,21,10,23,190,6,148,
    247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,
    57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,
    74,165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,
    60,211,133,230,220,105,92,41,55,46,245,40,244,102,143,54,
    65,25,63,161,1,216,80,73,209,76,132,187,208,89,18,169,
    200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,
    52,217,226,250,124,123,5,202,38,147,118,126,255,82,85,212,
    207,206,59,227,47,16,58,17,182,189,28,42,223,183,170,213,
    119,248,152,2,44,154,163,70,221,153,101,155,167,43,172,9,
    129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,
    218,246,97,228,251,34,242,193,238,210,144,12,191,179,162,241,
    81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,157,
    184,84,204,176,115,121,50,45,127,4,150,254,138,236,205,93,
    222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180,
    /* repeat */
    151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,
    140,36,103,30,69,142,8,99,37,240,21,10,23,190,6,148,
    247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,
    57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,
    74,165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,
    60,211,133,230,220,105,92,41,55,46,245,40,244,102,143,54,
    65,25,63,161,1,216,80,73,209,76,132,187,208,89,18,169,
    200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,
    52,217,226,250,124,123,5,202,38,147,118,126,255,82,85,212,
    207,206,59,227,47,16,58,17,182,189,28,42,223,183,170,213,
    119,248,152,2,44,154,163,70,221,153,101,155,167,43,172,9,
    129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,
    218,246,97,228,251,34,242,193,238,210,144,12,191,179,162,241,
    81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,157,
    184,84,204,176,115,121,50,45,127,4,150,254,138,236,205,93,
    222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180
};

static float fade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

static float lerp(float t, float a, float b) {
    return a + t * (b - a);
}

static float grad1(int hash, float x) {
    return (hash & 1) ? -x : x;
}

static float grad2(int hash, float x, float y) {
    int h = hash & 3;
    float u = (h < 2) ? x : y;
    float v = (h < 2) ? y : x;
    return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}

static float grad3(int hash, float x, float y, float z) {
    int h = hash & 15;
    float u = (h < 8) ? x : y;
    float v = (h < 4) ? y : ((h == 12 || h == 14) ? x : z);
    return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

float rh_noise1(float x) {
    int X = (int)floorf(x) & 255;
    x -= floorf(x);
    float u = fade(x);
    float g0 = grad1(perm[X],     x);
    float g1 = grad1(perm[X + 1], x - 1.0f);
    /* Remap from [-1,1] to [0,1] */
    return lerp(u, g0, g1) * 0.5f + 0.5f;
}

float rh_noise2(float x, float y) {
    int X = (int)floorf(x) & 255;
    int Y = (int)floorf(y) & 255;
    x -= floorf(x);
    y -= floorf(y);
    float u = fade(x);
    float v = fade(y);
    int A  = perm[X]     + Y;
    int B  = perm[X + 1] + Y;
    float g00 = grad2(perm[A],     x,        y);
    float g10 = grad2(perm[B],     x - 1.0f, y);
    float g01 = grad2(perm[A + 1], x,        y - 1.0f);
    float g11 = grad2(perm[B + 1], x - 1.0f, y - 1.0f);
    float r = lerp(v, lerp(u, g00, g10), lerp(u, g01, g11));
    return r * 0.5f + 0.5f;
}

float rh_noise3(float x, float y, float z) {
    int X = (int)floorf(x) & 255;
    int Y = (int)floorf(y) & 255;
    int Z = (int)floorf(z) & 255;
    x -= floorf(x);
    y -= floorf(y);
    z -= floorf(z);
    float u = fade(x);
    float v = fade(y);
    float w = fade(z);
    int A  = perm[X]     + Y;
    int AA = perm[A]     + Z;
    int AB = perm[A + 1] + Z;
    int B  = perm[X + 1] + Y;
    int BA = perm[B]     + Z;
    int BB = perm[B + 1] + Z;
    float r = lerp(w,
        lerp(v,
            lerp(u, grad3(perm[AA],   x,        y,        z),
                    grad3(perm[BA],   x - 1.0f, y,        z)),
            lerp(u, grad3(perm[AB],   x,        y - 1.0f, z),
                    grad3(perm[BB],   x - 1.0f, y - 1.0f, z))),
        lerp(v,
            lerp(u, grad3(perm[AA+1], x,        y,        z - 1.0f),
                    grad3(perm[BA+1], x - 1.0f, y,        z - 1.0f)),
            lerp(u, grad3(perm[AB+1], x,        y - 1.0f, z - 1.0f),
                    grad3(perm[BB+1], x - 1.0f, y - 1.0f, z - 1.0f))));
    return r * 0.5f + 0.5f;
}

float rh_pnoise1(float x, float period) {
    if (period < 1.0f) period = 1.0f;
    int ip = (int)period;
    int X = ((int)floorf(x)) % ip;
    if (X < 0) X += ip;
    int X1 = (X + 1) % ip;
    float fx = x - floorf(x);
    float u = fade(fx);
    float g0 = grad1(perm[X  & 255], fx);
    float g1 = grad1(perm[X1 & 255], fx - 1.0f);
    return lerp(u, g0, g1) * 0.5f + 0.5f;
}

float rh_cellnoise1(float x) {
    int ix = (int)floorf(x);
    /* Simple integer hash -> float in [0,1] */
    unsigned int n = (unsigned int)ix;
    n = (n << 13) ^ n;
    n = n * (n * n * 15731u + 789221u) + 1376312589u;
    return (float)(n & 0x7fffffff) / (float)0x7fffffff;
}
