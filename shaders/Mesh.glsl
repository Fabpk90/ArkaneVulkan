#version 450
#ifdef VERTEX_SHADER

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;

layout(location = 0) out vec2 uv;

void main() {
    
    gl_Position = vec4(inPosition, 1.0);
    uv = inUv;
}

#elif FRAGMENT_SHADER

layout(location = 0) in vec2 uv;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(uv, 1, 1.0);
}
#endif