#version 330 compatibility
// SSAO generation — hemisphere sampling against the prepass normal+depth buffer.
// Runs at half resolution; output is raw (noisy) AO in the red channel, cleaned
// up by ssao_blur.frag before ssao_apply multiplies it onto the scene.

uniform sampler2D tPrepass;    // unit 0: rgb = view normal, a = linear view depth (0 = sky)
uniform vec2  uProjTan;        // (tan(fovX/2), tan(fovY/2)) — view reconstruction
uniform float uRadius;         // sample hemisphere radius in world units
uniform float uIntensity;      // occlusion strength multiplier

varying vec2 vTexCoord;

#define KERNEL 12

void main()
{
    vec4  pn    = texture2D(tPrepass, vTexCoord);
    float depth = pn.a;
    if (depth <= 0.001)                    // sky / no geometry
    {
        gl_FragColor = vec4(1.0);
        return;
    }

    vec3 N = normalize(pn.rgb);
    // Reconstruct view-space position (left-handed, +z into screen).
    vec3 P = vec3((vTexCoord * 2.0 - 1.0) * uProjTan * depth, depth);

    // Per-pixel random rotation angle — breaks kernel banding into noise,
    // which the depth-aware blur then smooths out.
    float ang = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453) * 6.2831853;
    float ca = cos(ang), sa = sin(ang);

    // Tangent basis around the surface normal.
    vec3 up = (abs(N.z) < 0.99) ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 T  = normalize(cross(up, N));
    vec3 B  = cross(N, T);

    // Fixed hemisphere kernel (unit vectors * scale), biased toward the center.
    const vec3 kern[KERNEL] = vec3[](
        vec3( 0.208, 0.104,  0.302) * 0.35,
        vec3(-0.278, 0.156,  0.245) * 0.45,
        vec3( 0.096,-0.312,  0.288) * 0.55,
        vec3(-0.134,-0.201,  0.397) * 0.60,
        vec3( 0.371, 0.148,  0.229) * 0.65,
        vec3(-0.416, 0.302,  0.192) * 0.70,
        vec3( 0.229,-0.442,  0.240) * 0.75,
        vec3(-0.117, 0.471,  0.301) * 0.80,
        vec3( 0.482, 0.271,  0.255) * 0.85,
        vec3(-0.301,-0.436,  0.318) * 0.90,
        vec3( 0.128, 0.542,  0.361) * 0.95,
        vec3(-0.526,-0.118,  0.401) * 1.00
    );

    float occ = 0.0;
    for (int i = 0; i < KERNEL; ++i)
    {
        vec3 k   = kern[i];
        vec3 dir = T * (k.x * ca - k.y * sa) + B * (k.x * sa + k.y * ca) + N * k.z;
        vec3 S   = P + dir * uRadius;
        if (S.z <= 0.05)
            continue;

        vec2 suv = (S.xy / (S.z * uProjTan)) * 0.5 + 0.5;
        if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0)
            continue;

        // Depth-proportional bias: the prepass stores depth in FP16, whose
        // quantization step grows with distance (~0.03 at depth 50, ~0.06 at
        // 100) — a fixed bias causes false self-occlusion on distant walls.
        float bias = max(0.05, S.z * 0.003);
        float sd = texture2D(tPrepass, suv).a;
        if (sd > 0.001 && sd < S.z - bias)
        {
            // Fade contribution when the occluder is far outside the radius
            // (prevents distant silhouettes casting halos).
            occ += smoothstep(0.0, 1.0, uRadius / abs(P.z - sd));
        }
    }

    float ao = clamp(1.0 - (occ / float(KERNEL)) * uIntensity, 0.0, 1.0);
    gl_FragColor = vec4(ao, ao, ao, 1.0);
}
