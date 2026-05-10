// Quake-esque water fragment shader.
// Two scrolling layers with classic sine-wave turbulence, sampled at a
// fixed world-space tile scale so the effect never stretches with the mesh.

uniform sampler2D tDiffuse;   // water surface texture        (slot 0)
uniform sampler2D tNormal;    // optional second layer/overlay (slot 1)
uniform float     uHasNormal; // 1.0 when slot 1 texture is bound

uniform float fTime;

uniform vec3  uShallowColor;  // encoded from material AmbientColor in RenderSystem
uniform vec3  uDeepColor;     // encoded from material DiffuseColor in RenderSystem
uniform float uAlpha;         // encoded from material DiffuseColor.alpha

varying vec2 vWorldUV;

// Tile repeat frequency in world-units (metres).
// 1.0 = repeats every 1 m, 2.0 = repeats every 0.5 m.
const float TILE      = 0.25;

// Warp parameters (tune to taste).
const float WARP_AMP  = 0.05;   // amplitude in tile-space
const float WARP_FREQ = 4.0;    // spatial frequency

void main()
{
    vec2 uv = vWorldUV * TILE;

    // --- Layer 1: scrolls diagonally, warped by sine waves from base UV ---
    // Both sin inputs use the pre-scroll base UV to avoid feedback discontinuities.
    vec2 scroll1 = vec2(0.04, 0.03) * fTime;
    vec2 uv1 = uv + scroll1 + vec2(
        sin(uv.y * WARP_FREQ * 6.2831 + fTime * 1.2) * WARP_AMP,
        sin(uv.x * WARP_FREQ * 6.2831 + fTime * 0.9) * WARP_AMP
    );
    vec4 col1 = texture2D(tDiffuse, uv1);

    // --- Layer 2: scrolls opposite direction (same tex or slot-1 overlay) ---
    vec2 scroll2 = vec2(-0.02, -0.025) * fTime;
    vec2 uv2 = uv + scroll2 + vec2(
        sin(uv.y * WARP_FREQ * 6.2831 + fTime * 0.8) * WARP_AMP,
        sin(uv.x * WARP_FREQ * 6.2831 + fTime * 1.1) * WARP_AMP
    );
    vec4 col2 = (uHasNormal > 0.5)
        ? texture2D(tNormal,  uv2)
        : texture2D(tDiffuse, uv2);

    // Average the two layers.
    vec3 texColor = mix(col1.rgb, col2.rgb, 0.5);

    // Use texture luminance to blend shallow/deep color — brighter texels read
    // as shallow, darker texels as deep. Ties color variation to the surface
    // detail rather than a separate slow-moving wave.
    float luma      = dot(texColor, vec3(0.299, 0.587, 0.114));
    vec3 waterColor = mix(uDeepColor, uShallowColor, luma);

    // Multiply by 2 to match Quake's overbright water brightness.
    vec3 finalColor = texColor * waterColor * 2.0;

    gl_FragColor = vec4(finalColor, uAlpha);
}
