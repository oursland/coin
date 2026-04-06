#version 450 core

layout(location = 0) out vec4 outColor;

void main() {
    // Output a flat white color for headless metric tracking!
    outColor = vec4(1.0, 1.0, 1.0, 1.0);
}
