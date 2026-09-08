// Contrast Adaptive Sharpening (AMD FidelityFX CAS), sharpen-only path -- no
// upscaling. GLSL 1.10, to match the shared fxaa.vert.
//
// Replaces a plain unsharp mask. The unsharp version applied the same gain
// everywhere, which meant it re-amplified exactly the luma steps FXAA had just
// blended away -- this pass runs immediately after "fxaa" in the chain -- and
// rang on every high-contrast edge. CAS derives its gain per pixel from local
// contrast: near-flat neighbourhoods get the full sharpen, neighbourhoods that
// are already near the top or bottom of the range get almost none. That recovers
// texture detail without undoing the antialiasing.
//
// uStrength is the CAS sharpness knob, 0..1. Note that 0 is NOT "off" -- it is
// the gentlest CAS curve. Disable the pass itself for no sharpening at all.

uniform sampler2D tScene;
uniform vec2      uRcpFrame;
uniform float     uStrength;

varying vec2 vTexCoord;

void main()
{
    vec2 du = vec2(uRcpFrame.x, 0.0);
    vec2 dv = vec2(0.0, uRcpFrame.y);

    //  a b c
    //  d e f
    //  g h i
    vec3 a = texture2D(tScene, vTexCoord - du - dv).rgb;
    vec3 b = texture2D(tScene, vTexCoord      - dv).rgb;
    vec3 c = texture2D(tScene, vTexCoord + du - dv).rgb;
    vec3 d = texture2D(tScene, vTexCoord - du     ).rgb;
    vec3 e = texture2D(tScene, vTexCoord          ).rgb;
    vec3 f = texture2D(tScene, vTexCoord + du     ).rgb;
    vec3 g = texture2D(tScene, vTexCoord - du + dv).rgb;
    vec3 h = texture2D(tScene, vTexCoord      + dv).rgb;
    vec3 i = texture2D(tScene, vTexCoord + du + dv).rgb;

    // Range of the cross, plus the range of the full 3x3 -- CAS weights the cross
    // twice so a diagonal edge does not read as flat.
    vec3 mn = min(min(min(d, e), min(f, b)), h);
    mn += min(min(min(mn, a), c), min(g, i));

    vec3 mx = max(max(max(d, e), max(f, b)), h);
    mx += max(max(max(mx, a), c), max(g, i));

    // The adaptive term: headroom to white measured against the local range.
    // Near-flat regions give amp ~1 (sharpen fully); regions already spanning the
    // range give amp ~0 (leave alone), which is what protects FXAA-blended edges.
    vec3 amp = clamp(min(mn, 2.0 - mx) / max(mx, vec3(1.0 / 255.0)), 0.0, 1.0);
    amp = sqrt(amp);

    // sharpness 0 -> -1/8 (gentle), 1 -> -1/5 (strong)
    float peak = -1.0 / mix(8.0, 5.0, clamp(uStrength, 0.0, 1.0));
    vec3  w    = amp * peak;

    // Normalised 5-tap cross filter: centre plus w-weighted neighbours.
    vec3 result = (b * w + d * w + f * w + h * w + e) / (1.0 + 4.0 * w);

    gl_FragColor = vec4(clamp(result, 0.0, 1.0), 1.0);
}
