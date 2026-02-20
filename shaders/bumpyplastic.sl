surface bumpyplastic(float Ka = 1; float Kd = 0.5; float Ks = 0.5;
                       float roughness = 0.1; color specularcolor = color(1,1,1);
                       string texturename = ""; float height=0.01) {
    normal Nf = faceforward(normalize(N), I);
    vector V = -normalize(I);
    float hblur = float texture(texturename, s, t, "width", 10.0) ;
    float h = float texture(texturename, s, t) ;
    P += hblur * height * Nf ;
    Nf = calculatenormal(P) ;
    Oi = Os;
    Ci = Os * (Cs * h * (Ka * ambient() + Kd * diffuse(Nf)) +
               specularcolor * Ks * specular(Nf, V, roughness));
}
