#version 450

layout(location = 0) in vec3 inDir;
layout(location = 0) out vec4 fragColor;

layout(push_constant) uniform SkyboxPushConstants {
    mat4 viewProj;
    vec4 params;
    vec4 scroll;
    vec4 camera;
} uSky;

layout(set = 2, binding = 0) uniform sampler2D uSunTex;
layout(set = 2, binding = 1) uniform sampler2D uMoonTex;
layout(set = 2, binding = 2) uniform sampler2D uScrollTex;

const float PI = 3.14159265359;

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(
        mix(hash(i + vec2(0.0, 0.0)), hash(i + vec2(1.0, 0.0)), u.x),
        mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x),
        u.y
    );
}

float noisePeriodic(vec2 p, vec2 period) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    vec2 i00 = mod(i + vec2(0.0, 0.0), period);
    vec2 i10 = mod(i + vec2(1.0, 0.0), period);
    vec2 i01 = mod(i + vec2(0.0, 1.0), period);
    vec2 i11 = mod(i + vec2(1.0, 1.0), period);
    return mix(
        mix(hash(i00), hash(i10), u.x),
        mix(hash(i01), hash(i11), u.x),
        u.y
    );
}

float fbm(vec2 p) {
    float value = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 5; ++i) {
        value += noise(p) * amp;
        p = p * 2.03 + vec2(17.2, 9.3);
        amp *= 0.5;
    }
    return value;
}

float fbmPeriodic(vec2 p, vec2 period) {
    float value = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 5; ++i) {
        value += noisePeriodic(p, period) * amp;
        p = p * 2.0 + vec2(17.2, 9.3);
        period *= 2.0;
        amp *= 0.5;
    }
    return value;
}

vec4 sampleBillboard(sampler2D tex, vec3 dir, vec3 centerDir, float angularRadius) {
    vec3 basisUp = abs(centerDir.y) > 0.96 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    vec3 right = normalize(cross(basisUp, centerDir));
    vec3 up = normalize(cross(centerDir, right));

    float forward = dot(dir, centerDir);
    if (forward <= 0.0) {
        return vec4(0.0);
    }

    vec2 plane = vec2(dot(dir, right), dot(dir, up)) / max(forward, 0.001);
    vec2 uv = plane / tan(angularRadius) * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        return vec4(0.0);
    }

    return texture(tex, uv);
}

vec3 proceduralSky(vec3 dir, vec3 sunDir, float dayAmount) {
    float height = clamp(dir.y, -1.0, 1.0);
    float horizonMix = smoothstep(-0.18, 0.72, height);

    vec3 dayHorizon = vec3(0.84, 0.92, 1.02);
    vec3 dayUpper = vec3(0.18, 0.48, 0.88);
    vec3 twilightHorizon = vec3(1.02, 0.62, 0.34);
    vec3 twilightUpper = vec3(0.15, 0.22, 0.45);
    vec3 nightHorizon = vec3(0.03, 0.05, 0.10);
    vec3 nightUpper = vec3(0.005, 0.015, 0.045);

    float twilight = 1.0 - smoothstep(0.08, 0.38, abs(sunDir.y));
    vec3 horizonCol = mix(nightHorizon, twilightHorizon, twilight);
    horizonCol = mix(horizonCol, dayHorizon, dayAmount);
    vec3 upperCol = mix(nightUpper, twilightUpper, twilight);
    upperCol = mix(upperCol, dayUpper, dayAmount);

    vec3 skyColor = mix(horizonCol, upperCol, horizonMix);

    float sunDot = max(dot(dir, sunDir), 0.0);
    float mieGlow = pow(sunDot, 9.0);
    float aureole = pow(sunDot, 40.0);
    vec3 scatterColor = mix(vec3(1.05, 0.62, 0.30), vec3(1.0, 0.97, 0.92), dayAmount);
    skyColor += scatterColor * (mieGlow * 0.22 + aureole * 0.55) * mix(0.35, 1.0, dayAmount);

    float haze = exp(-max(height, -0.1) * 9.0);
    skyColor += mix(vec3(0.06, 0.08, 0.12), vec3(1.0, 0.93, 0.86), dayAmount) * haze * mix(0.05, 0.18, dayAmount);

    float cloudYaw = atan(dir.z, dir.x) / (2.0 * PI) + 0.5;
    vec2 cloudUv = vec2(cloudYaw * 6.0, height * 4.6 + 1.7);
    float cloudShape = fbmPeriodic(cloudUv, vec2(6.0, 1024.0));
    float cloudMask = smoothstep(0.56, 0.76, cloudShape) * smoothstep(-0.22, 0.18, height);
    vec3 cloudLit = mix(vec3(0.09, 0.11, 0.16), vec3(1.0, 0.98, 0.95), dayAmount);
    cloudLit *= 0.55 + mieGlow * 0.45;
    skyColor = mix(skyColor, skyColor + cloudLit * 0.38, cloudMask * mix(0.12, 0.55, dayAmount));

    float nightAmount = 1.0 - dayAmount;
    if (nightAmount > 0.001) {
        vec2 starUv = vec2(atan(dir.z, dir.x) * 24.0, asin(height) * 34.0);
        float starField = pow(max(fbm(starUv * 1.7) - 0.72, 0.0), 7.0) * 18.0;
        float starMask = smoothstep(-0.12, 0.35, height);
        vec3 galaxy = vec3(0.30, 0.38, 0.55) * pow(max(1.0 - abs(dot(dir, normalize(vec3(0.18, 0.88, -0.42)))), 0.0), 5.0);
        skyColor += (vec3(0.92, 0.94, 1.0) * starField + galaxy * 0.55) * nightAmount * starMask;
    }

    return skyColor;
}

vec3 scrollingSky() {
    vec2 viewport = max(uSky.scroll.zw, vec2(1.0));
    vec2 screenUv = gl_FragCoord.xy / viewport;
    vec2 uv = vec2(screenUv.x * uSky.scroll.x + 0.5 * uSky.scroll.x +
                       uSky.camera.x * uSky.camera.z * uSky.scroll.x,
                   screenUv.y * uSky.scroll.y + uSky.camera.y * uSky.params.w * uSky.scroll.y);
    return texture(uScrollTex, uv).rgb;
}

void main() {
    vec3 dir = normalize(inDir);
    float timeOfDay = uSky.params.x;
    float skyMode = uSky.params.y;
    float hasScrollTexture = uSky.params.z;

    float angle = fract(timeOfDay) * 2.0 * PI;
    vec3 sunDir = normalize(vec3(cos(angle), sin(angle) * 0.55, sin(angle)));
    vec3 moonDir = -sunDir;
    float dayAmount = smoothstep(-0.12, 0.16, sunDir.y);
    float nightAmount = 1.0 - dayAmount;

    if (skyMode > 0.5 && hasScrollTexture > 0.5) {
        fragColor = vec4(scrollingSky(), 1.0);
        return;
    }

    vec3 col = proceduralSky(dir, sunDir, dayAmount);

    float sunDot = max(dot(dir, sunDir), 0.0);
    float moonDot = max(dot(dir, moonDir), 0.0);

    vec4 sunSprite = sampleBillboard(uSunTex, dir, sunDir, 0.11);
    vec4 moonSprite = sampleBillboard(uMoonTex, dir, moonDir, 0.075);

    vec3 sunHalo = vec3(1.0, 0.97, 0.9) * (pow(sunDot, 18.0) * 0.45 + pow(sunDot, 220.0) * 4.0);
    vec3 moonHalo = vec3(0.75, 0.82, 1.0) * (pow(moonDot, 20.0) * 0.16);

    col += sunHalo * dayAmount;
    col = mix(col, moonHalo + col, nightAmount * 0.55);
    col = mix(col, col + sunSprite.rgb * (0.35 + sunSprite.a * 1.65), sunSprite.a * dayAmount);
    col = mix(col, col + moonSprite.rgb * (0.25 + moonSprite.a * 1.15), moonSprite.a * nightAmount);

    col = 1.0 - exp(-col * 1.15);
    fragColor = vec4(col, 1.0);
}
