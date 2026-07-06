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
    float fTime;          float uTime;  float uUseClusters;  float uPerFramePad0;
    mat4  uInvView;       // main camera view inverse — world-pos reconstruction
    mat4  uShadowMat[4];  // lightProj*lightView per shadow atlas slot
    vec4  uShadowRect[4]; // xy = atlas offset, z = scale, w = 1 if slot active
};

// Hybrid PBR Fragment Shader
// Diffuse: Lambert accumulation (identical brightness to old Blinn-Phong)
// Specular: GGX NDF + Smith geometry + Schlick Fresnel (physically-correct highlights)
// Metallic: tints F0 with albedo, suppresses diffuse proportionally
// Lighting computed in VIEW SPACE. Camera is at origin in view space.

#define MAX_LIGHTS 8
#define PI 3.14159265358979323846

varying vec3 vViewPos;
varying vec3 vViewNormal;
varying vec2 vTexCoord;
varying vec2 vLightmapUV;

uniform sampler2D tDiffuse;
uniform sampler2D tLightmap;
uniform float     uHasLightmap; // 1.0 if a baked lightmap texture is bound, else 0.0

uniform sampler2D tShadowMap;   // unit 11 — 4096 shadow ATLAS, four 2048 quadrants (R = depth)

varying vec4 vShadowClip[4];    // light clip-space coords per atlas slot (pre-divide)

// Lights in VIEW SPACE
uniform vec3  uLightPos[MAX_LIGHTS];
uniform vec4  uLightColor[MAX_LIGHTS];
uniform float uLightRadius[MAX_LIGHTS];
uniform float uLightCount;

// Spotlight cone data (only meaningful when uLightIsSpot[i] > 0.5)
uniform vec3  uLightDir[MAX_LIGHTS];      // view-space direction the spot is pointing
uniform float uLightIsSpot[MAX_LIGHTS];   // 1.0 = spotlight, 0.0 = point light
uniform float uLightCosOuter[MAX_LIGHTS]; // cos(outerCone half-angle) — hard cutoff
uniform float uLightCosInner[MAX_LIGHTS]; // cos(innerCone half-angle) — full brightness

// Clustered lighting (ClusteredLightManager). When uUseClusters > 0.5, per-light
// data comes from texelFetch textures on units 8-10 and the arrays above are
// unused. Grid layout constants must match ClusteredLightManager (16x8x24 grid,
// 4 texels per light, 256-wide index texture).
uniform sampler2D tLightData;     // unit 8: t0=pos+radius, t1=color+cosInner, t2=dir+cosOuter(-2=point)
uniform sampler2D tClusterGrid;   // unit 9: (offset, count) per froxel
uniform sampler2D tLightIndices;  // unit 10: flat light index list

// PBR material params (mapped from SMaterial in C++)
//   uRoughness = 1 - sqrt(Shininess/128)   -- 0=mirror, 1=matte
//   uMetallic  = SpecularColor.alpha/255    -- 0=plastic, 1=metal
uniform float uRoughness;
uniform float uMetallic;


uniform float uFresnelStrength; // 0.0 = disabled (default); >0 adds view-angle rim glow
uniform float uFresnelPower;    // Schlick exponent (default 4.0)
uniform float uAlpha;           // per-material opacity multiplier (1.0 = fully opaque)

// UV animation — driven by MaterialTypeParams[4..7].  All zero = static (no cost).
uniform vec2  uTexScroll;     // UV offset per second; (0,0) = static
uniform float uTexWarpAmp;    // sine warp amplitude in UV units; 0 = no warp
uniform float uTexWarpSpeed;  // warp oscillation speed (radians/second)

// Environment map (equirectangular) for ambient specular reflections
uniform sampler2D tEnvMap;
// Camera world-space basis vectors — used to convert the view-space reflection
// vector to world space without any matrix convention ambiguity.
// In OpenGL view space: X+=right, Y+=up, Z+=out-of-screen (-forward).

// PBR texture maps — slots 4-7 (each optional; uHas* gates the sample)
uniform sampler2D tNormalMap;
uniform float     uHasNormalMap;
uniform sampler2D tRoughnessMap;
uniform float     uHasRoughnessMap;
uniform sampler2D tMetallicMap;
uniform float     uHasMetallicMap;
uniform sampler2D tEmissionMap;
uniform float     uHasEmissionMap;

// ---------------------------------------------------------------------------
// GGX / Cook-Torrance helpers
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

// 3x3 PCF against one shadow atlas quadrant. clip = light clip-space coords
// (pre-divide — dividing in the vertex shader would double-divide during
// varying interpolation); rect = (atlas offset.xy, scale, active). Returns
// light visibility: 1.0 = lit, 0.3 = fully shadowed (0.7 max darkness so
// ambient/baked light still reads inside shadows).
float shadowFactorPCF(vec4 clip, vec4 rect)
{
    vec3 sc = clip.xyz / clip.w;
    sc = sc * 0.5 + 0.5;
    if (sc.x < 0.0 || sc.x > 1.0 || sc.y < 0.0 || sc.y > 1.0 || sc.z < 0.0 || sc.z > 1.0)
        return 1.0;

    // Slope-scale bias: the constant term covers precision noise; the
    // derivative term grows the bias on surfaces at shallow angles to the
    // light, eliminating triangular acne without adding Peter-Panning.
    float slope = length(vec2(dFdx(sc.z), dFdy(sc.z)));
    float bias  = uShadowBias + slope * 3.0;

    // Clamp taps inside the quadrant (one-texel margin) so PCF never bleeds
    // into a neighboring light's map.
    float texel = 1.0 / 4096.0;
    vec2  base  = rect.xy + sc.xy * rect.z;
    vec2  lo    = rect.xy + vec2(texel);
    vec2  hi    = rect.xy + vec2(rect.z - texel);

    float shadowSum = 0.0;
    for (int sx = -1; sx <= 1; ++sx)
        for (int sy = -1; sy <= 1; ++sy)
        {
            vec2 auv = clamp(base + vec2(sx, sy) * texel, lo, hi);
            shadowSum += (sc.z - bias > texture2D(tShadowMap, auv).r) ? 1.0 : 0.0;
        }
    return 1.0 - (shadowSum / 9.0) * 0.7;
}

// ---------------------------------------------------------------------------
void main()
{
    // UV scroll + sine warp.  Both use the unmodified vTexCoord as the sin input
    // to avoid feedback discontinuities (same pattern as the water shader).
    vec2 animUV = vTexCoord + uTexScroll * uTime;
    animUV += vec2(
        sin(vTexCoord.y * 6.2831 + uTime * uTexWarpSpeed)       * uTexWarpAmp,
        sin(vTexCoord.x * 6.2831 + uTime * uTexWarpSpeed * 0.7) * uTexWarpAmp
    );

    vec4 texColor = texture2D(tDiffuse, animUV);
    vec3 albedo   = pow(texColor.rgb, vec3(2.2)); // sRGB -> linear

    vec3 N = normalize(vViewNormal);
    vec3 V = normalize(-vViewPos);

    // Normal map — derivative-based TBN so no tangent vertex attribute is needed.
    // Works with EVT_STANDARD and EVT_2TCOORDS (lightmapped meshes).
    if (uHasNormalMap > 0.5)
    {
        vec3  dp1 = dFdx(vViewPos);
        vec3  dp2 = dFdy(vViewPos);
        vec2 duv1 = dFdx(animUV);
        vec2 duv2 = dFdy(animUV);
        float det = duv1.x * duv2.y - duv1.y * duv2.x;
        if (abs(det) > 1e-6)
        {
            float inv = 1.0 / det;
            vec3 T = normalize((dp1 * duv2.y - dp2 * duv1.y) * inv);
            vec3 B = normalize((dp2 * duv1.x - dp1 * duv2.x) * inv);
            N = normalize(mat3(T, B, N) * (texture2D(tNormalMap, animUV).rgb * 2.0 - 1.0));
        }
    }

    float NdotV = max(dot(N, V), 0.0001);

    // Per-pixel roughness/metallic — fall back to material uniforms when no map is bound.
    float roughness = (uHasRoughnessMap > 0.5) ? texture2D(tRoughnessMap, animUV).r : uRoughness;
    float metallic  = (uHasMetallicMap  > 0.5) ? texture2D(tMetallicMap,  animUV).r : uMetallic;

    // F0: base reflectance — dielectrics use 0.04, metals tint with albedo
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // Shadows are applied PER LIGHT inside the loop (each shadow-casting
    // spotlight samples its own atlas quadrant), so plain accumulators suffice
    // — a shadowed light only ever attenuates its own contribution.
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
        vec3  spotDir;
        bool  isSpot;
        float cosOuter, cosInner;
        int   shadowSlot;

        if (uUseClusters > 0.5)
        {
            int  fi = lightOffset + n;
            int  li = int(texelFetch(tLightIndices, ivec2(fi & 255, fi >> 8), 0).x);
            vec4 t0 = texelFetch(tLightData, ivec2(li * 4 + 0, 0), 0);
            vec4 t1 = texelFetch(tLightData, ivec2(li * 4 + 1, 0), 0);
            vec4 t2 = texelFetch(tLightData, ivec2(li * 4 + 2, 0), 0);
            vec4 t3 = texelFetch(tLightData, ivec2(li * 4 + 3, 0), 0);
            lightPos    = t0.xyz;  lightRadius = t0.w;
            lightColor  = t1.xyz;  cosInner    = t1.w;
            spotDir     = t2.xyz;  cosOuter    = t2.w;
            isSpot      = (cosOuter > -1.5);   // -2 marks a point light
            shadowSlot  = int(t3.x);           // -1 = casts no shadow
        }
        else
        {
            lightPos    = uLightPos[n];
            lightColor  = uLightColor[n].rgb;
            lightRadius = uLightRadius[n];
            spotDir     = uLightDir[n];
            isSpot      = uLightIsSpot[n] > 0.5;
            cosOuter    = uLightCosOuter[n];
            cosInner    = uLightCosInner[n];
            shadowSlot  = -1;   // fallback path carries no shadow data
        }

        vec3  toLight = lightPos - vViewPos;
        float dist    = length(toLight);
        vec3  L       = toLight / dist;

        // Same quadratic attenuation as before
        float atten = clamp(1.0 - (dist * dist) / (lightRadius * lightRadius), 0.0, 1.0);
        if (atten <= 0.0) continue;  // fragment outside this light's radius — skip diffuse + GGX

        // Spotlight cone attenuation — zero outside the outer cone, smooth falloff
        // between outer and inner cone, full brightness inside the inner cone.
        // dot(-L, spotDir): -L points from light toward fragment; spotDir is the cone axis.
        if (isSpot)
        {
            float cosAngle = dot(-L, spotDir);
            atten *= smoothstep(cosOuter, cosInner, cosAngle);
            if (atten <= 0.0) continue;
        }

        float NdotL = max(dot(N, L), 0.0);

        vec3 thisDiffuse  = lightColor * NdotL * atten;
        vec3 thisSpecular = vec3(0.0);

        // --- Specular (GGX Cook-Torrance) ---
        if (roughness < 0.99 && NdotL > 0.0)
        {
            vec3  H     = normalize(L + V);
            float NdotH = max(dot(N, H), 0.0);
            float VdotH = max(dot(V, H), 0.0);

            float r   = max(roughness, 0.025); // prevent singularity at r=0
            float D   = D_GGX(NdotH, r);
            float G   = G_SmithSchlick(NdotV, max(NdotL, 0.0001), r);
            vec3  F   = F_Schlick(VdotH, F0);

            vec3 spec = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);
            thisSpecular = spec * lightColor * NdotL * atten;
        }

        // Per-light shadow — sample this light's atlas quadrant. Only applied
        // to this light's direct contribution; ambient/baked light and other
        // lights are unaffected.
        if (shadowSlot >= 0 && uShadowRect[shadowSlot].w > 0.5)
        {
            float sf = shadowFactorPCF(vShadowClip[shadowSlot], uShadowRect[shadowSlot]);
            thisDiffuse  *= sf;
            thisSpecular *= sf;
        }

        diffuseAccum  += thisDiffuse;
        specularAccum += thisSpecular;
    }

    // Metallic surfaces have no diffuse — suppress it proportionally
    float diffuseFactor = 1.0 - metallic;

    // Baked lightmap replaces the ambient floor when present.
    // Dynamic lights still add on top, so explosions / flashlights remain visible
    // on lightmapped surfaces.
    vec3 bakedLight = (uHasLightmap > 0.5)
        ? pow(texture2D(tLightmap, vLightmapUV).rgb, vec3(2.2)) + uAmbientColor
        : uAmbientColor;

    // --- Environment map reflection (ambient specular) ---
    // Samples the equirectangular env map using the world-space reflection vector.
    // Only runs when an env map is bound and the surface has any glossiness.
    if (uHasEnvMap > 0.5 && roughness < 0.99)
    {
        // Reflect in view space — N and V are already view-space and correct for
        // any object transform (gl_NormalMatrix handles rotation + scale properly).
        vec3 R_view = reflect(-V, N);

        // Convert view-space reflection to world-space using explicit camera basis
        // vectors. View-space axes map to world as:
        //   X+ = uCamRight,    Y+ = uCamUp,    Z+ = -uCamForward (out of screen)
        // Building mat3 with these as columns gives the view→world rotation.
        // Irrlicht world space is left-handed (Z+ into screen), so view Z+ = world forward.
        // No negation on forward — the column for view Z+ is +uCamForward.
        vec3 R_world = mat3(uCamRight, uCamUp, uCamForward) * R_view;

        // Equirectangular UV from spherical coords
        vec2 envUV = vec2(
            atan(R_world.x, R_world.z) / (2.0 * PI),
            asin(clamp(-R_world.y, -1.0, 1.0)) / PI + 0.5
        );
        vec3 envSample = texture2D(tEnvMap, envUV).rgb;

        // Fresnel at current view angle — naturally handles metallic tinting via F0
        // (metals have F0=albedo, dielectrics have F0=0.04)
        vec3  envF        = F_Schlick(NdotV, F0);
        float roughFade   = (1.0 - roughness) * (1.0 - roughness);
        // Gate env map by scene ambient — fully off at true black, fully on above ~#0D0D0D.
        // Linear scale was too aggressive (12% at #1E1E1E); smoothstep gives a clean on/off.
        float ambientLum = dot(uAmbientColor, vec3(0.2126, 0.7152, 0.0722));
        specularAccum += envSample * envF * roughFade * smoothstep(0.0, 0.05, ambientLum);
    }

    vec3 color = albedo * max(bakedLight, diffuseAccum) * diffuseFactor
               + specularAccum;

    // Fresnel rim — view-angle glow for glassy/crystal surfaces.
    // When uFresnelStrength == 0 (default) this adds vec3(0) and costs nothing.
    float fresnel = pow(clamp(1.0 - NdotV, 0.0, 1.0), max(uFresnelPower, 0.001));
    color += vec3(fresnel * uFresnelStrength);

    // Emission — adds self-illumination after all lighting, before fog.
    if (uHasEmissionMap > 0.5)
        color += pow(texture2D(tEmissionMap, animUV).rgb, vec3(2.2));

    float fogDist   = length(vViewPos) - uFogStart;
    float fogFactor = 1.0 - exp(-uFogDensity * max(fogDist, 0.0));
    fogFactor       = clamp(fogFactor, 0.0, 1.0);
    gl_FragColor    = vec4(mix(color, uFogColor, fogFactor), texColor.a * uAlpha);
}
