#version 330 compatibility

// Per-frame constants — std140 block filled once per frame by RenderManager
// (updatePerFrameUBO) and bound to binding point 0 by the fork patch in
// COpenGLSLMaterialRenderer (see Include/irrlicht/PATCHES.md). Member names
// match the old per-draw uniforms; layout must mirror struct PerFrameData in
// RenderManager.cpp. fTime is engine time in MILLISECONDS; uTime is seconds.
layout(std140) uniform PerFrame
{
    vec3  uAmbientColor;  float uHasShadow;
    vec3  uFogColor;      float uFogDensity;
    vec3  uCamRight;      float uFogStart;
    vec3  uCamUp;         float uHasEnvMap;
    vec3  uCamForward;    float uShadowBias;
    vec4  uClusterParams; // (tileW_px, tileH_px, sliceScale, sliceBias)
    float fTime;          float uTime;  float uUseClusters;  float uPrepassValid;
    mat4  uInvView;       // main camera view inverse — world-pos reconstruction
    mat4  uShadowMat[4];  // lightProj*lightView per shadow atlas slot
    vec4  uShadowRect[4]; // xy = atlas offset, z = scale, w = 1 if slot active
    vec4  uFogVolParams;  // x = active fog-volume count, y = feather distance
    vec4  uFogVolMin[8];  // xyz = AABB min, w = density
    vec4  uFogVolMax[8];  // xyz = AABB max, w = start distance
    vec4  uFogVolColor[8];// rgb = color, w = reserved
};

// ---------------------------------------------------------------------------
// Localized fog: integrate fog along the camera->fragment ray.  The scene
// default fog (uFogColor/uFogDensity/uFogStart) applies over the ray portion
// outside every fog volume; each CONTENT_FOG AABB overrides density+color for
// the segment inside it, softened by a feather at the faces.  Colour is an
// optical-depth-weighted average (ordering-free) — an approximation of true
// front-to-back compositing that reads correctly for the target cases.
// ---------------------------------------------------------------------------
vec3 applyLocalFog(vec3 color, vec3 viewPos)
{
    vec3  camWS    = uInvView[3].xyz;
    vec3  fragWS   = (uInvView * vec4(viewPos, 1.0)).xyz;
    vec3  ray      = fragWS - camWS;
    float fragDist = length(ray);
    if (fragDist < 1e-4)
        return color;
    vec3 dir = ray / fragDist;

    float feather = max(uFogVolParams.y, 1e-3);
    int   count   = int(uFogVolParams.x + 0.5);

    float tau      = 0.0;        // total optical depth along the ray
    vec3  colNum   = vec3(0.0);  // sum of (segment colour * segment optical depth)
    float inLenSum = 0.0;        // total path length inside any volume

    for (int i = 0; i < 8; ++i)
    {
        if (i >= count)
            break;

        vec3 bmin = uFogVolMin[i].xyz;
        vec3 bmax = uFogVolMax[i].xyz;

        // Ray-AABB slab test in world space, clamped to [0, fragDist].
        // Nudge exact-zero ray axes off zero first: 1.0/0.0 = inf, and inf on a
        // face-aligned axis gives 0*inf = NaN, which whites out the fragment.
        vec3 dsafe  = dir + vec3(equal(dir, vec3(0.0))) * 1e-6;
        vec3 invD   = 1.0 / dsafe;
        vec3 t0     = (bmin - camWS) * invD;
        vec3 t1     = (bmax - camWS) * invD;
        vec3 tsmall = min(t0, t1);
        vec3 tbig   = max(t0, t1);
        float tNear = max(max(tsmall.x, tsmall.y), tsmall.z);
        float tFar  = min(min(tbig.x, tbig.y), tbig.z);
        tNear = max(tNear, 0.0);
        tNear = max(tNear, uFogVolMax[i].w);   // per-volume start distance from camera
        tFar  = min(tFar, fragDist);

        float inLen = max(tFar - tNear, 0.0);
        if (inLen <= 0.0)
            continue;

        // Feather: ramp the segment's contribution up over the first `feather`
        // world units so grazing/edge rays fade in instead of popping.
        float r    = clamp(inLen / feather, 0.0, 1.0);
        float soft = r * r * (3.0 - 2.0 * r);          // smoothstep
        float tauI = uFogVolMin[i].w * inLen * soft;

        tau      += tauI;
        colNum   += tauI * uFogVolColor[i].rgb;
        inLenSum += inLen;
    }

    // Scene-default fog over the ray portion not covered by any volume.
    float outDist = max((fragDist - inLenSum) - uFogStart, 0.0);
    float tauDef  = uFogDensity * outDist;
    tau    += tauDef;
    colNum += tauDef * uFogColor;

    if (!(tau > 1e-5))   // also catches NaN (any comparison with NaN is false)
        return color;

    float fogFactor = clamp(1.0 - exp(-tau), 0.0, 1.0);
    return mix(color, colNum / tau, fogFactor);
}

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

// Clustered lighting (ClusteredLightManager). When uUseClusters > 0.5, per-light
// data comes from texelFetch textures on units 8-10 and the arrays above are
// unused. Constants must match ClusteredLightManager: 16x8x24 grid, 4 texels
// per light, 256-wide index texture.
uniform sampler2D tLightData;     // unit 8: t0=pos+radius, t1=color+cosInner, t2=dir+cosOuter(-2=point)
uniform sampler2D tClusterGrid;   // unit 9: (offset, count) per froxel
uniform sampler2D tLightIndices;  // unit 10: flat light index list


uniform float uRoughness;
uniform float uMetallic;


uniform sampler2D tEnvMap;

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

    // Light list source: clustered path fetches this fragment's froxel list;
    // fallback path iterates the per-object uniform arrays (8 closest lights).
    int lightOffset = 0;
    int lightNum    = int(uLightCount);
    if (uUseClusters > 0.5)
    {
        ivec2 tile  = clamp(ivec2(gl_FragCoord.xy / uClusterParams.xy), ivec2(0), ivec2(15, 7));
        int   slice = clamp(int(floor(log2(max(vViewPos.z, 0.01)) * uClusterParams.z + uClusterParams.w)), 0, 23);
        vec2  oc    = texelFetch(tClusterGrid, ivec2(tile.x + tile.y * 16, slice), 0).xy;
        lightOffset = int(oc.x);
        lightNum    = int(oc.y);
    }

    for (int n = 0; n < lightNum; n++)
    {
        vec3  lightPos;
        vec3  lightColor;
        float lightRadius;
        if (uUseClusters > 0.5)
        {
            int  fi = lightOffset + n;
            int  li = int(texelFetch(tLightIndices, ivec2(fi & 255, fi >> 8), 0).x);
            vec4 t0 = texelFetch(tLightData, ivec2(li * 4 + 0, 0), 0);
            vec4 t1 = texelFetch(tLightData, ivec2(li * 4 + 1, 0), 0);
            lightPos    = t0.xyz;  lightRadius = t0.w;
            lightColor  = t1.xyz;
        }
        else
        {
            lightPos    = uLightPos[n];
            lightColor  = uLightColor[n].rgb;
            lightRadius = uLightRadius[n];
        }

        vec3  toLight = lightPos - vViewPos;
        float dist    = length(toLight);
        vec3  L       = toLight / dist;

        float atten = clamp(1.0 - (dist * dist) / (lightRadius * lightRadius), 0.0, 1.0);
        if (atten <= 0.0) continue;

        float NdotL = max(dot(N, L), 0.0);

        diffuseAccum += lightColor * NdotL * atten;

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
            specularAccum += spec * lightColor * NdotL * atten;
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

    gl_FragColor = vec4(applyLocalFog(color, vViewPos), texColor.a);
}
