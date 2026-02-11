surface paintedplastic(float Ka = 1; float Kd = 0.5; float Ks = 0.5;
                       float roughness = 0.1; color specularcolor = color(1,1,1);
                       string texturename = "") {
    normal Nf = faceforward(normalize(N), I);
    vector V = -normalize(I);
    color Ct = color texture(texturename, s, t);
    Oi = Os;
    Ci = Os * (Ct * (Ka * ambient() + Kd * diffuse(Nf)) +
               specularcolor * Ks * specular(Nf, V, roughness));
}
