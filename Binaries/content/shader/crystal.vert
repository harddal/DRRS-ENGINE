// Crystal Refraction Vertex Shader
// Identical output to phong_perpixel.vert — view-space position and normal
// via GL built-ins, plus texture UV. No lightmap UV needed.

varying vec3 vViewPos;
varying vec3 vViewNormal;
varying vec2 vTexCoord;

void main()
{
    gl_Position  = gl_ModelViewProjectionMatrix * gl_Vertex;
    vViewPos     = (gl_ModelViewMatrix * gl_Vertex).xyz;
    vViewNormal  = normalize(gl_NormalMatrix * gl_Normal);
    vTexCoord    = gl_MultiTexCoord0.xy;
}
