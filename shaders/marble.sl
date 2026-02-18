surface marble(
    float Ka = 1;
    float Kd = 0.8;
    float Ks = 0.2;
    float roughness = 0.08;
    color specularcolor = color(1, 1, 1);
    color veincolor = color(0.1, 0.08, 0.06);
    float scale = 4;
    float veinfreq = 1.5;
) {
    normal Nf = faceforward(normalize(N), I);
    vector V = -normalize(I);

    /* Multi-octave turbulence: sum noise at increasing frequencies */
    float t = noise(scale * P);
    t += 0.5 * noise(2.0 * scale * P);
    t += 0.25 * noise(4.0 * scale * P);
    t += 0.125 * noise(8.0 * scale * P);
    t = t / 1.875;

    /* Sine-wave vein bands along X, perturbed by turbulence */
    float stripe = sin(veinfreq * 3.14159265 * (xcomp(scale * P) + t));
    stripe = (stripe + 1.0) * 0.5;
    stripe = smoothstep(0.3, 0.7, stripe);

    /* Blend base color (Cs) toward vein color */
    color c = mix(Cs, veincolor, stripe);

    Oi = Os;
    Ci = Os * (c * (Ka * ambient() + Kd * diffuse(Nf)) +
               specularcolor * Ks * specular(Nf, V, roughness));
}
