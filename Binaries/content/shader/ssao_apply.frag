#version 330 compatibility
// Multiplies blurred SSAO onto the scene. Runs directly AFTER the tonemap pass
// (LDR space) — applying AO to the HDR scene before a filmic tonemap at high
// exposure compresses the darkening into the curve's shoulder and makes it
// nearly invisible. The AO buffer sits on raw texture unit 13, bound once per
// frame by RenderManager::drawSSAO().

uniform sampler2D tScene;   // unit 0: tonemapped LDR scene
uniform sampler2D tAO;      // unit 13: blurred half-res AO (bilinear upsample)

varying vec2 vTexCoord;

void main()
{
    vec4  color = texture2D(tScene, vTexCoord);
    float ao    = texture2D(tAO, vTexCoord).r;
    gl_FragColor = vec4(color.rgb * ao, color.a);
}
