#version 330 core
out vec4 FragColor;

in vec2 TexCoord;

uniform sampler2D sceneTex;
uniform float threshold = 1.0;

void main() {
    vec3 c = texture(sceneTex, TexCoord).rgb;
    float luma = dot(c, vec3(0.2125, 0.7154, 0.0721));
    float bright = max(luma - threshold, 0.0);
    vec3 masked = c * step(0.0, bright);
    FragColor = vec4(masked, 1.0);
}
