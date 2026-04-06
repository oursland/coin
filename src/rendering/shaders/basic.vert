#version 450 core

layout(location = 0) in vec3 inPosition;

// Transform Storage Buffer
struct Transform {
    mat4 modelMatrix;
};
layout(std430, set = 0, binding = 0) readonly buffer Transforms {
    Transform transforms[];
};

// Camera MVP Uniform Buffer
layout(set = 0, binding = 2, std140) uniform CameraUniform {
    mat4 viewProj;
} cam;

void main() {
    uint idx = gl_InstanceIndex;
    mat4 model = transforms[idx].modelMatrix;
    gl_Position = cam.viewProj * model * vec4(inPosition, 1.0);
}
