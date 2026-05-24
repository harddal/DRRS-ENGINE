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

uniform sampler2D tShadowMap;   // slot 3 — directional shadow map (R = depth in [0,1])
uniform float     uHasShadow;   // 1.0 when a shadow map is bound for this frame
uniform float     uShadowBias;  // depth bias to prevent self-shadowing acne

varying vec4 vShadowClip;       // light clip-space coords from vertex shader (pre-divide)

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

// PBR material params (mapped from SMaterial in C++)
//   uRoughness = 1 - sqrt(Shininess/128)   -- 0=mirror, 1=matte
//   uMetallic  = SpecularColor.alpha/255    -- 0=plastic, 1=metal
uniform float uRoughness;
uniform float uMetallic;
uniform vec3  uAmbientColor;

uniform vec3  uFogColor;
uniform float uFogDensity;
uniform float uFogStart;

uniform float uFresnelStrength; // 0.0 = disabled (default); >0 adds view-angle rim glow
uniform float uFresnelPower;    // Schlick exponent (default 4.0)
uniform float uAlpha;           // per-material opacity multiplier (1.0 = fully opaque)

// UV animation — driven by MaterialTypeParams[4..7].  All zero = static (no cost).
uniform float uTime;          // seconds
uniform vec2  uTexScroll;     // UV offset per second; (0,0) = static
uniform float uTexWarpAmp;    // sine warp amplitude in UV units; 0 = no warp
uniform float uTexWarpSpeed;  // warp oscillation speed (radians/second)

// Environment map (equirectangular) for ambient specular reflections
uniform sampler2D tEnvMap;
uniform float     uHasEnvMap;   // 1.0 when an env map is bound, else 0.0
// Camera world-space basis vectors — used to convert the view-space reflection
// vector to world space without any matrix convention ambiguity.
// In OpenGL view space: X+=right, Y+=up, Z+=out-of-screen (-forward).
uniform vec3 uCamRight;    // world-space direction for view-space X+
uniform vec3 uCamUp;       // world-space direction for view-space Y+
uniform vec3 uCamForward;  // world-space direction camera is pointing (into screen)

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

    // Separate accumulators for shadow-casting spotlight vs. all other lights.
    // shadowFactor must only attenuate the spotlight's contribution — applying it
    // to the full diffuseAccum would darken surfaces lit by non-shadow point lights
    // that merely happen to fall inside the spotlight's frustum.
    vec3 diffuseAccum  = vec3(0.0);   // point lights (unshadowed)
    vec3 specularAccum = vec3(0.0);   // point lights specular (unshadowed)
    vec3 spotDiffuse   = vec3(0.0);   // shadow-casting spotlight diffuse
    vec3 spotSpecular  = vec3(0.0);   // shadow-casting spotlight specular

    for (int i = 0; i < MAX_LIGHTS; i++)
    {
        if (float(i) >= uLightCount)
            break;

        vec3  toLight = uLightPos[i] - vViewPos;
        float dist    = length(toLight);
        vec3  L       = toLight / dist;

        // Same quadratic attenuation as before
        float atten = clamp(1.0 - (dist * dist) / (uLightRadius[i] * uLightRadius[i]), 0.0, 1.0);
        if (atten <= 0.0) continue;  // fragment outside this light's radius — skip diffuse + GGX

        // Spotlight cone attenuation — zero outside the outer cone, smooth falloff
        // between outer and inner cone, full brightness inside the inner cone.
        // dot(-L, spotDir): -L points from light toward fragment; spotDir is the cone axis.
        bool isSpot = uLightIsSpot[i] > 0.5;
        if (isSpot)
        {
            float cosAngle = dot(-L, uLightDir[i]);
            atten *= smoothstep(uLightCosOuter[i], uLightCosInner[i], cosAngle);
            if (atten <= 0.0) continue;
        }

        float NdotL = max(dot(N, L), 0.0);

        vec3 thisDiffuse  = uLightColor[i].rgb * NdotL * atten;
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
            thisSpecular = spec * uLightColor[i].rgb * NdotL * atten;
        }

        // Route into shadow-caster bucket (first spotlight) or regular bucket.
        if (isSpot)
        {
            spotDiffuse  += thisDiffuse;
            spotSpecular += thisSpecular;
        }
        else
        {
            diffuseAccum  += thisDiffuse;
            specularAccum += thisSpecular;
        }
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

    // 3x3 PCF shadow — only applied to direct diffuse, not ambient/baked floor.
    // Perspective divide is done here (not in the vertex shader) so that OpenGL's
    // perspective-correct varying interpolation does not introduce a second division,
    // which would cause shadow offsets that grow as the light gets closer.
    float shadowFactor = 1.0;
    if (uHasShadow > 0.5)
    {
        vec3 shadowCoord = vShadowClip.xyz / vShadowClip.w;
        shadowCoord      = shadowCoord * 0.5 + 0.5;

        bool inFrustum = shadowCoord.x >= 0.0 && shadowCoord.x <= 1.0 &&
                         shadowCoord.y >= 0.0 && shadowCoord.y <= 1.0 &&
                         shadowCoord.z >= 0.0 && shadowCoord.z <= 1.0;
        if (inFrustum)
        {
            // Slope-scale bias: the constant bias handles precision differences; the
            // slope term (derivative of shadow depth across the screen) automatically
            // increases bias for surfaces facing the light at a shallow angle, which
            // eliminates the triangular acne pattern without increasing Peter Panning.
            float slope = length(vec2(dFdx(shadowCoord.z), dFdy(shadowCoord.z)));
            float bias  = uShadowBias + slope * 3.0;

            vec2 texelSize = vec2(1.0 / 2048.0);
            float shadowSum = 0.0;
            for (int sx = -1; sx <= 1; ++sx)
                for (int sy = -1; sy <= 1; ++sy)
                {
                    float closestDepth = texture2D(tShadowMap, shadowCoord.xy + vec2(sx, sy) * texelSize).r;
                    shadowSum += (shadowCoord.z - bias > closestDepth) ? 1.0 : 0.0;
                }
            // 0.7 = max shadow darkness; ambient still reaches shadowed areas
            shadowFactor = 1.0 - (shadowSum / 9.0) * 0.7;
        }
    }

    // Merge: point-light diffuse is never shadowed; spotlight diffuse + specular are.
    diffuseAccum  += spotDiffuse  * shadowFactor;
    specularAccum += spotSpecular * shadowFactor;

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
