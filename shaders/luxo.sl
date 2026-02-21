surface luxo(float Ka = 1; float Kd = 0.5; float Ks = 0.5;
                float roughness = 0.1; color specularcolor = color(1,1,1)) {
    normal Nf = faceforward(normalize(N), I);
    vector V = -normalize(I);
    color blue = color(0.03, 0.3, 1) ;
    color yellow = color(1, 1, 0) ;
    color red = color(1, 0, 0) ;
    point Pp = transform("current", "object", P) ;
    float x = xcomp(Pp) ;
    float y = ycomp(Pp) ;
    float z = zcomp(Pp) ;
    color c ;

    if (z > 0.33) {
        float rho = sqrt(x*x+y*y) ;
        float th = atan(y, x) ;
        float R = 0.6 ;
        float r = 0.3 ;
        float f = (R + r) / 2 + (R - r) / 2 * cos(5*th) ;
        if (rho - f < 0) {
            c = red ;
        } else {
            c = yellow ;
        }
    } else {
        if (z < -.33) {
            c = yellow ;
        } else {
            c = blue ;
        }
    }
        

    Oi = Os;
    Ci = Os * (c * (Ka * ambient() + Kd * diffuse(Nf)) +
               specularcolor * Ks * specular(Nf, V, roughness));
}
