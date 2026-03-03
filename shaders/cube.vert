#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inUV;
layout(location = 3) in uint inTextureId;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragUV;
layout(location = 2) flat out uint fragTextureId;

layout(set = 0, binding = 16) uniform CameraUBO {
    mat4 model;
    mat4 view;
    mat4 proj;
} camera;

void main() {
    gl_Position = camera.proj * camera.view * camera.model * vec4(inPosition, 1.0);
    fragColor = inColor;
    fragUV = inUV;
    fragTextureId = inTextureId;
}
