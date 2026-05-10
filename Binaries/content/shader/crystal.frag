// Crystal Refraction Fragment Shader
// Screen-space refraction driven by a normal map, with GGX specular and Fresnel rim.
// Lighting computed in view space (camera at origin). TBN built from screen-space
// derivatives so no tangent vertex attributes are required.

#define MAX_LIGHTS 8
#define PI 3.14159265358979323846

varying vec3 vViewPos;
varying vec3 vViewNormal;
varying vec2 vTexCoord;

uniform sampler2D tDiffuse;    // slot 0: crystal albedo / tint
uniform sampler2D tNormalMap;  // slot 1: normal map
uniform sampler2D tRefraction; // slot 2: opaque scene grab (injected per-draw)

uniform float uRefractionStrength;
uniform float uTransparency;    // 0=fully see-through, 1=fully opaque tint
uniform float uFresnelPower;
uniform float uFresnelStrength; // rim brightness (0=no rim, higher=more glow)
uniform float uShimmerSpeed;    // animation rate (0=frozen, 1=normal, higher=faster)
uniform float uTime;            // seconds elapsed
uniform vec3  uCrystalColor;    // per-prop RGB tint (default white)
uniform vec2  iResolution;

uniform vec3  uLightPos[MAX_LIGHTS];
uniform vec4  uLightColor[MAX_LIGHTS];
uniform float uLightRadius[MAX_LIGHTS];
uniform float uLightCount;
uniform vec3  uAmbientColor;

// ---------------------------------------------------------------------------
// GGX specular helpers (same as phong_perpixel)
// ---------------------------------------------------------------------------
float D_GGX(float NdotH, float roughness)
{
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float G_SmithSchlick(float NdotV, float NdotL, float roughness)
{
    float k  = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    float GV = NdotV / (NdotV * (1.0 - k) + k);
    float GL = NdotL / (NdotL * (1.0 - k) + k);
    return GV * GL;
}

vec3 F_Schlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ---------------------------------------------------------------------------
void main()
{
    // --- TBN from screen-space derivatives (no tangent attributes required) ---
    // Flip the geometric normal for back faces — culling is disabled for the
    // per-triangle sort, so back faces are rendered and their outward normal
    // points away from the camera without this correction.
    vec3 N = normalize(vViewNormal);
    if (!gl_FrontFacing) N = -N;

    vec3 dp1  = dFdx(vViewPos);
    vec3 dp2  = dFdy(vViewPos);
    vec2 duv1 = dFdx(vTexCoord);
    vec2 duv2 = dFdy(vTexCoord);

    // --- Dual-layer animated normal map (Tarydium shimmer) ---
    float t   = uTime * uShimmerSpeed;
    vec2  uv1 = vTexCoord + vec2( 0.020,  0.015) * t;
    vec2  uv2 = vTexCoord * 1.4 + vec2(-0.013,  0.021) * t;
    vec3 nm1  = texture2D(tNormalMap, uv1).rgb * 2.0 - 1.0;
    vec3 nm2  = texture2D(tNormalMap, uv2).rgb * 2.0 - 1.0;
    vec3 nmSample = normalize(nm1 + nm2);

    float det = duv1.x * duv2.y - duv1.y * duv2.x;
    vec3 mappedNormal;
    if (abs(det) < 0.0001)
    {
        // Degenerate UV island — TBN is undefined, fall back to geometric normal.
        mappedNormal = N;
    }
    else
    {
        float invDet = 1.0 / (det < 0.0 ? min(det, -0.0001) : max(det, 0.0001));
        vec3 T = normalize((dp1 * duv2.y - dp2 * duv1.y) * invDet);
        vec3 B = normalize((dp2 * duv1.x - dp1 * duv2.x) * invDet);
        mappedNormal = normalize(mat3(T, B, N) * nmSample);
    }

    // --- Screen-space refraction ---
    vec2 screenUV  = gl_FragCoord.xy / iResolution;
    vec2 distort   = nmSample.xy * uRefractionStrength;
    vec3 refracted = texture2D(tRefraction, screenUV + distort).rgb;

    // --- Crystal albedo tint ---
    vec4 albedo = texture2D(tDiffuse, vTexCoord);

    // --- View direction (camera at origin in view space) ---
    vec3 V     = normalize(-vViewPos);
    float NdotV = max(dot(mappedNormal, V), 0.0001);

    // --- Fresnel rim (Schlick approximation, F0=0.04 for glass) ---
    // Gentle pulse driven by the shimmer clock gives the rim a breathing quality.
    vec3 F0      = vec3(0.04);
    float pulse   = 0.85 + 0.15 * sin(uTime * uShimmerSpeed * 2.0);
    float fresnel = pow(1.0 - NdotV, uFresnelPower) * pulse;

    // --- Diffuse + specular from dynamic lights ---
    // Diffuse drives visible animated shimmer at any opacity level — the moving normal
    // map creates light/shadow variation across facets even when refraction fades out.
    float roughness    = 0.15;
    vec3 diffuseAccum  = vec3(0.0);
    vec3 specularAccum = vec3(0.0);

    for (int i = 0; i < MAX_LIGHTS; i++)
    {
        if (float(i) >= uLightCount) break;

        vec3  toLight = uLightPos[i] - vViewPos;
        float dist    = length(toLight);
        vec3  L       = toLight / dist;

        float atten = clamp(1.0 - (dist * dist) / (uLightRadius[i] * uLightRadius[i]), 0.0, 1.0);
        if (atten <= 0.0) continue;

        float NdotL = max(dot(mappedNormal, L), 0.0);
        diffuseAccum += uLightColor[i].rgb * NdotL * atten;

        if (NdotL > 0.0)
        {
            vec3  H     = normalize(L + V);
            float NdotH = max(dot(mappedNormal, H), 0.0);
            float VdotH = max(dot(V, H), 0.0);

            float D   = D_GGX(NdotH, roughness);
            float G   = G_SmithSchlick(NdotV, max(NdotL, 0.0001), roughness);
            vec3  F   = F_Schlick(VdotH, F0);

            vec3 spec = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);
            specularAccum += spec * uLightColor[i].rgb * NdotL * atten;
        }
    }

    // --- Composite ---
    // Base color is multiplied by total light (ambient floor + diffuse) so the crystal
    // responds to scene lighting at any opacity. Fresnel rim scales down as the crystal
    // becomes opaque to prevent bloom blow-out on the already-lit surface.
    vec3 base       = mix(refracted * albedo.rgb * uCrystalColor, albedo.rgb * uCrystalColor, uTransparency);
    vec3 totalLight = uAmbientColor + diffuseAccum;
    float fresnelScale = 1.0 - uTransparency;
    vec3 color = base * totalLight + fresnel * vec3(uFresnelStrength) * fresnelScale + specularAccum;

    gl_FragColor = vec4(color, 1.0);
}
