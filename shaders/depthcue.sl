volume depthcue(float mindistance = 0; float maxdistance = 1;
                color background = color(0, 0, 0)) {
    float depth = zcomp(P);
    float f;
    if (maxdistance > mindistance)
        f = clamp((depth - mindistance) / (maxdistance - mindistance), 0, 1);
    else
        f = 0;
    Ci = mix(Ci, background * Oi, f);
}
