#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec3 camPos;
    vec4 lights[3];
    vec4 lightColors[3];
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in float inMetallic;
layout(location = 4) in float inRoughness;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragAlbedo;
layout(location = 3) out float fragMetallic;
layout(location = 4) out float fragRoughness;

void main() {
    vec4 worldPos = ubo.model * vec4(inPosition, 1.0);
    fragWorldPos  = worldPos.xyz;

    mat3 normalMatrix = transpose(inverse(mat3(ubo.model)));
    fragNormal        = normalize(normalMatrix * inNormal);

    fragAlbedo    = inColor;
    fragMetallic  = inMetallic;
    fragRoughness = inRoughness;

    gl_Position = ubo.proj * ubo.view * worldPos;
}
