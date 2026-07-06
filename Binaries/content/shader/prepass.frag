#version 330 compatibility
// Writes (view-space normal, linear view depth) into the RGBA16F prepass RTT.
// The RTT is cleared to (0,0,0,0); depth == 0 marks "no geometry" (sky).

varying vec3  vViewNormal;
varying float vViewZ;

void main()
{
    gl_FragColor = vec4(normalize(vViewNormal), vViewZ);
}
