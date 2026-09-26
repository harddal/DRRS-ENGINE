#version 330 compatibility
// Writes (view-space normal, linear view depth) into the RGBA16F prepass RTT.
// The RTT is cleared to (0,0,0,0); depth == 0 marks "no geometry" (sky).
//
// ALPHA CUT-OUTS: this pass must discard exactly what the main pass discards,
// or a cut-out surface stamps its WHOLE QUAD into the depth channel. Everything
// that reads this buffer then believes there is solid geometry in the holes:
// SSAO and soft particles carve a phantom rectangle, and water — which measures
// its fog depth and its refraction offset against this buffer — reads "geometry
// in front of the surface", collapses its depth to zero and renders completely
// clear, so the water disappears behind every leaf card that overlaps it.
//
// uAlphaRef is 0 for opaque geometry (no texture fetch at all) and the source
// material's cut-off for foliage/grass/masked materials — see drawPrePass().

uniform sampler2D tDiffuse;
uniform float     uAlphaRef;   // 0 = opaque, no cut-out test

varying vec3  vViewNormal;
varying float vViewZ;
varying vec2  vTexCoord;

void main()
{
    if (uAlphaRef > 0.0 && texture2D(tDiffuse, vTexCoord).a < uAlphaRef)
        discard;

    gl_FragColor = vec4(normalize(vViewNormal), vViewZ);
}
