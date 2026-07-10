#version 330 core

uniform mat4 ortho;

layout(location = 0) in vec4 aPosition;

out vec3 position;

void main(void)
{    
    gl_Position = ortho*vec4(aPosition.xyz,1);
    position = (gl_Position.xyz / 2 + 0.5);
}