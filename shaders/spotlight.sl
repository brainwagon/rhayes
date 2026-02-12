light spotlight(float intensity = 1; color lightcolor = 1;
                point from = point "shader" (0, 0, 0);
                point to = point "shader" (0, 0, 1);
                float coneangle = radians(30);
                float conedeltaangle = radians(5);
                float beamdistribution = 2) {
    illuminate(from, normalize(to - from), coneangle + conedeltaangle) {
        vector Lnorm = normalize(Ps - from);
        vector axis = normalize(to - from);
        float cosangle = Lnorm . axis;
        float atten = pow(max(cosangle, 0), beamdistribution);
        float a = acos(cosangle);
        if (a > coneangle) {
            float t = (a - coneangle) / conedeltaangle;
            atten = atten * (1 - t) * (1 - t);
        }
        Cl = intensity * lightcolor * atten;
        L = Ps - from;
    }
}
