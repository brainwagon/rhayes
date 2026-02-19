surface bumpmatte(float Ka = 1; float Kd = 1; float Km = 0.05; float freq = 10.0) {
    /* Displace P along the surface normal by noise */
    float disp = Km * noise(freq * P);
    P = P + disp * normalize(N);
    N = calculatenormal(P);

    normal Nf = faceforward(normalize(N), I);
    Oi = Os;
    Ci = Os * Cs * (Ka * ambient() + Kd * diffuse(Nf));
}
