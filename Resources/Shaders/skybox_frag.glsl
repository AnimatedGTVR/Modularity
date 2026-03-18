#version 330 core
out vec4 FragColor;
in vec3 fragPos;

uniform float timeOfDay;

float hash(vec3 p) {
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

vec3 getSkyColor(vec3 dir, float tod) {
    float height = dir.y;

    float t = fract(tod);
    float angle = t * 6.28318530718;
    vec3 sunDir = normalize(vec3(cos(angle), sin(angle) * 0.5, sin(angle)));
    float sunDot = max(dot(dir, sunDir), 0.0);

    vec3 nightTop       = vec3(0.01, 0.03, 0.08);
    vec3 nightHorizon   = vec3(0.04, 0.06, 0.12);
    vec3 dayTop         = vec3(0.25, 0.55, 1.00);
    vec3 dayHorizon     = vec3(0.75, 0.88, 1.00);
    vec3 sunriseTop     = vec3(0.45, 0.32, 0.55);
    vec3 sunriseHorizon = vec3(1.10, 0.60, 0.30);
    vec3 sunsetTop      = vec3(0.55, 0.35, 0.45);
    vec3 sunsetHorizon  = vec3(1.05, 0.45, 0.20);

    vec3 skyTop, skyHorizon;
    if (t < 0.25) {
        float f = t * 4.0;
        skyTop     = mix(nightTop,     sunriseTop,     f);
        skyHorizon = mix(nightHorizon, sunriseHorizon, f);
    } else if (t < 0.5) {
        float f = (t-0.25)*4.0;
        skyTop     = mix(sunriseTop,     dayTop,     f);
        skyHorizon = mix(sunriseHorizon, dayHorizon, f);
    } else if (t < 0.75) {
        float f = (t-0.5)*4.0;
        skyTop     = mix(dayTop,     sunsetTop,     f);
        skyHorizon = mix(dayHorizon, sunsetHorizon, f);
    } else {
        float f = (t-0.75)*4.0;
        skyTop     = mix(sunsetTop,     nightTop,     f);
        skyHorizon = mix(sunsetHorizon, nightHorizon, f);
    }

    float horizonMix = smoothstep(-0.2, 0.45, height);
    vec3 skyColor = mix(skyHorizon, skyTop, horizonMix);

    // Warm haze near the horizon for softer day transitions
    float hazeAmount = exp(-max(height, 0.0) * 12.0);
    vec3 hazeColor = mix(skyHorizon * 0.8, vec3(1.0, 0.95, 0.9), 0.6);

    vec3 sunCol = vec3(1.0, 0.95, 0.8);
    float sunGlow = pow(sunDot, 128.0) * 2.5;
    float sunDisc = smoothstep(0.9995, 0.9998, sunDot);
    float atmGlow = pow(sunDot, 8.0) * 0.6;
    float mie = pow(sunDot, 12.0);

    float sunVisibility = smoothstep(-0.12, 0.15, sunDir.y); // smooth fade in/out
    skyColor += sunCol * (sunDisc + atmGlow + sunGlow * 0.35) * sunVisibility;

    // Forward-scattered haze brightens the horizon when the sun is low
    vec3 forwardHaze = hazeColor * mie * 0.25 * sunVisibility;
    skyColor += forwardHaze;
    skyColor += hazeColor * hazeAmount * 0.18 * (0.3 + sunVisibility);

    float night = 1.0 - sunVisibility;
    if (night > 0.0) {
        vec3 p = dir * 160.0;
        vec3 i = floor(p);
        vec3 f = fract(p);

        float stars = 0.0;
        for (int z=-1; z<=1; z++)
        for (int y=-1; y<=1; y++)
        for (int x=-1; x<=1; x++) {
            vec3 o = vec3(x,y,z);
            vec3 pos = i + o;
            float h = hash(pos);
            if (h > 0.99) {
                vec3 center = o + 0.5 + (hash(pos+vec3(7,13,21))-0.5)*0.8;
                float d = length(f - center);
                float star = 1.0 - smoothstep(0.0, 0.12, d);
                star *= (h - 0.99)*100.0;
                stars += star;
            }
        }
        stars = pow(stars, 1.4);
        stars *= night;
        stars *= smoothstep(-0.1, 0.25, height);

        // Subtle Milky Way band to add structure to the night sky
        vec3 galaxyAxis = normalize(vec3(0.2, 0.85, -0.5));
        float galaxyBand = 1.0 - abs(dot(dir, galaxyAxis)); // highest when perpendicular to the axis
        galaxyBand = pow(max(galaxyBand, 0.0), 2.5); // soften and widen
        vec3 galaxyCol = vec3(0.65, 0.78, 1.0) * galaxyBand * 0.5;
        galaxyCol *= smoothstep(-0.15, 0.25, height);

        skyColor += vec3(1.0, 0.95, 0.9) * stars * 2.5 + galaxyCol;
    }

    return skyColor;
}

void main() {
    vec3 dir = normalize(fragPos);
    vec3 col = getSkyColor(dir, timeOfDay);
    FragColor = vec4(col, 1.0);
}
