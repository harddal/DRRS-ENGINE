// Retro radioactive grain: continuous film-grain base + stochastic halftone
// dots + rare Geiger-counter pulse flashes. Phosphorescent green-yellow palette.

uniform sampler2D tScene;
uniform float     uGrainStrength;
uniform float     uTime;
uniform vec2      uResolution;

varying vec2 vTexCoord;

// Two-level sin hash: first level maps pixel coords to [0,1] keeping
// the sin argument ~580K max; second level mixes with frame at ~43K max,
// well within float32 sin() precision (problems start above ~5M).
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}
float hash2(vec2 p) {
    return fract(sin(dot(p, vec2(311.7, 127.1))) * 43758.5453123);
}

void main()
{
    vec3  c  = texture2D(tScene, vTexCoord).rgb;
    vec2  px = gl_FragCoord.xy;
    float fr = mod(floor(uTime * 24.0), 97.0);

    float hA = hash(px);
    float hB = hash2(px);

    // -------------------------------------------------------------------
    // Layer 1: continuous film-grain base
    // Quantised to three levels {-1, 0, +1} for the gritty, chunky texture
    // of aged film stock or a low-res phosphor display.
    // -------------------------------------------------------------------
    float g = hash(vec2(hA * 100.0, fr)) * 2.0 - 1.0;
    g = sign(g) * step(0.35, abs(g));
    c = clamp(c + g * uGrainStrength * 0.12, 0.0, 1.0);

    // -------------------------------------------------------------------
    // Layer 2: stochastic halftone dots
    // Screen tiled into 8x8 px cells. Each cell randomly places one soft
    // circle, giving the irregular-dot character of stochastic halftone
    // printing or phosphorescent particle tracks on a scintillator.
    // -------------------------------------------------------------------
    float cellSz = 8.0;
    vec2  cell0  = floor(px / cellSz);
    float dots   = 0.0;

    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            vec2  nc  = cell0 + vec2(float(dx), float(dy));
            float ncA = hash (nc + 7.3) * 100.0;
            float ncB = hash2(nc + 3.1) * 100.0;

            // Per-cell per-frame hit roll
            float present = step(1.0 - uGrainStrength * 0.6, hash(vec2(ncA, fr)));

            // Random centre within cell, random radius 1.2–3.0 px
            float cx = hash (vec2(ncB, fr + 11.0));
            float cy = hash2(vec2(ncB, fr + 11.0));
            vec2  ctr = (nc + vec2(cx, cy)) * cellSz;
            float r   = 1.0;

            float d = length(px - ctr);
            dots = max(dots, (1.0 - smoothstep(r * 0.3, r, d)) * present);
        }
    }

    c = clamp(c + dots * vec3(1.0, 1.0, 1.0) * 0.8, 0.0, 1.0);

    // -------------------------------------------------------------------
    // Layer 3: Geiger-counter pulse flashes
    // Rare circular bursts: bright white core fading to green-yellow glow,
    // like the flash of a scintillator crystal absorbing a gamma ray.
    // -------------------------------------------------------------------
    for (int i = 0; i < 3; i++) {
        float fi  = float(i);
        vec2  sd  = vec2(fi * 17.3 + fr * 3.1, fi * 31.7 + fr * 5.9);
        float sA  = hash(sd)  * 100.0;
        float sB  = hash2(sd) * 100.0;

        float present = step(1.0 - uGrainStrength * 0.18, hash(vec2(sA, fr + 3.0)));
        vec2  ctr = vec2(
            hash (vec2(sA, fr + 7.0)) * uResolution.x,
            hash2(vec2(sB, fr + 7.0)) * uResolution.y
        );
        float r = 4.0 + 14.0 * hash(vec2(sA, fr + 13.0));
        float d = length(px - ctr);

        float core = 1.0 - smoothstep(0.0,        r * 0.25, d);
        float glow = 1.0 - smoothstep(r * 0.25, r,          d);
        c += mix(vec3(0.25, 1.0, 0.15), vec3(1.0, 1.0, 0.8), core) * glow * 0.7 * present;
    }

    c = clamp(c, 0.0, 1.0);
    gl_FragColor = vec4(c, 1.0);
}
