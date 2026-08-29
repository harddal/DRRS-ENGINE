// Telescopic sight — a sharp circular aperture over a heavily blurred surround.
//
// Replaces the old approach of masking the screen with an opaque texture. Doing
// it here rather than as a 2D overlay buys three things: the aperture is a true
// circle at any resolution or aspect (a stretched overlay turned it into an
// ellipse), the surround shows the world out of focus rather than a flat colour,
// and the aperture size becomes a uniform that can be animated with the zoom.

uniform sampler2D tScene;
uniform vec2  uRcpFrame;   // 1/width, 1/height
uniform float uAspect;     // width/height
uniform float uAperture;   // aperture radius, 1.0 == half the SHORT screen axis
uniform float uSoftness;   // feather width at the aperture edge
uniform float uBlurRadius; // blur tap spread, in pixels
uniform float uVignette;   // brightness of the blurred surround (1 = untouched)

varying vec2 vTexCoord;

void main()
{
    // Aspect-corrected distance from screen centre, normalised so that r == 1.0
    // sits on the SHORT axis. Correcting by aspect is what keeps the aperture
    // circular on a widescreen instead of stretching it into an oval.
    vec2 d = vTexCoord - vec2(0.5, 0.5);
    d.x *= uAspect;
    float r = length(d) * 2.0;

    vec4 sharp = texture2D(tScene, vTexCoord);

    // 0 inside the aperture, 1 well outside, feathered across uSoftness so the
    // edge of the sight is not a hard jagged circle.
    float mask = smoothstep(uAperture, uAperture + uSoftness, r);

    // Two rings of eight taps plus the centre. A single ring at a large radius
    // reads as a ghosted double image rather than as blur; the inner ring at half
    // the radius fills that gap for eight more fetches.
    vec2 o = uRcpFrame * uBlurRadius;

    vec4 acc = sharp;

    acc += texture2D(tScene, vTexCoord + vec2( 1.0,  0.0) * o);
    acc += texture2D(tScene, vTexCoord + vec2(-1.0,  0.0) * o);
    acc += texture2D(tScene, vTexCoord + vec2( 0.0,  1.0) * o);
    acc += texture2D(tScene, vTexCoord + vec2( 0.0, -1.0) * o);
    acc += texture2D(tScene, vTexCoord + vec2( 0.7,  0.7) * o);
    acc += texture2D(tScene, vTexCoord + vec2(-0.7,  0.7) * o);
    acc += texture2D(tScene, vTexCoord + vec2( 0.7, -0.7) * o);
    acc += texture2D(tScene, vTexCoord + vec2(-0.7, -0.7) * o);

    acc += texture2D(tScene, vTexCoord + vec2( 0.5,  0.0) * o);
    acc += texture2D(tScene, vTexCoord + vec2(-0.5,  0.0) * o);
    acc += texture2D(tScene, vTexCoord + vec2( 0.0,  0.5) * o);
    acc += texture2D(tScene, vTexCoord + vec2( 0.0, -0.5) * o);
    acc += texture2D(tScene, vTexCoord + vec2( 0.35, 0.35) * o);
    acc += texture2D(tScene, vTexCoord + vec2(-0.35, 0.35) * o);
    acc += texture2D(tScene, vTexCoord + vec2( 0.35,-0.35) * o);
    acc += texture2D(tScene, vTexCoord + vec2(-0.35,-0.35) * o);

    vec4 blurred = acc * (1.0 / 17.0);

    vec3 col = mix(sharp.rgb, blurred.rgb, mask);

    // Darken with distance as well as blurring. A real sight loses light towards
    // the edge, and it also stops the bright blurred surround from competing with
    // the target for attention.
    col *= mix(1.0, uVignette, mask);

    gl_FragColor = vec4(col, sharp.a);
}
