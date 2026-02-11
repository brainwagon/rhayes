surface plastic(float Ka = 1; float Kd = 0.5; float Ks = 0.5;
                float roughness = 0.1; color specularcolor = color(1,1,1)) {
    normal Nf = faceforward(normalize(N), I);
    vector V = -normalize(I);
    Oi = Os;
    Ci = Os * (Cs * (Ka * ambient() + Kd * diffuse(Nf)) +
               specularcolor * Ks * specular(Nf, V, roughness));
}
