#version 450
#ifdef VERTEX_SHADER

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 uv;

void main() {
    
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 0.0, 1.0);
    fragColor = inColor;
    uv = inUV;
}

#elif FRAGMENT_SHADER

layout(binding = 1) uniform sampler2D texSampler;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 uv;
layout(location = 2) in mat3 TBN;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(texSampler, uv);
}
#endif