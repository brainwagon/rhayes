surface 
noisetest(float Ka = 1; float Kd = 0.5; float Ks = 0.5;
                float roughness = 0.1; color specularcolor = color(1,1,1)) 
                {
    normal Nf = faceforward(normalize(N), I);
    vector V = -normalize(I);
    color c = color noise(10.0*P) ;
    Oi = Os;
    Ci = Os * (noise(10.0*P) * Cs * (Ka * ambient() + Kd * diffuse(Nf)) +
               specularcolor * Ks * specular(Nf, V, roughness));
}
