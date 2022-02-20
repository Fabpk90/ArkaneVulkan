#version 450
#ifdef VERTEX_SHADER

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBiTangent;

layout(location = 0) out vec2 uv;
layout(location = 1) out mat3 TBN;

void main() {
    
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 1.0);
    uv = inUv;

    vec3 normal = normalize(cross(inTangent, inBiTangent));
    TBN = mat3(normal, inTangent, inBiTangent);
    //second approach is to multiply all vectors (view and light dir) in here
    // and we pass the result to the fragment, this saves us from computing the normal for each fragment
}

#elif FRAGMENT_SHADER

layout(location = 0) in vec2 uv;
layout(location = 1) in mat3 TBN;

layout(binding = 1) uniform sampler2D texSampler;

layout(location = 0) out vec4 outColor;

void main() {
    //  vec3 normal = texture(texNormal, uv).xyz * 2 - 1.0 remmapping from [-1;1] to [0; 1]
    // normalize(TBN * normal) convert to world space

    
    outColor = texture(texSampler, uv);
}
#endif