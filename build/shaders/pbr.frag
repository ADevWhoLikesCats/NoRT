#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec3 camPos;
    vec4 lights[3];
    vec4 lightColors[3];
} ubo;

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragAlbedo;
layout(location = 3) in float fragMetallic;
layout(location = 4) in float fragRoughness;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return a2 / max(denom, 1e-6);
}

float geometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    vec3  N         = normalize(fragNormal);
    vec3  V         = normalize(ubo.camPos - fragWorldPos);
    vec3  albedo    = fragAlbedo;
    float metallic  = fragMetallic;
    float roughness = clamp(fragRoughness, 0.04, 1.0);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 kD = (1.0 - fresnelSchlick(max(dot(N, V), 0.0), F0)) * (1.0 - metallic);

    vec3 Lo = vec3(0.0);

    for (int i = 0; i < 3; ++i) {
        vec3  lightPos   = ubo.lights[i].xyz;
        vec3  lightColor = ubo.lightColors[i].rgb;

        vec3  L     = normalize(lightPos - fragWorldPos);
        vec3  H     = normalize(V + L);
        float dist  = length(lightPos - fragWorldPos);
        float atten = 1.0 / (dist * dist);

        float D = distributionGGX(N, H, roughness);
        float G = geometrySmith(N, V, L, roughness);
        vec3  F = fresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3  numerator   = D * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 1e-6;
        vec3  specular    = numerator / denominator;

        float NdotL = max(dot(N, L), 0.0);
        Lo += (kD * albedo / PI + specular) * lightColor * atten * NdotL;
    }

    vec3 ambient = 0.03 * albedo;
    vec3 color   = ambient + Lo;
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    outColor = vec4(color, 1.0);
}
