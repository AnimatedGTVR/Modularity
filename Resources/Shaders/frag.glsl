#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;

uniform sampler2D texture1;
uniform sampler2D overlayTex;
uniform sampler2D normalMap;
uniform float mixAmount = 0.2;
uniform bool hasOverlay = false;
uniform bool hasNormalMap = false;

uniform vec3 viewPos;
uniform vec3 materialColor = vec3(1.0);

uniform float ambientStrength = 0.2;
uniform float specularStrength = 0.5;
uniform float shininess = 32.0;
const int MAX_LIGHTS = 10;
uniform int lightCount = 0; // up to MAX_LIGHTS
// type: 0 dir, 1 point, 2 spot
uniform int lightTypeArr[MAX_LIGHTS];
uniform vec3 lightDirArr[MAX_LIGHTS];
uniform vec3 lightPosArr[MAX_LIGHTS];
uniform vec3 lightColorArr[MAX_LIGHTS];
uniform float lightIntensityArr[MAX_LIGHTS];
uniform float lightRangeArr[MAX_LIGHTS];
uniform float lightInnerCosArr[MAX_LIGHTS];
uniform float lightOuterCosArr[MAX_LIGHTS];

// Single directional light controlled by hierarchy (fallback if none set)
uniform vec3 lightDir = normalize(vec3(0.3, 1.0, 0.5));
uniform vec3 lightColor = vec3(1.0);
uniform float lightIntensity = 1.0;

void main()
{
    vec3 norm = normalize(Normal);
    vec3 viewDir = normalize(viewPos - FragPos);

    // Texture mixing (corrected)
    vec4 tex1 = texture(texture1, TexCoord);
    vec3 texColor = tex1.rgb;
    if (hasOverlay) {
        vec3 overlay = texture(overlayTex, TexCoord).rgb;
        texColor = mix(texColor, overlay, mixAmount);
    }
    vec3 baseColor = texColor * materialColor;

    // Normal map (tangent-space)
    if (hasNormalMap) {
        vec3 mapN = texture(normalMap, TexCoord).xyz * 2.0 - 1.0;
        vec3 dp1 = dFdx(FragPos);
        vec3 dp2 = dFdy(FragPos);
        vec2 duv1 = dFdx(TexCoord);
        vec2 duv2 = dFdy(TexCoord);
        vec3 tangent = normalize(dp1 * duv2.y - dp2 * duv1.y);
        vec3 bitangent = normalize(-dp1 * duv2.x + dp2 * duv1.x);
        mat3 TBN = mat3(tangent, bitangent, normalize(Normal));
        norm = normalize(TBN * mapN);
    }

    vec3 ambient = ambientStrength * baseColor;
    vec3 lighting = ambient;

    int count = min(lightCount, MAX_LIGHTS);
    for (int i = 0; i < count; ++i) {
        int ltype = lightTypeArr[i];
        vec3 ldir = (ltype == 0) ? -normalize(lightDirArr[i]) : lightPosArr[i] - FragPos;
        float dist = length(ldir);
        vec3 lDirN = normalize(ldir);

        float attenuation = 1.0;
        if (ltype != 0) {
            float range = lightRangeArr[i];
            if (range > 0.0 && dist > range) continue;
            if (range > 0.0) {
                float falloff = clamp(1.0 - (dist / range), 0.0, 1.0);
                attenuation = falloff * falloff;
            }
        }

        float intensity = lightIntensityArr[i];
        if (intensity <= 0.0) continue;

        float diff = max(dot(norm, lDirN), 0.0);
        vec3 diffuse = diff * lightColorArr[i] * intensity;

        vec3 halfwayDir = normalize(lDirN + viewDir);
        float spec = pow(max(dot(norm, halfwayDir), 0.0), shininess);
        vec3 specular = specularStrength * spec * lightColorArr[i] * intensity;

        if (ltype == 2) {
            float cosTheta = dot(-lDirN, normalize(lightDirArr[i]));
            float spotAtten = smoothstep(lightOuterCosArr[i], lightInnerCosArr[i], cosTheta);
            attenuation *= spotAtten;
        }

        lighting += attenuation * (diffuse + specular) * baseColor;
    }

    float alpha = tex1.a;
    FragColor = vec4(lighting, alpha);  // Preserve alpha if needed
}
