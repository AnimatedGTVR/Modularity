#version 330 core
out vec4 FragColor;

in vec2 TexCoord;

uniform sampler2D sceneTex;
uniform float threshold = 1.0;

void main() {
    vec3 c = texture(sceneTex, TexCoord).rgb;
    float luma = dot(c, vec3(0.2125, 0.7154, 0.0721));
    float knee = 0.25;
    float w = clamp((luma - threshold) / max(knee, 1e-4), 0.0, 1.0);
    w = w * w * (3.0 - 2.0 * w);
    vec3 masked = c * w;
    FragColor = vec4(masked, 1.0);
}
