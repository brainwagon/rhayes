light shadowspotlight(float intensity = 1; color lightcolor = 1;
                      point from = point "shader" (0, 0, 0);
                      point to = point "shader" (0, 0, 1);
                      float coneangle = radians(30);
                      float conedeltaangle = radians(5);
                      float beamdistribution = 2;
                      string shadowname = "") {
    uniform vector A = normalize(to - from);
    uniform float cosoutside = cos(coneangle);
    uniform float cosinside = cos(coneangle - conedeltaangle);
    illuminate(from, A, coneangle) {
        float cosangle = (L . A) / length(L);
        float atten = pow(cosangle, beamdistribution) / (L . L);
        atten *= smoothstep(cosoutside, cosinside, cosangle);
        float shad = shadow(shadowname, Pw);
        Cl = atten * intensity * lightcolor * (1 - shad);
    }
}
