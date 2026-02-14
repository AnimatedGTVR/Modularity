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
uniform bool unlit = false;

uniform vec3 viewPos;
uniform vec3 materialColor = vec3(1.0);

uniform float ambientStrength = 0.2;
uniform vec3 ambientColor = vec3(1.0);
uniform float specularStrength = 0.5;
uniform float shininess = 32.0;

const int MAX_LIGHTS = 10;
const int MAX_SHADOW_MAPS = 4;
uniform int lightCount = 0; // up to MAX_LIGHTS

// type: 0 dir, 1 point, 2 spot, 3 area (rect)
uniform int lightTypeArr[MAX_LIGHTS];
uniform vec3 lightDirArr[MAX_LIGHTS];
uniform vec3 lightPosArr[MAX_LIGHTS];
uniform vec3 lightColorArr[MAX_LIGHTS];
uniform float lightIntensityArr[MAX_LIGHTS];
uniform float lightRangeArr[MAX_LIGHTS];
uniform float lightInnerCosArr[MAX_LIGHTS];
uniform float lightOuterCosArr[MAX_LIGHTS];
uniform vec2 lightAreaSizeArr[MAX_LIGHTS];
uniform float lightAreaFadeArr[MAX_LIGHTS];
uniform int lightShadowMapArr[MAX_LIGHTS];
uniform int lightShadowModeArr[MAX_LIGHTS]; // 0 off, 1 hard, 2 soft
uniform float lightShadowBiasArr[MAX_LIGHTS];
uniform float lightShadowSoftnessArr[MAX_LIGHTS];
uniform float lightShadowFarArr[MAX_LIGHTS];

uniform samplerCube shadowCube0;
uniform samplerCube shadowCube1;
uniform samplerCube shadowCube2;
uniform samplerCube shadowCube3;

float sampleShadowCube(int mapIndex, vec3 sampleDir)
{
    if (mapIndex == 0) return texture(shadowCube0, sampleDir).r;
    if (mapIndex == 1) return texture(shadowCube1, sampleDir).r;
    if (mapIndex == 2) return texture(shadowCube2, sampleDir).r;
    if (mapIndex == 3) return texture(shadowCube3, sampleDir).r;
    return 1.0;
}

float computeShadowOcclusion(int lightIndex, vec3 fragToLight, float nl)
{
    int mode = lightShadowModeArr[lightIndex];
    int mapIndex = lightShadowMapArr[lightIndex];
    if (mode <= 0 || mapIndex < 0 || mapIndex >= MAX_SHADOW_MAPS) return 0.0;

    float farPlane = max(lightShadowFarArr[lightIndex], 0.001);
    float currentDepth = length(fragToLight);
    if (currentDepth <= 0.0001) return 0.0;

    float baseBias = max(lightShadowBiasArr[lightIndex], 0.0001);
    float slopeBias = baseBias * (1.0 - clamp(nl, 0.0, 1.0));
    float bias = max(baseBias * 0.25, slopeBias);
    float hardDepth = sampleShadowCube(mapIndex, fragToLight) * farPlane;
    if (mode == 1) {
        return (currentDepth - bias > hardDepth) ? 1.0 : 0.0;
    }

    float softness = max(lightShadowSoftnessArr[lightIndex], 0.0);
    if (softness <= 0.0001) {
        return (currentDepth - bias > hardDepth) ? 1.0 : 0.0;
    }

    const vec3 sampleOffsetDirections[20] = vec3[](
        vec3(1, 1, 1), vec3(1, -1, 1), vec3(-1, -1, 1), vec3(-1, 1, 1),
        vec3(1, 1, -1), vec3(1, -1, -1), vec3(-1, -1, -1), vec3(-1, 1, -1),
        vec3(1, 1, 0), vec3(1, -1, 0), vec3(-1, -1, 0), vec3(-1, 1, 0),
        vec3(1, 0, 1), vec3(-1, 0, 1), vec3(1, 0, -1), vec3(-1, 0, -1),
        vec3(0, 1, 1), vec3(0, -1, 1), vec3(0, -1, -1), vec3(0, 1, -1)
    );

    float diskRadius = softness * (1.0 + currentDepth / farPlane);
    float shadow = 0.0;
    for (int i = 0; i < 20; ++i) {
        float closestDepth = sampleShadowCube(mapIndex, fragToLight + sampleOffsetDirections[i] * diskRadius) * farPlane;
        shadow += (currentDepth - bias > closestDepth) ? 1.0 : 0.0;
    }
    return shadow / 20.0;
}

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

    if (unlit) {
        FragColor = vec4(baseColor, tex1.a);
        return;
    }

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

    vec3 albedo = pow(max(baseColor, vec3(0.0)), vec3(2.2));
    float metallic = clamp(specularStrength, 0.0, 1.0);
    float smoothness = clamp(shininess / 256.0, 0.0, 1.0);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 diffuseColor = albedo * (1.0 - metallic);

    vec3 ambient = ambientStrength * ambientColor * diffuseColor;
    vec3 lighting = ambient;

    int count = min(lightCount, MAX_LIGHTS);
    for (int i = 0; i < count; ++i) {
        int ltype = lightTypeArr[i];
        float intensity = lightIntensityArr[i];
        if (intensity <= 0.0) continue;

        vec3 lDirN;
        float attenuation = 1.0;

        if (ltype == 0) {
            lDirN = -normalize(lightDirArr[i]);
        } else if (ltype == 3) { // area light approximate
            vec3 n = normalize(lightDirArr[i]);
            vec3 up = abs(n.y) > 0.9 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
            vec3 tangent = normalize(cross(up, n));
            vec3 bitangent = cross(n, tangent);

            vec3 center = lightPosArr[i];
            vec3 rel = FragPos - center;
            float distPlane = dot(rel, n);
            vec3 onPlane = FragPos - distPlane * n;
            vec2 halfSize = lightAreaSizeArr[i] * 0.5;
            vec2 local;
            local.x = dot(onPlane - center, tangent);
            local.y = dot(onPlane - center, bitangent);

            float fade = clamp(lightAreaFadeArr[i], 0.0, 1.0);
            vec2 absLocal = abs(local);
            float edgeWeight = 1.0;
            if (fade < 0.0001) {
                if (absLocal.x > halfSize.x || absLocal.y > halfSize.y) continue;
            } else {
                vec2 inner = halfSize * (1.0 - fade);
                vec2 delta = max(halfSize - inner, vec2(0.0001));
                vec2 outside = max(absLocal - inner, vec2(0.0));
                float maxOutside = max(outside.x / delta.x, outside.y / delta.y);
                edgeWeight = 1.0 - clamp(maxOutside, 0.0, 1.0);
                if (edgeWeight <= 0.0) continue;
                edgeWeight = smoothstep(0.0, 1.0, edgeWeight);
            }

            vec3 closest = center + tangent * local.x + bitangent * local.y;

            vec3 lvec = closest - FragPos;
            float dist = length(lvec);
            if (dist < 1e-4) continue;
            lDirN = normalize(lvec);

            float range = lightRangeArr[i];
            if (range > 0.0 && dist > range) continue;
            if (range > 0.0) {
                float falloff = clamp(1.0 - (dist / range), 0.0, 1.0);
                attenuation = falloff * falloff;
            }
            float facing = max(dot(n, -lDirN), 0.0);
            attenuation *= facing * edgeWeight;
        } else {
            vec3 ldir = lightPosArr[i] - FragPos;
            float dist = length(ldir);
            lDirN = normalize(ldir);

            float range = lightRangeArr[i];
            if (range > 0.0 && dist > range) continue;
            if (range > 0.0) {
                float falloff = clamp(1.0 - (dist / range), 0.0, 1.0);
                attenuation = falloff * falloff;
            }
        }

        float nl = max(dot(norm, lDirN), 0.0);
        vec3 diffuse = nl * diffuseColor * lightColorArr[i] * intensity;

        vec3 halfwayDir = normalize(lDirN + viewDir);
        float specPower = mix(8.0, 256.0, smoothness);
        float spec = pow(max(dot(norm, halfwayDir), 0.0), specPower);
        float vdh = max(dot(viewDir, halfwayDir), 0.0);
        vec3 fresnel = F0 + (1.0 - F0) * pow(1.0 - vdh, 5.0);
        vec3 specular = fresnel * spec * lightColorArr[i] * intensity;

        if (ltype == 2) {
            float cosTheta = dot(-lDirN, normalize(lightDirArr[i]));
            float spotAtten = smoothstep(lightOuterCosArr[i], lightInnerCosArr[i], cosTheta);
            attenuation *= spotAtten;
        }

        float shadow = 0.0;
        if (ltype != 0) {
            shadow = computeShadowOcclusion(i, lightPosArr[i] - FragPos, nl);
        }

        lighting += (1.0 - shadow) * attenuation * (diffuse + specular);
    }

    float alpha = tex1.a;
    vec3 finalColor = pow(max(lighting, vec3(0.0)), vec3(1.0 / 2.2));
    FragColor = vec4(finalColor, alpha);
}
