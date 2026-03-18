#version 330 core

in vec2 vUv;
in vec4 vColor;
in vec4 vParams;

out vec4 FragColor;

uniform sampler2D u_spriteTex;
uniform sampler2D u_additiveLightTex;
uniform sampler2D u_multiplyLightTex;
uniform sampler2D u_subtractiveLightTex;
uniform vec2 u_viewportSize;
uniform vec3 u_baseAmbient;

void main() {
    vec4 sprite = texture(u_spriteTex, vUv) * vColor;
    if (sprite.a <= 0.0001) {
        discard;
    }

    float receiveLighting = vParams.x;
    float unlit = vParams.y;
    float emissive = max(0.0, vParams.z);

    vec3 result = sprite.rgb;
    if (receiveLighting > 0.5 && unlit < 0.5) {
        vec2 lightUv = gl_FragCoord.xy / max(u_viewportSize, vec2(1.0));
        vec3 additive = texture(u_additiveLightTex, lightUv).rgb;
        vec3 multiply = texture(u_multiplyLightTex, lightUv).rgb;
        vec3 subtractive = texture(u_subtractiveLightTex, lightUv).rgb;
        result = sprite.rgb * u_baseAmbient;
        result = (result + sprite.rgb * additive) * multiply;
        result = max(vec3(0.0), result - sprite.rgb * subtractive);
    }

    result += sprite.rgb * emissive;
    FragColor = vec4(result, sprite.a);
}
