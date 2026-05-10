// Terrain Blend Fragment Shader
// Identical to phong_perpixel.frag except albedo is derived from a splat map
// blending up to 4 tiling detail textures over the original mesh diffuse.
//
// Slot layout:
//   tDiffuse  (slot 0) — original mesh texture, shown where splat is black
//   tLightmap (slot 1) — baked lightmap (unchanged from phong_perpixel)
//   tSplatMap (slot 2) — RGBA blend weights, sampled in UV1 (unique unwrap)
//   tDetailA  (slot 3) — painted layer A, tiled in UV0
//   tDetailB  (slot 4) — painted layer B
//   tDetailC  (slot 5) — painted layer C
//   tDetailD  (slot 6) — painted layer D

#define MAX_LIGHTS 8
#define PI 3.14159265358979323846

varying vec3 vViewPos;
varying vec3 vViewNormal;
varying vec2 vTexCoord;
varying vec2 vLightmapUV;

uniform sampler2D tDiffuse;
uniform sampler2D tLightmap;
uniform float     uHasLightmap;

uniform sampler2D tSplatMap;
uniform sampler2D tDetailA;
uniform sampler2D tDetailB;
uniform sampler2D tDetailC;
uniform sampler2D tDetailD;
uniform vec4      uDetailTiling; // per-layer UV tiling multiplier

uniform vec3  uLightPos[MAX_LIGHTS];
uniform vec4  uLightColor[MAX_LIGHTS];
uniform float uLightRadius[MAX_LIGHTS];
uniform float uLightCount;

uniform float uRoughness;
uniform float uMetallic;
uniform vec3  uAmbientColor;

uniform vec3  uFogColor;
uniform float uFogDensity;
uniform float uFogStart;

uniform sampler2D tEnvMap;
uniform float     uHasEnvMap;
uniform vec3 uCamRight;
uniform vec3 uCamUp;
uniform vec3 uCamForward;

// ---------------------------------------------------------------------------
// GGX / Cook-Torrance helpers (identical to phong_perpixel.frag)
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

vec3 F_Schlick(float VdotH, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - VdotH, 0.0, 1.0), 5.0);
}

// ---------------------------------------------------------------------------
void main()
{
    // Splat-blended albedo — original diffuse fades out as layers are painted
    vec4  splat      = texture2D(tSplatMap, vLightmapUV);
    float baseWeight = max(1.0 - (splat.r + splat.g + splat.b + splat.a), 0.0);
    vec4  texColor   = texture2D(tDiffuse,  vTexCoord)                       * baseWeight
                     + texture2D(tDetailA,  vTexCoord * uDetailTiling.x)     * splat.r
                     + texture2D(tDetailB,  vTexCoord * uDetailTiling.y)     * splat.g
                     + texture2D(tDetailC,  vTexCoord * uDetailTiling.z)     * splat.b
                     + texture2D(tDetailD,  vTexCoord * uDetailTiling.w)     * splat.a;
    vec3  albedo     = texColor.rgb;

    vec3 N = normalize(vViewNormal);
    vec3 V = normalize(-vViewPos);

    float NdotV = max(dot(N, V), 0.0001);

    vec3 F0 = mix(vec3(0.04), albedo, uMetallic);

    vec3 diffuseAccum  = vec3(0.0);
    vec3 specularAccum = vec3(0.0);

    for (int i = 0; i < MAX_LIGHTS; i++)
    {
        if (float(i) >= uLightCount)
            break;

        vec3  toLight = uLightPos[i] - vViewPos;
        float dist    = length(toLight);
        vec3  L       = toLight / dist;

        float atten = clamp(1.0 - (dist * dist) / (uLightRadius[i] * uLightRadius[i]), 0.0, 1.0);
        if (atten <= 0.0) continue;

        float NdotL = max(dot(N, L), 0.0);

        diffuseAccum += uLightColor[i].rgb * NdotL * atten;

        if (uRoughness < 0.99 && NdotL > 0.0)
        {
            vec3  H     = normalize(L + V);
            float NdotH = max(dot(N, H), 0.0);
            float VdotH = max(dot(V, H), 0.0);

            float r   = max(uRoughness, 0.025);
            float D   = D_GGX(NdotH, r);
            float G   = G_SmithSchlick(NdotV, max(NdotL, 0.0001), r);
            vec3  F   = F_Schlick(VdotH, F0);

            vec3 spec = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);
            specularAccum += spec * uLightColor[i].rgb * NdotL * atten;
        }
    }

    float diffuseFactor = 1.0 - uMetallic;

    vec3 bakedLight = (uHasLightmap > 0.5)
        ? texture2D(tLightmap, vLightmapUV).rgb + uAmbientColor
        : uAmbientColor;

    if (uHasEnvMap > 0.5 && uRoughness < 0.99)
    {
        vec3 R_view  = reflect(-V, N);
        vec3 R_world = mat3(uCamRight, uCamUp, uCamForward) * R_view;

        vec2 envUV = vec2(
            atan(R_world.x, R_world.z) / (2.0 * PI),
            asin(clamp(-R_world.y, -1.0, 1.0)) / PI + 0.5
        );
        vec3 envSample = texture2D(tEnvMap, envUV).rgb;

        vec3  envF      = F_Schlick(NdotV, F0);
        float roughFade = (1.0 - uRoughness) * (1.0 - uRoughness);
        specularAccum += envSample * envF * roughFade;
    }

    vec3 color = albedo * max(bakedLight, diffuseAccum) * diffuseFactor
               + specularAccum;

    float fogDist   = length(vViewPos) - uFogStart;
    float fogFactor = 1.0 - exp(-uFogDensity * max(fogDist, 0.0));
    fogFactor       = clamp(fogFactor, 0.0, 1.0);
    gl_FragColor    = vec4(mix(color, uFogColor, fogFactor), texColor.a);
}
