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
uniform float vhsOverlayDistortionStrength = 0.0;
uniform float vhsOverlayAnimationSpeed = 0.0;
uniform float vhsOverlayColorBleed = 0.0;
uniform float vhsOverlayBanding = 0.0;
uniform int vhsOverlaySignalMode = 3; // 0 Composite, 1 S-Video, 2 RF, 3 VHS SP, 4 VHS LP, 5 VHS EP
uniform float vhsOverlayDropouts = 0.0;
uniform bool enableWavyEffect = false;
uniform float wavyAmplitude = 0.0;
uniform float wavyFrequency = 16.0;
uniform float wavySpeed = 0.0;
uniform bool wavyVertical = false;

uniform vec2 texelSize = vec2(1.0 / 1280.0, 1.0 / 720.0);
uniform float u_time = 0.0;

float interleavedNoise(vec2 p);
vec3 toneMap(vec3 color);

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

vec3 rgbToYiq(vec3 color) {
    return vec3(
        dot(color, vec3(0.299, 0.587, 0.114)),
        dot(color, vec3(0.596, -0.274, -0.322)),
        dot(color, vec3(0.211, -0.523, 0.312))
    );
}

vec3 yiqToRgb(vec3 yiq) {
    return vec3(
        yiq.x + 0.956 * yiq.y + 0.621 * yiq.z,
        yiq.x - 0.272 * yiq.y - 0.647 * yiq.z,
        yiq.x - 1.106 * yiq.y + 1.703 * yiq.z
    );
}

vec3 sampleDisplayBase(vec2 uv) {
    return toneMap(sampleBase(clamp(uv, vec2(0.0), vec2(1.0))));
}

// ---------------------------------------------------------------------------
// NTSC / VHS emulation, modeled on the ntsc-rs signal chain:
// edge wave -> head switching -> tracking noise -> band-limited luma with
// one-sided tape smear + sharpen overshoot -> narrow delayed chroma ->
// cross-color / chroma phase noise / chroma loss -> snow speckles.
// Modes: 0 NTSC Composite, 1 NTSC S-Video, 2 NTSC RF, 3 VHS SP, 4 VHS LP, 5 VHS EP.
// ---------------------------------------------------------------------------

float vhsHash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float vhsNoise2D(vec2 p, float seed) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = vhsHash(i + vec2(seed, seed));
    float b = vhsHash(i + vec2(1.0 + seed, seed));
    float c = vhsHash(i + vec2(seed, 1.0 + seed));
    float d = vhsHash(i + vec2(1.0 + seed, 1.0 + seed));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float vhsFbm2(vec2 p, float seed) {
    return vhsNoise2D(p, seed) * 0.625 +
           vhsNoise2D(p * 2.0, seed + 9.0) * 0.25 +
           vhsNoise2D(p * 4.0, seed + 17.0) * 0.125;
}

// Sparse comet-shaped transients ("snow"): a damped cosine burst that decays
// to the right, the way a tape dropout reads back.
float vhsSpeckles(float xPx, float line, float frame, float rate, float widthPx, float pxScale) {
    float acc = 0.0;
    for (int k = 0; k < 2; ++k) {
        float fk = float(k);
        float gate = vhsHash(vec2(line * 1.117 + fk * 37.0, frame * 0.731 + fk));
        if (gate < rate) {
            float x0 = vhsHash(vec2(line * 0.913 + fk * 11.0, frame + 3.0 + fk)) * widthPx;
            float len = mix(8.0, 64.0, vhsHash(vec2(frame * 1.3 + fk, line * 0.7))) * pxScale;
            float dxp = xPx - x0;
            if (dxp >= 0.0 && dxp < len) {
                float amp = vhsHash(vec2(x0 * 0.37 + frame, line + fk)) * 3.0 - 1.0;
                acc += cos(dxp * 3.14159265 / max(len * 0.25, 1.0)) *
                       pow(1.0 - dxp / len, 2.0) * amp;
            }
        }
    }
    return acc;
}

// Tape-speed/source parameters. Luma/chroma smear lengths are in native NTSC
// pixels (~754 samples per active line) and get rescaled to the viewport.
void vhsModeParams(int mode, out float lumaSmearPx, out float chromaSmearPx,
                   out float chromaDelayPx, out float sharpenAmt, out float baseNoise,
                   out float chromaLossBase, out float crossColorAmt, out float snowBase,
                   out float isTape) {
    if (mode == 0) {        // NTSC Composite
        lumaSmearPx = 0.20; chromaSmearPx = 2.0; chromaDelayPx = 0.0; sharpenAmt = 0.0;
        baseNoise = 0.008; chromaLossBase = 0.0; crossColorAmt = 1.0; snowBase = 0.0; isTape = 0.0;
    } else if (mode == 1) { // NTSC S-Video
        lumaSmearPx = 0.15; chromaSmearPx = 1.4; chromaDelayPx = 0.0; sharpenAmt = 0.0;
        baseNoise = 0.005; chromaLossBase = 0.0; crossColorAmt = 0.15; snowBase = 0.0; isTape = 0.0;
    } else if (mode == 2) { // NTSC RF (antenna)
        lumaSmearPx = 0.40; chromaSmearPx = 2.4; chromaDelayPx = 0.5; sharpenAmt = 0.0;
        baseNoise = 0.040; chromaLossBase = 0.0; crossColorAmt = 1.3; snowBase = 0.08; isTape = 0.0;
    } else if (mode == 4) { // VHS LP (1.9 MHz luma / 300 kHz chroma)
        lumaSmearPx = 1.20; chromaSmearPx = 7.6; chromaDelayPx = 5.0; sharpenAmt = 0.55;
        baseNoise = 0.035; chromaLossBase = 0.015; crossColorAmt = 0.60; snowBase = 0.06; isTape = 1.0;
    } else if (mode == 5) { // VHS EP (1.4 MHz luma / 280 kHz chroma)
        lumaSmearPx = 1.65; chromaSmearPx = 8.1; chromaDelayPx = 6.0; sharpenAmt = 0.60;
        baseNoise = 0.055; chromaLossBase = 0.030; crossColorAmt = 0.70; snowBase = 0.12; isTape = 1.0;
    } else {                // VHS SP (2.4 MHz luma / 320 kHz chroma)
        lumaSmearPx = 0.95; chromaSmearPx = 7.1; chromaDelayPx = 4.0; sharpenAmt = 0.50;
        baseNoise = 0.020; chromaLossBase = 0.005; crossColorAmt = 0.50; snowBase = 0.03; isTape = 1.0;
    }
}

vec3 applyVhsOverlay(vec3 color, vec2 uv) {
    if (!enableVHSOverlay || vhsOverlayOpacity <= 0.000001) {
        return color;
    }

    int mode = clamp(vhsOverlaySignalMode, 0, 5);
    float lumaSmearPx, chromaSmearPx, chromaDelayPx, sharpenAmt, baseNoise;
    float chromaLossBase, crossColorAmt, snowBase, isTape;
    vhsModeParams(mode, lumaSmearPx, chromaSmearPx, chromaDelayPx, sharpenAmt, baseNoise,
                  chromaLossBase, crossColorAmt, snowBase, isTape);

    float speed = max(vhsOverlayAnimationSpeed, 0.0);
    // Wrap time so hash inputs stay small; float precision otherwise degrades
    // the noise after long sessions.
    float t = mod(u_time * speed, 3600.0);
    float frame = floor(mod(t * 29.97, 1024.0));

    float widthPx = 1.0 / max(texelSize.x, 1e-6);
    float heightPx = 1.0 / max(texelSize.y, 1e-6);
    float pxScale = max(widthPx / 754.0, 0.1);  // horizontal scale vs. native NTSC sampling
    float vScale = max(heightPx / 480.0, 0.1);

    float distortion = clamp(vhsOverlayDistortionStrength, 0.0, 2.0);
    float trackAmount = clamp(vhsOverlayBottomNoiseBandHeight, 0.0, 1.0);
    float headAmount = clamp(vhsOverlayBottomNoiseBandIntensity, 0.0, 2.0);
    float chromaBleed = clamp(vhsOverlayChromaBleed, 0.0, 1.0);
    float colorBleed = clamp(vhsOverlayColorBleed, 0.0, 1.0);
    float noiseAmount = clamp(vhsOverlayTapeNoise, 0.0, 1.0);
    float dropouts = clamp(vhsOverlayDropouts, 0.0, 1.0);

    vec2 wUv = uv;
    float line = floor(uv.y * heightPx);
    float rowFromBottom = uv.y * heightPx;  // uv.y = 0 is the bottom of the picture

    // --- Edge wave: scanlines surf on smooth low-frequency noise ---
    float waveAmp = distortion * 10.0 * pxScale * mix(0.15, 1.0, isTape);
    float wave = (vhsFbm2(vec2(line * 0.05 / vScale, t * 2.0), 7.0) - 0.5) * 2.0 * waveAmp;
    wUv.x += wave * texelSize.x;

    // Per-line time-base error: a sub-pixel twitch on every scanline.
    wUv.x += (vhsHash(vec2(line, frame)) - 0.5) * (0.3 + isTape * 0.7) * distortion * pxScale * texelSize.x;

    // --- Head switching: the bottom rows tear rightward, tape only ---
    float headTransient = 0.0;
    float headMask = 0.0;
    float headRows = 8.0 * vScale;
    if (isTape > 0.5 && headAmount > 0.0001 && rowFromBottom < headRows) {
        headMask = 1.0;
        float prog = (headRows - rowFromBottom + 3.0 * vScale) / max(headRows, 1.0);
        float shiftPx = 72.0 * pxScale * headAmount * 0.5 * pow(clamp(prog, 0.0, 2.0), 1.5) +
                        (vhsHash(vec2(floor(rowFromBottom), frame)) - 0.5) * pxScale;
        if (floor(rowFromBottom) >= floor(headRows) - 1.0) {
            // Mid-line switch: only part of the top affected row tears, with a
            // bright transient right at the switch point.
            float pos = 0.6 + (vhsHash(vec2(frame, 17.0)) + vhsHash(vec2(frame, 91.0)) - 1.0) * 0.15;
            if (uv.x > pos) {
                wUv.x -= shiftPx * texelSize.x;
            }
            float dxp = (uv.x - pos) * widthPx;
            float transientLen = 16.0 * pxScale;
            if (dxp >= 0.0 && dxp < transientLen) {
                headTransient = pow(1.0 - dxp / transientLen, 3.0) *
                                (vhsHash(vec2(frame, 5.0)) * 0.5 + 0.25) * headAmount;
            }
        } else {
            wUv.x -= shiftPx * texelSize.x;
        }
    }

    // --- Tracking noise: a band above the head switch that ramps toward the bottom ---
    float trackScale = 0.0;
    float trackRows = trackAmount * 0.35 * heightPx;
    if (isTape > 0.5 && trackAmount > 0.0001 && rowFromBottom < trackRows) {
        trackScale = 1.0 - rowFromBottom / max(trackRows, 1.0);
        float waveShift = (vhsFbm2(vec2(line * 0.5, t * 3.0), 23.0) - 0.5) *
                          trackScale * 30.0 * pxScale;
        wUv.x += waveShift * texelSize.x;
    }

    wUv = clamp(wUv, vec2(0.0), vec2(1.0));

    // --- Luma: nearly sharp, with a one-sided tape smear and sharpen overshoot ---
    // The asymmetry matters: a real deck's lowpass is causal, so detail smears
    // to the right instead of blurring evenly.
    vec2 dx = vec2(texelSize.x, 0.0);
    float smearL = max(lumaSmearPx * pxScale, 0.01);
    vec3 rgb0 = sampleDisplayBase(wUv);
    float y0 = rgbToYiq(rgb0).x;
    float y1 = rgbToYiq(sampleDisplayBase(wUv - dx * smearL)).x;
    float y2 = rgbToYiq(sampleDisplayBase(wUv - dx * smearL * 2.0)).x;
    float y3 = rgbToYiq(sampleDisplayBase(wUv - dx * smearL * 3.0)).x;
    float yLow = y0 * 0.46 + y1 * 0.27 + y2 * 0.16 + y3 * 0.11;
    float yWide = (y0 + y1 + y2 + y3) * 0.25;
    float yOut = yLow + (yLow - yWide) * sharpenAmt * 2.0;

    // RF ghosting: faint displaced echo of the picture.
    if (mode == 2) {
        float yGhost = rgbToYiq(sampleDisplayBase(wUv + dx * 30.0 * pxScale)).x;
        yOut += (yGhost - yOut) * 0.08;
    }

    // --- Chroma: very narrow bandwidth, delayed right, vertically blended ---
    float smearC = max(chromaSmearPx * pxScale * mix(0.6, 1.6, chromaBleed), 0.01);
    float delayC = (chromaDelayPx + colorBleed * 4.0) * pxScale;
    // Sampling half a scanline up lets bilinear filtering average each chroma
    // line with its neighbor for free (VHS chroma vertical blend).
    vec2 cUv = vec2(wUv.x - delayC * texelSize.x, wUv.y + isTape * 0.5 * texelSize.y);
    vec2 iq = rgbToYiq(sampleDisplayBase(cUv)).yz * 0.38 +
              rgbToYiq(sampleDisplayBase(cUv - dx * smearC * 0.75)).yz * 0.28 +
              rgbToYiq(sampleDisplayBase(cUv - dx * smearC * 1.75)).yz * 0.20 +
              rgbToYiq(sampleDisplayBase(cUv - dx * smearC * 3.0)).yz * 0.14;

    // Cross-color / dot crawl: luma edges leak in along the subcarrier phase.
    float subcarrierPhase = line * 3.14159265 + uv.x * widthPx * 1.5708 + mod(frame, 4.0) * 1.5708;
    iq += (y0 - y1) * 0.5 * vec2(cos(subcarrierPhase), sin(subcarrierPhase)) *
          crossColorAmt * (0.08 + colorBleed * 0.12);

    // Chroma phase noise: per-line hue wobble.
    float hueJit = (vhsHash(vec2(line * 0.531, frame + 7.0)) - 0.5) * 6.28318 *
                   (0.04 + colorBleed * 0.20) * (0.3 + isTape * 0.7);
    float cosH = cos(hueJit);
    float sinH = sin(hueJit);
    iq = vec2(iq.x * cosH - iq.y * sinH, iq.x * sinH + iq.y * cosH);

    // Chroma loss: whole scanlines randomly drop their color.
    float lossRate = chromaLossBase + colorBleed * 0.05 * isTape + trackScale * 0.25;
    if (vhsHash(vec2(line * 1.731, frame * 1.13 + 41.0)) < lossRate) {
        iq = vec2(0.0);
    }

    // --- Noise floor: smooth per-line grain plus fine speckle ---
    float grainAmt = baseNoise + noiseAmount * 0.15 + trackScale * trackScale * trackAmount * 0.4;
    float grain = vhsFbm2(vec2(uv.x * widthPx * 0.25 / pxScale, line * 0.77 + frame * 31.7), 3.0) - 0.5;
    float grainFine = vhsHash(gl_FragCoord.xy + vec2(frame * 13.7, frame * 7.3)) - 0.5;
    yOut += grain * grainAmt * 1.6 + grainFine * grainAmt * 0.5;

    // Soft colored noise in the chroma planes.
    iq += vec2(vhsNoise2D(vec2(uv.x * widthPx * 0.05, line * 0.5 + frame * 11.0), 5.0) - 0.5,
               vhsNoise2D(vec2(uv.x * widthPx * 0.05, line * 0.5 + frame * 17.0), 13.0) - 0.5) *
          (0.02 + noiseAmount * 0.12 + colorBleed * 0.05);

    // --- Snow: sparse comet-tailed dropout transients ---
    float speckleRate = snowBase * 0.5 + dropouts * 0.25 + trackScale * trackScale * trackAmount * 0.6;
    if (speckleRate > 0.0001) {
        yOut += vhsSpeckles(uv.x * widthPx, line, frame, speckleRate, widthPx, pxScale) *
                (0.5 + dropouts * 0.5);
    }

    // Head switching transient + noise inside the switch region.
    yOut += headTransient;
    if (headMask > 0.5) {
        yOut += (vhsHash(vec2(gl_FragCoord.x, gl_FragCoord.y + frame * 7.0)) - 0.5) * 0.2 * headAmount;
    }

    // Tape playback lifts black level slightly and compresses whites.
    float blackLift = 0.012 * isTape;
    yOut = yOut * (1.0 - blackLift * 2.0) + blackLift;

    vec3 ntscColor = clamp(yiqToRgb(vec3(yOut, iq)), 0.0, 1.0);

    float scanlinePhase = gl_FragCoord.y * 3.14159265;
    float scanline = 1.0 - clamp(vhsOverlayScanlineStrength, 0.0, 1.0) *
        (0.18 + 0.22 * (0.5 + 0.5 * sin(scanlinePhase)));
    ntscColor *= scanline;

    float banding = clamp(vhsOverlayBanding, 0.0, 1.0);
    if (banding > 0.0001) {
        float levels = mix(64.0, 10.0, banding);
        vec3 degraded = floor(clamp(ntscColor, 0.0, 1.0) * levels) / levels;
        ntscColor = mix(ntscColor, degraded, banding * 0.7);
    }

    ntscColor = clamp(ntscColor, 0.0, 1.0);
    return mix(color, ntscColor, clamp(vhsOverlayOpacity, 0.0, 1.0));
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
    vec3 outputColor = toneMap(color);
    outputColor = applyStaticEffect(outputColor);
    outputColor = applyVhsOverlay(outputColor, sampleUv);

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
