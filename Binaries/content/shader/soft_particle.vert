#version 330 compatibility
// Soft particle vertex shader — SPARK quads arrive pre-built in world space
// with per-vertex color; pass color, UV, and view depth through.

varying vec2  vTexCoord;
varying vec4  vColor;
varying float vViewZ;

void main()
{
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
    vViewZ      = (gl_ModelViewMatrix * gl_Vertex).z;
    vTexCoord   = gl_MultiTexCoord0.xy;
    vColor      = gl_Color;
}
