surface teapotplastic(float Ka = 1; float Kd = 0.5; float Ks = 0.5;
                       float roughness = 0.1; color specularcolor = color(1,1,1);
                       string texturename = "") {
    normal Nf = faceforward(normalize(N), I);
    vector V = -normalize(I);
    color Ct = color texture(texturename, s, t);
    Oi = Os * noise(11.0*P);
    Ci = Os * (color noise(10.0*P) * (Ka * ambient() + Kd * diffuse(Nf)) +
               specularcolor * Ks * specular(Nf, V, roughness));
}
