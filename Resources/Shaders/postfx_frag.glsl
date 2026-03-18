#version 330 core
out vec4 FragColor;

in vec2 TexCoord;

uniform sampler2D sceneTex;
uniform sampler2D bloomTex;
uniform sampler2D historyTex;

uniform bool enableHDR = true;
uniform int toneMapper = 2;
uniform float whitePoint = 4.0;
uniform float gamma = 2.2;

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
uniform float motionBlurThreshold = 0.04;
uniform float motionBlurClamp = 0.35;

uniform bool enableVignette = false;
uniform float vignetteIntensity = 0.35;
uniform float vignetteSmoothness = 0.25;

uniform bool enableChromatic = false;
uniform float chromaticAmount = 0.0025;

uniform bool enableSharpen = false;
uniform float sharpenStrength = 0.15;

uniform bool enableAO = false;
uniform float aoRadius = 0.0035;
uniform float aoStrength = 0.6;
uniform vec2 texelSize = vec2(1.0 / 1280.0, 1.0 / 720.0);

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

vec3 applySharpening(vec2 uv, vec3 color) {
    if (!enableSharpen) {
        return color;
    }

    vec3 north = sampleBase(uv + vec2(0.0, texelSize.y));
    vec3 south = sampleBase(uv - vec2(0.0, texelSize.y));
    vec3 east = sampleBase(uv + vec2(texelSize.x, 0.0));
    vec3 west = sampleBase(uv - vec2(texelSize.x, 0.0));
    vec3 blurred = (north + south + east + west) * 0.25;
    vec3 sharpened = color + (color - blurred) * sharpenStrength;
    return max(sharpened, vec3(0.0));
}

vec3 toneMap(vec3 color) {
    vec3 mapped = max(color, vec3(0.0));
    if (enableHDR) {
        float wp = max(whitePoint, 0.001);
        vec3 scaled = mapped / wp;
        if (toneMapper == 1) {
            mapped = scaled / (vec3(1.0) + scaled);
        } else if (toneMapper == 2) {
            vec3 x = scaled;
            mapped = clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
        } else {
            mapped = clamp(scaled, 0.0, 1.0);
        }
    } else {
        mapped = clamp(mapped, 0.0, 1.0);
    }

    float safeGamma = max(gamma, 0.001);
    return pow(clamp(mapped, 0.0, 1.0), vec3(1.0 / safeGamma));
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
        vec3 history = texture(historyTex, TexCoord).rgb;
        vec3 delta = clamp(history - color, vec3(-motionBlurClamp), vec3(motionBlurClamp));
        float diff = max(max(abs(delta.r), abs(delta.g)), abs(delta.b));
        float response = smoothstep(motionBlurThreshold,
                                    max(motionBlurThreshold * 4.0, motionBlurThreshold + 0.0001),
                                    diff);
        float mixAmt = clamp(motionBlurStrength * response, 0.0, 0.92);
        color += delta * mixAmt;
    }

    if (enableBloom) {
        vec3 glow = texture(bloomTex, TexCoord).rgb * bloomIntensity;
        color += glow;
    }

    color = applySharpening(TexCoord, color);
    FragColor = vec4(toneMap(color), 1.0);
}
