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

uniform bool enableDither = false;
uniform float ditherIntensity = 0.65;
uniform int ditherColorBits = 5;
uniform float ditherDarkAdjustment = 0.35;
uniform float ditherPixelation = 0.0;
uniform float ditherSize = 1.0;
uniform float ditherContrast = 0.35;
uniform float ditherOffset = 0.0;
uniform int ditherPalette = 0;
uniform int ditherPattern = 4;
uniform bool enableStatic = false;
uniform float staticIntensity = 0.0;
uniform float staticGrainScale = 1.0;
uniform float staticDarkAreaInfluence = 0.0;
uniform float staticSpeed = 0.0;
uniform bool enableStaticDistortion = false;
uniform float staticDistortionHorizontalJitterAmount = 0.0;
uniform float staticDistortionLineDensity = 128.0;
uniform float staticDistortionGlitchFrequency = 0.0;
uniform float staticDistortionStrength = 0.0;
uniform bool enableLensDistortion = false;
uniform float lensDistortionAmount = 0.0;
uniform float lensDistortionEdgeFalloff = 0.75;
uniform vec2 lensDistortionCenterOffset = vec2(0.0);
uniform bool enableVHSOverlay = false;
uniform float vhsOverlayOpacity = 0.0;
uniform float vhsOverlayScanlineStrength = 0.0;
uniform float vhsOverlayTapeNoise = 0.0;
uniform float vhsOverlayChromaBleed = 0.0;
uniform float vhsOverlayBottomNoiseBandHeight = 0.0;
uniform float vhsOverlayBottomNoiseBandIntensity = 0.0;
uniform bool enableWavyEffect = false;
uniform float wavyAmplitude = 0.0;
uniform float wavyFrequency = 16.0;
uniform float wavySpeed = 0.0;
uniform bool wavyVertical = false;

uniform vec2 texelSize = vec2(1.0 / 1280.0, 1.0 / 720.0);
uniform float u_time = 0.0;

float interleavedNoise(vec2 p);

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

vec2 applyLensDistortion(vec2 uv) {
    if (!enableLensDistortion || abs(lensDistortionAmount) <= 0.000001) {
        return uv;
    }

    vec2 center = vec2(0.5) + lensDistortionCenterOffset;
    vec2 delta = uv - center;
    float dist = length(delta);
    float falloff = smoothstep(clamp(lensDistortionEdgeFalloff, 0.0, 1.0), 1.0, dist * 1.41421356);
    float warp = lensDistortionAmount * falloff * falloff;
    return center + delta * (1.0 + warp);
}

vec2 applyWavyEffect(vec2 uv) {
    if (!enableWavyEffect || abs(wavyAmplitude) <= 0.000001) {
        return uv;
    }

    float phase = u_time * wavySpeed;
    float wave = sin((wavyVertical ? uv.x : uv.y) * wavyFrequency + phase);
    if (wavyVertical) {
        uv.y += wave * wavyAmplitude;
    } else {
        uv.x += wave * wavyAmplitude;
    }
    return uv;
}

vec2 applyStaticDistortion(vec2 uv) {
    if (!enableStaticDistortion || staticDistortionStrength <= 0.000001) {
        return uv;
    }

    float lineDensity = max(staticDistortionLineDensity, 1.0);
    float lineId = floor(uv.y * lineDensity);
    float timePhase = floor(u_time * staticDistortionGlitchFrequency);
    float lineNoise = interleavedNoise(vec2(lineId * 0.731, timePhase * 13.37 + lineId));
    float glitchMask = step(0.58, interleavedNoise(vec2(lineId + timePhase, lineId * 1.37 + 29.0)));
    float jitter = (lineNoise - 0.5) * staticDistortionHorizontalJitterAmount * staticDistortionStrength;
    uv.x += jitter * mix(0.35, 1.0, glitchMask);
    return uv;
}

vec3 applyStaticEffect(vec3 color) {
    if (!enableStatic || staticIntensity <= 0.000001) {
        return color;
    }

    float scale = max(staticGrainScale, 0.01);
    vec2 noiseUv = gl_FragCoord.xy / scale;
    float timeSeed = u_time * staticSpeed;
    float noiseR = interleavedNoise(noiseUv + vec2(timeSeed, timeSeed * 0.37));
    float noiseG = interleavedNoise(noiseUv + vec2(17.0, 53.0) + vec2(timeSeed * 1.11, timeSeed * 0.19));
    float noiseB = interleavedNoise(noiseUv + vec2(29.0, 11.0) - vec2(timeSeed * 0.93, timeSeed * 0.23));
    float luma = luminance(color);
    float darkFactor = mix(1.0, 1.0 + staticDarkAreaInfluence, 1.0 - luma);
    vec3 noise = vec3(noiseR, noiseG, noiseB) - 0.5;
    return clamp(color + noise * staticIntensity * darkFactor, 0.0, 1.0);
}

vec3 applyVhsChromaBleed(vec2 uv, vec3 color) {
    if (!enableVHSOverlay || vhsOverlayChromaBleed <= 0.000001) {
        return color;
    }

    float bleedStrength = clamp(vhsOverlayChromaBleed * 40.0, 0.0, 1.0);
    vec2 offset = vec2(vhsOverlayChromaBleed * 10.0, 0.0) * texelSize;
    vec3 left = sampleBase(uv - offset);
    vec3 right = sampleBase(uv + offset);
    vec3 bleed = vec3(left.r, color.g, right.b);
    return mix(color, bleed, bleedStrength);
}

vec3 applyVhsOverlay(vec3 color, vec2 uv) {
    if (!enableVHSOverlay || vhsOverlayOpacity <= 0.000001) {
        return color;
    }

    float scanlinePhase = gl_FragCoord.y * 0.5 + u_time * 24.0;
    float scanline = 0.88 + 0.12 * sin(scanlinePhase);
    float scanlineMask = mix(1.0, scanline, clamp(vhsOverlayScanlineStrength, 0.0, 1.0));

    float tapeSeed = u_time * 53.0;
    float tapeNoise = interleavedNoise(gl_FragCoord.xy * 0.85 + vec2(tapeSeed, tapeSeed * 0.37)) - 0.5;
    vec3 noiseColor = vec3(
        tapeNoise,
        interleavedNoise(gl_FragCoord.xy * 0.91 + vec2(13.0, 47.0) + vec2(tapeSeed * 0.71, tapeSeed * 0.29)) - 0.5,
        interleavedNoise(gl_FragCoord.xy * 1.07 + vec2(29.0, 11.0) - vec2(tapeSeed * 0.43, tapeSeed * 0.17)) - 0.5);
    noiseColor *= clamp(vhsOverlayTapeNoise, 0.0, 1.0) * 0.35;

    float bandHeight = clamp(vhsOverlayBottomNoiseBandHeight, 0.0, 1.0);
    float bandMask = 1.0 - smoothstep(0.0, max(0.0001, bandHeight), uv.y);
    float bandNoise = interleavedNoise(vec2(gl_FragCoord.x * 0.72, gl_FragCoord.y * 8.0 + u_time * 120.0));
    vec3 bandColor = vec3(bandNoise) * clamp(vhsOverlayBottomNoiseBandIntensity, 0.0, 2.0) * bandMask;
    bandColor += vec3(0.25, 0.25, 0.25) * bandMask * clamp(vhsOverlayBottomNoiseBandIntensity, 0.0, 2.0) * 0.25;

    vec3 overlay = color;
    overlay *= scanlineMask;
    overlay += noiseColor;
    overlay += bandColor;
    overlay = clamp(overlay, 0.0, 1.0);
    return mix(color, overlay, clamp(vhsOverlayOpacity, 0.0, 1.0));
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

vec2 resolvePixelatedUv(vec2 uv) {
    if (ditherPixelation <= 1.0) {
        return uv;
    }
    vec2 stepSize = max(texelSize * ditherPixelation, texelSize);
    return clamp((floor(uv / stepSize) + vec2(0.5)) * stepSize, vec2(0.0), vec2(1.0));
}

float orderedDither4x4(vec2 fragCoord) {
    ivec2 cell = ivec2(mod(floor(fragCoord), 4.0));
    int index = cell.x + cell.y * 4;
    if (index == 0) return 0.0 / 16.0;
    if (index == 1) return 8.0 / 16.0;
    if (index == 2) return 2.0 / 16.0;
    if (index == 3) return 10.0 / 16.0;
    if (index == 4) return 12.0 / 16.0;
    if (index == 5) return 4.0 / 16.0;
    if (index == 6) return 14.0 / 16.0;
    if (index == 7) return 6.0 / 16.0;
    if (index == 8) return 3.0 / 16.0;
    if (index == 9) return 11.0 / 16.0;
    if (index == 10) return 1.0 / 16.0;
    if (index == 11) return 9.0 / 16.0;
    if (index == 12) return 15.0 / 16.0;
    if (index == 13) return 7.0 / 16.0;
    if (index == 14) return 13.0 / 16.0;
    return 5.0 / 16.0;
}

float orderedDither8x8(vec2 fragCoord) {
    const float bayer[64] = float[](
         0.0, 48.0, 12.0, 60.0,  3.0, 51.0, 15.0, 63.0,
        32.0, 16.0, 44.0, 28.0, 35.0, 19.0, 47.0, 31.0,
         8.0, 56.0,  4.0, 52.0, 11.0, 59.0,  7.0, 55.0,
        40.0, 24.0, 36.0, 20.0, 43.0, 27.0, 39.0, 23.0,
         2.0, 50.0, 14.0, 62.0,  1.0, 49.0, 13.0, 61.0,
        34.0, 18.0, 46.0, 30.0, 33.0, 17.0, 45.0, 29.0,
        10.0, 58.0,  6.0, 54.0,  9.0, 57.0,  5.0, 53.0,
        42.0, 26.0, 38.0, 22.0, 41.0, 25.0, 37.0, 21.0
    );
    ivec2 cell = ivec2(mod(floor(fragCoord), 8.0));
    return bayer[cell.x + cell.y * 8] / 64.0;
}

float orderedDither16x16(vec2 fragCoord) {
    vec2 coarse = floor(fragCoord * 0.5);
    float a = orderedDither8x8(coarse);
    float b = orderedDither8x8(coarse + vec2(11.0, 3.0));
    return clamp(mix(a, b, 0.5), 0.0, 1.0);
}

float checkerDither(vec2 fragCoord) {
    vec2 cell = mod(floor(fragCoord), 2.0);
    return (cell.x == cell.y) ? 0.2 : 0.8;
}

float interleavedNoise(vec2 p) {
    return fract(52.9829189 * fract(0.06711056 * p.x + 0.00583715 * p.y));
}

float sampleDitherPattern(vec2 fragCoord) {
    float size = max(ditherSize, 1.0);
    vec2 scaled = fragCoord / size;
    if (ditherPattern == 0) {
        return orderedDither4x4(scaled);
    }
    if (ditherPattern == 1) {
        return orderedDither8x8(scaled);
    }
    if (ditherPattern == 2) {
        return orderedDither16x16(scaled);
    }
    if (ditherPattern == 3) {
        return checkerDither(scaled);
    }

    float bayer = orderedDither8x8(scaled);
    float coarse = orderedDither4x4(scaled * 0.5 + vec2(1.0, 2.0));
    float noise = interleavedNoise(floor(scaled) + vec2(17.0, 29.0));
    return clamp(mix(mix(bayer, coarse, 0.35), noise, 0.12), 0.0, 1.0);
}

float shapeDither(float threshold) {
    float centered = threshold - 0.5 + ditherOffset * 0.5;
    float gain = max(0.05, 1.0 + ditherContrast * 2.0);
    centered = sign(centered) * pow(abs(centered) * 2.0, gain) * 0.5;
    return clamp(centered + 0.5, 0.0, 1.0);
}

vec3 applyPalette(vec3 color) {
    if (ditherPalette == 0) {
        return color;
    }

    const vec3 warmA = vec3(0.090, 0.086, 0.145);
    const vec3 warmB = vec3(0.337, 0.302, 0.455);
    const vec3 warmC = vec3(0.729, 0.678, 0.745);
    const vec3 warmD = vec3(0.956, 0.934, 0.902);

    const vec3 coolA = vec3(0.074, 0.094, 0.176);
    const vec3 coolB = vec3(0.286, 0.322, 0.525);
    const vec3 coolC = vec3(0.690, 0.714, 0.835);
    const vec3 coolD = vec3(0.953, 0.960, 0.976);

    const vec3 monoA = vec3(0.08);
    const vec3 monoB = vec3(0.34);
    const vec3 monoC = vec3(0.67);
    const vec3 monoD = vec3(0.94);

    const vec3 sepiaA = vec3(0.110, 0.082, 0.055);
    const vec3 sepiaB = vec3(0.372, 0.258, 0.145);
    const vec3 sepiaC = vec3(0.702, 0.584, 0.384);
    const vec3 sepiaD = vec3(0.949, 0.902, 0.769);

    float luma = dot(color, vec3(0.299, 0.587, 0.114));
    vec3 c0 = warmA;
    vec3 c1 = warmB;
    vec3 c2 = warmC;
    vec3 c3 = warmD;
    if (ditherPalette == 2) {
        c0 = coolA; c1 = coolB; c2 = coolC; c3 = coolD;
    } else if (ditherPalette == 3) {
        c0 = monoA; c1 = monoB; c2 = monoC; c3 = monoD;
    } else if (ditherPalette == 4) {
        c0 = sepiaA; c1 = sepiaB; c2 = sepiaC; c3 = sepiaD;
    }

    if (luma < 0.333) {
        return mix(c0, c1, smoothstep(0.0, 0.333, luma));
    }
    if (luma < 0.666) {
        return mix(c1, c2, smoothstep(0.333, 0.666, luma));
    }
    return mix(c2, c3, smoothstep(0.666, 1.0, luma));
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
    vec2 sampleUv = resolvePixelatedUv(TexCoord);
    sampleUv = applyLensDistortion(sampleUv);
    sampleUv = applyWavyEffect(sampleUv);
    sampleUv = applyStaticDistortion(sampleUv);
    vec3 color = sampleBase(sampleUv);

    if (enableChromatic) {
        color = applyChromatic(sampleUv);
    }

    if (enableAO) {
        color *= computeAOFactor(sampleUv);
    }

    if (enableVignette) {
        color *= computeVignette(sampleUv);
    }

    if (enableMotionBlur && hasHistory) {
        vec3 history = texture(historyTex, sampleUv).rgb;
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

    color = applySharpening(sampleUv, color);
    color = applyVhsChromaBleed(sampleUv, color);
    vec3 outputColor = toneMap(color);
    outputColor = applyStaticEffect(outputColor);
    outputColor = applyVhsOverlay(outputColor, TexCoord);

    if (enableDither) {
        float levels = max(1.0, exp2(float(clamp(ditherColorBits, 1, 8))) - 1.0);
        float ditherBase = shapeDither(sampleDitherPattern(gl_FragCoord.xy));
        vec3 ditherNoise = vec3(
            ditherBase,
            shapeDither(sampleDitherPattern(gl_FragCoord.xy + vec2(1.0, 2.0))),
            shapeDither(sampleDitherPattern(gl_FragCoord.xy + vec2(2.0, 1.0)))) - 0.5;
        float luma = dot(outputColor, vec3(0.299, 0.587, 0.114));
        float darkBias = clamp((1.0 - luma) * max(ditherDarkAdjustment, 0.0), 0.0, 1.0);
        float strength = max(ditherIntensity, 0.0) * (0.65 + darkBias * 0.85);
        vec3 quantized = floor(clamp(outputColor, 0.0, 1.0) * levels + 0.5 + ditherNoise * strength) / levels;
        outputColor = clamp(applyPalette(clamp(quantized, 0.0, 1.0)), 0.0, 1.0);
    }

    FragColor = vec4(outputColor, 1.0);
}
