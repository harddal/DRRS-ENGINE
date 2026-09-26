#version 330 compatibility
// Source-engine-style water — vertex stage.
//
// Everything the fragment stage needs is world-space, because the surface
// detail (wave normals) is tiled against world coordinates rather than mesh
// UVs: a water brush can be scaled to any size and the ripples keep a constant
// real-world scale instead of stretching with the mesh.
//
// vViewZ matches the geometry prepass convention exactly (LH view space, +z
// into the screen) so the fragment stage can subtract the two and get the
// thickness of water between this surface and whatever is behind it.

uniform mat4 mWorld;     // object -> world                      (set by WaterShaderCallback)
uniform mat4 mWorldIT;   // inverse-transpose of mWorld, for normals

varying vec3  vWorldPos;
varying vec3  vWorldNormal;
varying float vViewZ;

void main()
{
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;

    vec4 wp      = mWorld * gl_Vertex;
    vWorldPos    = wp.xyz;

    // Water brushes are commonly non-uniformly scaled (a thin, wide box), so a
    // plain mat3(mWorld) would skew the normal. The inverse-transpose does not.
    vWorldNormal = mat3(mWorldIT) * gl_Normal;

    vViewZ       = (gl_ModelViewMatrix * gl_Vertex).z;   // LH view space: +z into screen
}
