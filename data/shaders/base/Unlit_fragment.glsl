#version 300 es

precision highp float;

out vec4 fragColor;
in vec4 vertexColor;

void main()
{
    fragColor = vertexColor;
}
