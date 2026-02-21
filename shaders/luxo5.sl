float sgn(float x)
{
    return (x > 0) ? 1 : ((x < 0) ? -1 : 0);
}

float sdPentagram(vector p; float r)
{
    /* constants (same numeric values as GLSL) */
    float k1x = 0.809016994; // cos(pi/5)
    float k2x = 0.309016994; // sin(pi/10)
    float k1y = 0.587785252; // sin(pi/5)
    float k2y = 0.951056516; // cos(pi/10)
    float k1z = 0.726542528; // tan(pi/5)

    vector v1 = vector( k1x, -k1y, 0);
    vector v2 = vector(-k1x, -k1y, 0);
    vector v3 = vector( k2x, -k2y, 0);

    p[0] = abs(p[0]);
    p -= 2 * max(v1 . p, 0) * v1;

    /* p -= 2.0*max(dot(v2,p),0.0)*v2; */
    p -= 2 * max(v2 . p, 0) * v2;

    /* p.x = abs(p.x); */
    p[0] = abs(p[0]);

    /* p.y -= r; */
    p[1] -= r;

    /* length(p - v3*clamp(dot(p,v3),0,k1z*r)) */
    float t = clamp(p . v3, 0, k1z * r);
    vector q = p - v3 * t;

    /* 2D cross (z-component): p.y*v3.x - p.x*v3.y */
    float cross2 = p[1] * v3[0] - p[0] * v3[1];

    return length(q) * sgn(cross2);
}

surface luxo5(float Ka = 1; float Kd = 0.5; float Ks = 0.5;
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
        setzcomp(Pp, 0.) ;
        if (sdPentagram(Pp, 0.6) < 0.)  {
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
