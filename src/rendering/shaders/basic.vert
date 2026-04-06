#version 450 core

layout(location = 0) in vec3 inPosition;

// Transform Storage Buffer
struct Transform {
    mat4 modelMatrix;
};
layout(std430, set = 0, binding = 0) readonly buffer Transforms {
    Transform transforms[];
};

// We don't have a camera matrix binding right now, just the model matrix. 
// We are doing headless output specifically to test statistics, not visual placement perfection!
void main() {
    uint idx = gl_InstanceIndex;
    mat4 model = transforms[idx].modelMatrix;
    gl_Position = model * vec4(inPosition, 1.0);
}
