#version 430 core

layout(location = 0) in vec3 aPosition;
layout(location = 2) in vec2 aTexCoord;

uniform mat4 uModelViewProj;
uniform vec2 uUvOffset; // this frame's window into the noise texture
uniform vec2 uUvScale;

out vec2 vNoiseUv;
out vec2 vQuadUv; // 0..1 across the quad, for the edge fade

void main()
{
    vNoiseUv = uUvOffset + aTexCoord * uUvScale;
    vQuadUv = aTexCoord;
    gl_Position = uModelViewProj * vec4(aPosition, 1.0);
}
