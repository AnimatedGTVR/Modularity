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

void main() {
    vec3 color = texture(sceneTex, TexCoord).rgb;
    if (enableBloom) {
        vec3 glow = texture(bloomTex, TexCoord).rgb;
        color += glow * bloomIntensity;
    }

    color = applyColorAdjust(color);

    if (enableMotionBlur && hasHistory) {
        vec3 history = texture(historyTex, TexCoord).rgb;
        color = mix(color, history, clamp(motionBlurStrength, 0.0, 0.98));
    }

    FragColor = vec4(color, 1.0);
}
