#version 330 compatibility
// Thin geometry pre-pass — view-space normal + linear view depth.
// Consumed by SSAO (and later: soft particles, screen-space decals).

varying vec3  vViewNormal;
varying float vViewZ;

void main()
{
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
    vViewNormal = gl_NormalMatrix * gl_Normal;
    vViewZ      = (gl_ModelViewMatrix * gl_Vertex).z;   // LH view space: +z into screen
}
