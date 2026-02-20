volume fog(float distance = 1; color background = color(0, 0, 0)) {
    float depth = zcomp(P);
    float f;
    if (distance > 0)
        f = clamp(1 - exp(-depth / distance), 0, 1);
    else
        f = 0;
    Ci = mix(Ci, background * Oi, f);
}
