// Per-Pixel Blinn-Phong Vertex Shader
// Computes everything in VIEW SPACE using GL built-ins to avoid
// Irrlicht row-major / GLSL column-major convention issues entirely.

varying vec3 vViewPos;     // view-space vertex position
varying vec3 vViewNormal;  // view-space normal
varying vec2 vTexCoord;
varying vec2 vLightmapUV;  // lightmap UV (UV channel 1, set by LightmapBaker)
varying vec4 vShadowClip;  // light clip-space coords (NOT divided by w — see fragment shader)

// lightProj * lightView * world — combined per-object, mirrors gl_ModelViewProjectionMatrix
// in the shadow depth pass so the depth values are numerically identical.
uniform mat4 mLightMVP;

void main()
{
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;

    // View-space position and normal via GL built-ins — no custom matrix uniforms needed.
    // gl_ModelViewMatrix  = correct view*world transform
    // gl_NormalMatrix     = transpose(inverse(modelview)) upper-left 3x3 = correct normal matrix
    vViewPos    = (gl_ModelViewMatrix * gl_Vertex).xyz;
    vViewNormal = normalize(gl_NormalMatrix * gl_Normal);

    vTexCoord   = gl_MultiTexCoord0.xy;
    vLightmapUV = gl_MultiTexCoord1.xy;

    // Pass clip-space coords WITHOUT dividing by w. Perspective divide must happen in the
    // fragment shader, not here. If we stored NDC (post-divide) in a varying, OpenGL would
    // perspective-correct it again during rasterization (double division), producing shadow
    // offsets that grow as the light gets closer (stronger perspective distortion).
    vShadowClip = mLightMVP * gl_Vertex;
}
