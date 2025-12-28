#version 330 core
out vec4 FragColor;

in vec2 TexCoord;

uniform sampler2D sceneTex;
uniform sampler2D bloomTex;
uniform sampler2D historyTex;

uniform bool enableBloom = false;
uniform float bloomIntensity = 0.8;

uniform bool enableColorAdjust = false;
uniform float exposure = 0.0; // EV stops
uniform float contrast = 1.0;
uniform float saturation = 1.0;
uniform vec3 colorFilter = vec3(1.0);

uniform bool enableMotionBlur = false;
uniform bool hasHistory = false;
uniform float motionBlurStrength = 0.15;

uniform bool enableVignette = false;
uniform float vignetteIntensity = 0.35;
uniform float vignetteSmoothness = 0.25;

uniform bool enableChromatic = false;
uniform float chromaticAmount = 0.0025;

uniform bool enableAO = false;
uniform float aoRadius = 0.0035;
uniform float aoStrength = 0.6;

vec3 applyColorAdjust(vec3 color) {
    if (enableColorAdjust) {
        color *= exp2(exposure);
        color = (color - 0.5) * contrast + 0.5;
        float luma = dot(color, vec3(0.299, 0.587, 0.114));
        color = mix(vec3(luma), color, saturation);
        color *= colorFilter;
    }
    return color;
}

vec3 sampleBase(vec2 uv) {
    return applyColorAdjust(texture(sceneTex, uv).rgb);
}

float luminance(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

float computeVignette(vec2 uv) {
    float dist = length(uv - vec2(0.5));
    float vig = smoothstep(0.8 - vignetteIntensity, 1.0 + vignetteSmoothness, dist);
    return clamp(1.0 - vig * vignetteIntensity, 0.0, 1.0);
}

vec3 applyChromatic(vec2 uv) {
    vec3 base = sampleBase(uv);
    vec2 dir = uv - vec2(0.5);
    float dist = max(length(dir), 0.0001);
    vec2 offset = normalize(dir) * chromaticAmount * (1.0 + dist * 2.0);
    vec3 rSample = sampleBase(uv + offset);
    vec3 bSample = sampleBase(uv - offset);
    vec3 ca = vec3(rSample.r, base.g, bSample.b);
    return mix(base, ca, 0.85);
}

float computeAOFactor(vec2 uv) {
    vec3 centerColor = sampleBase(uv);
    float centerLum = luminance(centerColor);
    float occlusion = 0.0;
    vec2 directions[4] = vec2[](vec2(1.0, 0.0), vec2(-1.0, 0.0), vec2(0.0, 1.0), vec2(0.0, -1.0));
    for (int i = 0; i < 4; ++i) {
        vec2 sampleUv = uv + directions[i] * aoRadius;
        vec3 sampleColor = sampleBase(sampleUv);
        float sampleLum = luminance(sampleColor);
        occlusion += max(0.0, centerLum - sampleLum);
    }
    occlusion /= 4.0;
    return clamp(1.0 - occlusion * aoStrength, 0.0, 1.0);
}

void main() {
    vec3 color = sampleBase(TexCoord);

    if (enableChromatic) {
        color = applyChromatic(TexCoord);
    }

    if (enableAO) {
        color *= computeAOFactor(TexCoord);
    }

    if (enableVignette) {
        color *= computeVignette(TexCoord);
    }

    if (enableMotionBlur && hasHistory) {
        vec2 dir = TexCoord - vec2(0.5);
        float len = length(dir);
        dir = (len > 0.0001) ? dir / len : vec2(0.0);
        float smear = clamp(motionBlurStrength, 0.0, 0.98) * 0.035; // subtle default

        vec3 accum = vec3(0.0);
        float weightSum = 0.0;
        for (int i = 0; i < 3; ++i) {
            float t = (float(i) + 1.0) / 3.0;
            float w = 1.0 - t * 0.4;
            vec2 offsetUv = TexCoord - dir * smear * t;
            offsetUv = clamp(offsetUv, vec2(0.002), vec2(0.998));
            vec3 sampleCol = texture(historyTex, offsetUv).rgb;
            accum += sampleCol * w;
            weightSum += w;
        }
        vec3 history = (weightSum > 0.0) ? accum / weightSum : texture(historyTex, TexCoord).rgb;
        float diff = length(color - history);
        float motionWeight = smoothstep(0.01, 0.08, diff); // suppress blur when camera still
        float mixAmt = clamp(motionBlurStrength * 0.85, 0.0, 0.9) * motionWeight;
        if (mixAmt > 0.0001) {
            color = mix(color, history, mixAmt);
        }
    }

    if (enableBloom) {
        vec3 glow = texture(bloomTex, TexCoord).rgb * bloomIntensity;
        color += glow;
    }

    FragColor = vec4(color, 1.0);
}
