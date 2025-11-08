#version 300 es

uniform mat4 u_modelViewProj;

in vec3 a_position;
in vec3 a_normal;
in vec4 a_color0;

out vec3 vertexNormal;
out vec4 vertexColor;

void main()
{
  gl_Position = u_modelViewProj * vec4(a_position, 1.0);
  vertexNormal = a_normal;
  vertexColor = a_color0;
}
