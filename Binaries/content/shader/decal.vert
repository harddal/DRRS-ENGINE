#version 330 compatibility
// Screen-space decal box — plain transform; all the work is per-fragment.

void main()
{
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
}
