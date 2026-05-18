#include "Engine/Renderer/RenderManager.h"

#include <chrono>
#include <fstream>
#include <algorithm>

#include <cereal/archives/xml.hpp>
#include <IMGUI/imgui.h>
#include <spdlog/spdlog.h>

#include "Engine/Engine.h"
#include "Utility/Utility.h"

#include "Extensions/CUnrealMeshFileLoader.h"
#include "Extensions/CBloodShader.h"

#include <IMGUI/backends/imgui_impl_opengl3.h>


#include <GL/gl.h>

#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#define QUAD_COLOR_MODE irr::video::ECF_A16B16G16R16F

using namespace std;
using namespace irr;
using namespace core;
using namespace scene;
using namespace video;

RenderManager* RenderManager::s_Instance = nullptr;
irr::gui::IGUIFont* g_DefaultTextRenderableFontSm;

void Set_IMGUI_Default_Theme()
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.898f, 0.850f, 0.858f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImVec4(0.498f, 0.450f, 0.458f, 1.0f));
	
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.5f, 0.5f, 0.5f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
	
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.2f, 0.3f, 0.6f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.2f, 0.3f, 0.6f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.2f, 0.3f, 0.6f, 1.0f));
	
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.5f, 0.5f, 0.5f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, ImVec4(0.5f, 0.5f, 0.5f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.2f, 0.3f, 0.6f, 1.0f));
	
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.13f, 0.4f, 0.8f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.13f, 0.4f, 0.8f, 1.0f));
	
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.13f, 0.4f, 0.8f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.13f, 0.4f, 0.8f, 1.0f));
	
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

    ImGui::GetStyle().WindowRounding = 0.0f;
    ImGui::GetStyle().ChildRounding = 0.0f;
    ImGui::GetStyle().FrameRounding = 0.0f;
    ImGui::GetStyle().GrabRounding = 0.0f;
    ImGui::GetStyle().ScrollbarRounding = 0.0f;
    ImGui::GetStyle().DockingSeparatorSize = 0.0f;
}

void WaterShaderCallback::OnSetConstants(IMaterialRendererServices* services, s32 userData)
{
    const auto driver = services->getVideoDriver();

    // Time in seconds — getCurrentTime() returns milliseconds.
    auto time = static_cast<f32>(Engine::Get()->getCurrentTime()) / 1000.0f;
    services->setVertexShaderConstant("fTime", &time, 1);
    services->setPixelShaderConstant("fTime",  &time, 1);

    // World matrix (transposed for GLSL column-major) — vertex shader uses it
    // to convert object-space positions into world-space XZ tile coordinates.
    auto world  = driver->getTransform(ETS_WORLD);
    auto worldT = world.getTransposed();
    services->setVertexShaderConstant("mWorld", worldT.pointer(), 16);

    // Texture sampler bindings.
    int slot0 = 0, slot1 = 1;
    services->setPixelShaderConstant("tDiffuse", &slot0, 1);
    services->setPixelShaderConstant("tNormal",  &slot1, 1);

    float hasNormal = (m_currentMaterial.TextureLayer[1].Texture != nullptr) ? 1.0f : 0.0f;
    services->setPixelShaderConstant("uHasNormal", &hasNormal, 1);

    // Shallow color encoded in AmbientColor by RenderSystem.
    auto ambient = m_currentMaterial.AmbientColor;
    float shallowColor[3] = {
        ambient.getRed()   / 255.f,
        ambient.getGreen() / 255.f,
        ambient.getBlue()  / 255.f
    };
    services->setPixelShaderConstant("uShallowColor", shallowColor, 3);

    // Deep color + alpha encoded in DiffuseColor by RenderSystem.
    auto diffuse = m_currentMaterial.DiffuseColor;
    float deepColor[3] = {
        diffuse.getRed()   / 255.f,
        diffuse.getGreen() / 255.f,
        diffuse.getBlue()  / 255.f
    };
    float alpha = diffuse.getAlpha() / 255.f;
    services->setPixelShaderConstant("uDeepColor", deepColor, 3);
    services->setPixelShaderConstant("uAlpha",     &alpha,    1);
}


void ShaderMaterialManager::add(ShaderMaterial material)
{
    s_ShaderMaterialList.push_back(material);
}

E_MATERIAL_TYPE ShaderMaterialManager::get(std::string name)
{
    for (auto m : s_ShaderMaterialList) {
        if (m.name == name) {
            return static_cast<E_MATERIAL_TYPE>(m.material);
        }
    }

    spdlog::warn("ShaderMaterialManager: Shader material {} invalid", name);

    return EMT_SOLID;
}

vector<ShaderMaterial> ShaderMaterialManager::s_ShaderMaterialList;

bool ShaderMaterialManager::isRefraction(irr::s32 materialType)
{
    for (const auto& m : s_ShaderMaterialList)
        if (m.material == materialType && m.isRefraction)
            return true;
    return false;
}

void ShaderConstantSetCallBack::OnSetConstants(IMaterialRendererServices* services, s32 userData)
{
    const auto driver = services->getVideoDriver();

    auto time = static_cast<f32>(Engine::Get()->getCurrentTime());
    services->setVertexShaderConstant("fTime", &time, 1);

    auto resolution = vector2df(static_cast<irr::f32>(RenderManager::Get()->driver()->getScreenSize().Width), static_cast<irr::f32>(RenderManager::Get()->driver()->getScreenSize().Height));
    services->setVertexShaderConstant("iResolution", reinterpret_cast<f32*>(&resolution), 2);

    auto invWorld = driver->getTransform(ETS_WORLD);
    invWorld.makeInverse();

    services->setVertexShaderConstant("mInvWorld", invWorld.pointer(), 16);

    auto worldViewProj = driver->getTransform(ETS_PROJECTION);
    worldViewProj *= driver->getTransform(ETS_VIEW);
    worldViewProj *= driver->getTransform(ETS_WORLD);

    services->setVertexShaderConstant("mWorldViewProj", worldViewProj.pointer(), 16);

    auto world = driver->getTransform(ETS_WORLD);
    world = world.getTransposed();

    services->setVertexShaderConstant("mTransWorld", world.pointer(), 16);

    // Combined light MVP per object = lightProj * lightView * world.
    // Mirrors exactly what gl_ModelViewProjectionMatrix is in the shadow depth pass,
    // so depth values are numerically identical and self-shadowing acne is eliminated.
    auto rm = RenderManager::Get();
    auto lightMVP = rm->getLightProjView();
    lightMVP *= driver->getTransform(ETS_WORLD);
    services->setVertexShaderConstant("mLightMVP", lightMVP.pointer(), 16);

    auto pos = RenderManager::Get()->sceneManager()->getActiveCamera()->getAbsolutePosition();

    services->setVertexShaderConstant("fCameraPos", reinterpret_cast<f32*>(&pos), 3);

    auto textureLayerID = 0;
    services->setPixelShaderConstant("tDiffuse", &textureLayerID, 1);

    // Lightmap texture lives in material slot 1 (set by LightmapBaker::bakeScene).
    // Bind sampler and inform the shader whether a baked lightmap is present.
    int  lightmapLayerID = 1;
    float hasLightmap = (m_currentMaterial.TextureLayer[1].Texture != nullptr) ? 1.0f : 0.0f;
    services->setPixelShaderConstant("tLightmap",    &lightmapLayerID, 1);
    services->setPixelShaderConstant("uHasLightmap", &hasLightmap,     1);

    // Shadow map — slot 3.  Bind the RTT and inform the shader whether it is active.
    {
        int shadowSlot = 3;
        services->setPixelShaderConstant("tShadowMap", &shadowSlot, 1);

        auto* rmShadow = RenderManager::Get();
        float hasShadow = (rmShadow->hasShadowLight() && rmShadow->getShadowMapRTT()) ? 1.0f : 0.0f;
        services->setPixelShaderConstant("uHasShadow", &hasShadow, 1);

        float shadowBias = rmShadow->getShadowBias();
        services->setPixelShaderConstant("uShadowBias", &shadowBias, 1);

        // Bind the shadow map texture to slot 3 by mutating a copy of the current
        // material — same pattern used for the env map on slot 2.
        if (hasShadow > 0.5f)
        {
            irr::video::SMaterial mat = m_currentMaterial;
            mat.TextureLayer[3].Texture = rmShadow->getShadowMapRTT();
            driver->setMaterial(mat);
        }
    }

    // -------------------------------------------------------------------------
    // Per-pixel Blinn-Phong: upload up to MAX_LIGHTS closest dynamic lights
    // -------------------------------------------------------------------------
    static constexpr int MAX_LIGHTS = 8;

    // Object world-space position comes from the current WORLD transform origin
    vector3df objectPos = driver->getTransform(ETS_WORLD).getTranslation();

    auto lights = RenderManager::gatherClosestLights(driver, RenderManager::Get()->sceneManager(), objectPos, MAX_LIGHTS);

    // Build flat arrays for GLSL uniform upload
    float lightPos     [MAX_LIGHTS * 3] = {};
    float lightColor   [MAX_LIGHTS * 4] = {};
    float lightRadius  [MAX_LIGHTS]     = {};
    float lightDir     [MAX_LIGHTS * 3] = {};
    float lightIsSpot  [MAX_LIGHTS]     = {};
    float lightCosOuter[MAX_LIGHTS]     = {};
    float lightCosInner[MAX_LIGHTS]     = {};
    int   lightCount = static_cast<int>(lights.size());

    for (int i = 0; i < lightCount; ++i)
    {
        lightPos[i * 3 + 0] = lights[i].pos.X;
        lightPos[i * 3 + 1] = lights[i].pos.Y;
        lightPos[i * 3 + 2] = lights[i].pos.Z;

        lightColor[i * 4 + 0] = lights[i].color.r;
        lightColor[i * 4 + 1] = lights[i].color.g;
        lightColor[i * 4 + 2] = lights[i].color.b;
        lightColor[i * 4 + 3] = 1.0f;

        lightRadius[i] = lights[i].radius;

        lightIsSpot[i]   = lights[i].isSpot ? 1.0f : 0.0f;
        lightDir[i*3+0]  = lights[i].direction.X;
        lightDir[i*3+1]  = lights[i].direction.Y;
        lightDir[i*3+2]  = lights[i].direction.Z;
        lightCosOuter[i] = lights[i].cosOuter;
        lightCosInner[i] = lights[i].cosInner;
    }

    // Irrlicht's OpenGL/GLSL backend requires array uniforms to be addressed
    // with "[0]" in the name; plain "uLightPos" silently fails to bind.
    // Always upload the full MAX_LIGHTS elements so the bind succeeds even
    // when fewer lights are active (unused elements are zero-initialised above).
    services->setPixelShaderConstant("uLightPos[0]",      lightPos,      MAX_LIGHTS * 3);
    services->setPixelShaderConstant("uLightColor[0]",    lightColor,    MAX_LIGHTS * 4);
    services->setPixelShaderConstant("uLightRadius[0]",   lightRadius,   MAX_LIGHTS);
    services->setPixelShaderConstant("uLightDir[0]",      lightDir,      MAX_LIGHTS * 3);
    services->setPixelShaderConstant("uLightIsSpot[0]",   lightIsSpot,   MAX_LIGHTS);
    services->setPixelShaderConstant("uLightCosOuter[0]", lightCosOuter, MAX_LIGHTS);
    services->setPixelShaderConstant("uLightCosInner[0]", lightCosInner, MAX_LIGHTS);
    float fLightCount = static_cast<float>(lightCount);
    services->setPixelShaderConstant("uLightCount", &fLightCount, 1);

    // PBR material params — derived from Irrlicht SMaterial fields so existing
    // mesh setup code needs no changes.  Control per-mesh via:
    //   node->getMaterial(i).Shininess        = 64.f;  // maps to roughness (0=matte → 128+=shiny)
    //   node->getMaterial(i).SpecularColor.setAlpha(200); // maps to metallic (0=dielectric, 255=metal)
    //
    // Roughness: convert Shininess (0..∞) → roughness (1..0)
    //   shininess=0   → roughness=1.0 (fully matte / diffuse-only)
    //   shininess=32  → roughness≈0.50 (semi-gloss)
    //   shininess=128 → roughness≈0.13 (fairly mirror-like)
    const irr::video::SMaterial& mat = m_currentMaterial;
    float roughness = 1.0f - sqrtf(irr::core::clamp(mat.Shininess, 0.0f, 128.0f) / 128.0f);
    float metallic  = static_cast<float>(mat.SpecularColor.getAlpha()) / 255.0f;
    auto ambLight = RenderManager::Get()->sceneManager()->getAmbientLight();
    float ambientColor[3] = { ambLight.getRed(), ambLight.getGreen(), ambLight.getBlue() };
    services->setPixelShaderConstant("uRoughness",    &roughness,    1);
    services->setPixelShaderConstant("uMetallic",     &metallic,     1);
    services->setPixelShaderConstant("uAmbientColor", ambientColor,  3);

    services->setPixelShaderConstant("uFogColor",   fogColor,    3);
    services->setPixelShaderConstant("uFogDensity", &fogDensity, 1);
    services->setPixelShaderConstant("uFogStart",   &fogStart,   1);

    // --- Environment map ---
    // Bind the equirectangular env map to slot 2 and tell the shader whether it's present.
    int   envMapSlot = 2;
    float hasEnvMap  = (m_envMap != nullptr) ? 1.0f : 0.0f;
    services->setPixelShaderConstant("tEnvMap",    &envMapSlot, 1);
    services->setPixelShaderConstant("uHasEnvMap", &hasEnvMap,  1);
    if (m_envMap)
    {
        // IVideoDriver has no public setTexture(). The Irrlicht-idiomatic way to bind
        // an extra texture to a unit is to add it to a material and call setMaterial().
        // Irrlicht will then bind each TextureLayer to the corresponding GL texture unit.
        // We copy the current material so nothing else changes; the next node's draw call
        // will replace it with its own material as normal.
        irr::video::SMaterial mat = m_currentMaterial;
        mat.TextureLayer[2].Texture = m_envMap;
        driver->setMaterial(mat);
    }

    // Camera world-space basis vectors for view→world reflection conversion.
    // Irrlicht world space is LEFT-HANDED (X+right, Y+up, Z+into screen).
    // In left-handed space, right = worldUp × forward (reversed from right-handed).
    {
        auto* cam = RenderManager::Get()->sceneManager()->getActiveCamera();
        irr::core::vector3df forward = (cam->getTarget() - cam->getAbsolutePosition()).normalize();
        irr::core::vector3df right   = cam->getUpVector().crossProduct(forward).normalize();
        irr::core::vector3df up      = forward.crossProduct(right).normalize(); // re-orthogonalize

        services->setPixelShaderConstant("uCamRight",   reinterpret_cast<f32*>(&right),   3);
        services->setPixelShaderConstant("uCamUp",      reinterpret_cast<f32*>(&up),      3);
        services->setPixelShaderConstant("uCamForward", reinterpret_cast<f32*>(&forward), 3);
    }

    // Fresnel rim + per-material alpha — read from MaterialTypeParams slots [2]/[3]
    // and DiffuseColor.alpha so crystal/glassy props need no separate shader.
    // Non-crystal materials leave these at 0/0/255 so defaults are harmless.
    float fresnelStrength = m_currentMaterial.MaterialTypeParams[2];
    float fresnelPower    = m_currentMaterial.MaterialTypeParams[3] > 0.0f
                          ? m_currentMaterial.MaterialTypeParams[3] : 4.0f;
    float matAlpha        = static_cast<float>(m_currentMaterial.DiffuseColor.getAlpha()) / 255.0f;
    services->setPixelShaderConstant("uFresnelStrength", &fresnelStrength, 1);
    services->setPixelShaderConstant("uFresnelPower",    &fresnelPower,    1);
    services->setPixelShaderConstant("uAlpha",           &matAlpha,        1);

    // UV scroll + warp — driven by MaterialTypeParams[4..7].
    float uTimeSec    = static_cast<float>(Engine::Get()->getCurrentTime()) / 1000.0f;
    float uvScroll[2] = { m_currentMaterial.MaterialTypeParams[4],
                          m_currentMaterial.MaterialTypeParams[5] };
    float warpAmp     = m_currentMaterial.MaterialTypeParams[6];
    float warpSpeed   = m_currentMaterial.MaterialTypeParams[7];
    services->setPixelShaderConstant("uTime",         &uTimeSec,  1);
    services->setPixelShaderConstant("uTexScroll",    uvScroll,   2);
    services->setPixelShaderConstant("uTexWarpAmp",   &warpAmp,   1);
    services->setPixelShaderConstant("uTexWarpSpeed", &warpSpeed, 1);
}

void TerrainShaderCallback::OnSetConstants(IMaterialRendererServices* services, s32 userData)
{
    // Upload all base PBR + lighting uniforms (tDiffuse=0, tLightmap=1, lights, env map suppressed).
    ShaderConstantSetCallBack::OnSetConstants(services, userData);

    // Suppress env map — slot 2 is the splat map, not an env map.
    float hasEnvMap = 0.0f;
    services->setPixelShaderConstant("uHasEnvMap", &hasEnvMap, 1);

    // Tell the shader which texture unit each splat/detail sampler lives on.
    // The textures themselves are pre-bound by PropManager::enablePainting via
    // node->setMaterialTexture(), so Irrlicht binds them before this callback runs.
    int slot2 = 2, slot3 = 3, slot4 = 4, slot5 = 5, slot6 = 6;
    services->setPixelShaderConstant("tSplatMap", &slot2, 1);
    services->setPixelShaderConstant("tDetailA",  &slot3, 1);
    services->setPixelShaderConstant("tDetailB",  &slot4, 1);
    services->setPixelShaderConstant("tDetailC",  &slot5, 1);
    services->setPixelShaderConstant("tDetailD",  &slot6, 1);

    services->setPixelShaderConstant("uDetailTiling", tiling, 4);
}

void BloomBrightCallback::OnSetConstants(IMaterialRendererServices* services, s32)
{
    int slot = 0;
    services->setPixelShaderConstant("tScene",     &slot,      1);
    services->setPixelShaderConstant("uThreshold", &threshold, 1);
}

void BloomBlurCallback::OnSetConstants(IMaterialRendererServices* services, s32)
{
    int slot = 0;
    services->setPixelShaderConstant("tBloom",     &slot,      1);
    services->setPixelShaderConstant("uDirection", direction,  2);

    auto sz = RenderManager::Get()->driver()->getScreenSize();
    float rcpFrame[2] = {
        1.0f / static_cast<float>(sz.Width),
        1.0f / static_cast<float>(sz.Height)
    };
    services->setPixelShaderConstant("uRcpFrame", rcpFrame, 2);
}

void BloomCompositeCallback::OnSetConstants(IMaterialRendererServices* services, s32)
{
    int slotBloom = 0, slotScene = 1;
    services->setPixelShaderConstant("tBloom",         &slotBloom,  1);
    services->setPixelShaderConstant("tScene",         &slotScene,  1);
    services->setPixelShaderConstant("uBloomStrength", &strength,   1);
}

void TonemapCallback::OnSetConstants(IMaterialRendererServices* services, s32)
{
    int slot = 0;
    services->setPixelShaderConstant("tScene",      &slot,       1);
    services->setPixelShaderConstant("uExposure",   &exposure,   1);
    services->setPixelShaderConstant("uWhitePoint", &whitePoint, 1);
}

void SharpenCallback::OnSetConstants(IMaterialRendererServices* services, s32)
{
    int slot = 0;
    services->setPixelShaderConstant("tScene",    &slot,    1);
    services->setPixelShaderConstant("uStrength", &strength, 1);

    auto sz = RenderManager::Get()->driver()->getScreenSize();
    float rcpFrame[2] = {
        1.0f / static_cast<float>(sz.Width),
        1.0f / static_cast<float>(sz.Height)
    };
    services->setPixelShaderConstant("uRcpFrame", rcpFrame, 2);
}

void PixelateCallback::OnSetConstants(IMaterialRendererServices* services, s32)
{
    int slot = 0;
    services->setPixelShaderConstant("tScene",     &slot,     1);
    services->setPixelShaderConstant("uPixelSize", &pixelSize, 1);

    auto sz = RenderManager::Get()->driver()->getScreenSize();
    float rcpFrame[2] = {
        1.0f / static_cast<float>(sz.Width),
        1.0f / static_cast<float>(sz.Height)
    };
    services->setPixelShaderConstant("uRcpFrame", rcpFrame, 2);
}

void ColorGradeCallback::OnSetConstants(IMaterialRendererServices* services, s32)
{
    int slot = 0;
    services->setPixelShaderConstant("tScene",       &slot,      1);
    services->setPixelShaderConstant("uSaturation",  &saturation, 1);
    services->setPixelShaderConstant("uBrightness",  &brightness, 1);
    services->setPixelShaderConstant("uColorTint",   colorTint,  3);
}

void PosterizeCallback::OnSetConstants(IMaterialRendererServices* services, s32)
{
    int slot = 0;
    services->setPixelShaderConstant("tScene",    &slot,     1);
    services->setPixelShaderConstant("uLevels",   &levels,   1);
    services->setPixelShaderConstant("uStrength", &strength, 1);
}

void FilmGrainCallback::OnSetConstants(IMaterialRendererServices* services, s32)
{
    int slot = 0;
    float t  = static_cast<float>(Engine::Get()->getCurrentTime()) / 1000.0f;
    services->setPixelShaderConstant("tScene",         &slot,     1);
    services->setPixelShaderConstant("uGrainStrength", &strength, 1);
    services->setPixelShaderConstant("uTime",          &t,        1);
}

void FXAAShaderCallback::OnSetConstants(IMaterialRendererServices* services, s32 userData)
{
    int slot = 0;
    services->setPixelShaderConstant("tScene", &slot, 1);

    auto sz = RenderManager::Get()->driver()->getScreenSize();
    float rcpFrame[2] = {
        1.0f / static_cast<float>(sz.Width),
        1.0f / static_cast<float>(sz.Height)
    };
    services->setPixelShaderConstant("uRcpFrame", rcpFrame, 2);
}

void RenderManager::recreatePostProcessRTTs(irr::u32 w, irr::u32 h)
{
    if (m_sceneRTT)      { m_driver->removeTexture(m_sceneRTT);      m_sceneRTT      = nullptr; }
    if (m_ppRTT[0])      { m_driver->removeTexture(m_ppRTT[0]);      m_ppRTT[0]      = nullptr; }
    if (m_ppRTT[1])      { m_driver->removeTexture(m_ppRTT[1]);      m_ppRTT[1]      = nullptr; }
    if (m_lumRTT)        { m_driver->removeTexture(m_lumRTT);        m_lumRTT        = nullptr; }

    irr::core::dimension2du sz(w, h);
    m_sceneRTT      = m_driver->addRenderTargetTexture(sz, "pp_scene",     QUAD_COLOR_MODE);
    m_ppRTT[0]      = m_driver->addRenderTargetTexture(sz, "pp_buf0",      QUAD_COLOR_MODE);
    m_ppRTT[1]      = m_driver->addRenderTargetTexture(sz, "pp_buf1",      QUAD_COLOR_MODE);
    m_lumRTT        = m_driver->addRenderTargetTexture(irr::core::dimension2du(1, 1), "pp_lum", QUAD_COLOR_MODE);

    if (!m_sceneRTT || !m_ppRTT[0] || !m_ppRTT[1] || !m_lumRTT)
        spdlog::error("RenderManager: failed to create post-process RTTs ({}x{})", w, h);

    // Load FBO extension functions for depth blitting (only once — they don't change).
    if (!m_fnBlitFramebuffer)
    {
        m_fnBindFramebuffer = (void*)wglGetProcAddress("glBindFramebuffer");
        m_fnBlitFramebuffer = (void*)wglGetProcAddress("glBlitFramebuffer");
        if (!m_fnBlitFramebuffer)
            spdlog::warn("RenderManager: glBlitFramebuffer unavailable — LDR effect depth testing disabled");
    }
}

void RenderManager::createShadowResources()
{
    constexpr irr::u32 SHADOW_RES = 2048;
    if (m_shadowMapRTT)
        m_driver->removeTexture(m_shadowMapRTT);

    m_shadowMapRTT = m_driver->addRenderTargetTexture(
        irr::core::dimension2du(SHADOW_RES, SHADOW_RES),
        "shadow_map",
        irr::video::ECF_R32F);  // 32-bit float single channel — far better precision than RGBA16F

    if (!m_shadowMapRTT)
        spdlog::error("RenderManager: failed to create shadow map RTT ({}x{})", SHADOW_RES, SHADOW_RES);

    // Pre-create a shadow camera node so we don't allocate one every frame.
    // It lives in the scene but is never the active camera during normal rendering.
    if (!m_shadowCamera)
        m_shadowCamera = m_sceneManager->addCameraSceneNode(nullptr,
            irr::core::vector3df(0, 100, 0),
            irr::core::vector3df(0, 0, 0));
}

void RenderManager::drawShadowPass()
{
    if (m_shadowDepthMat < 0 || !m_shadowMapRTT || !m_shadowCamera)
        return;

    // Find the first spotlight by traversing the scene graph directly rather than
    // reading getDynamicLight(), which is populated during drawAll() and therefore
    // holds last frame's position/direction. Traversing scene nodes and calling
    // updateAbsolutePosition() gives the current-frame world transform, so the
    // shadow camera and the lighting shader both use the same frame's light data.
    bool foundLight = false;
    irr::video::SLight lightData;
    irr::core::array<irr::scene::ISceneNode*> lightNodes;
    m_sceneManager->getSceneNodesFromType(irr::scene::ESNT_LIGHT, lightNodes);
    for (irr::u32 i = 0; i < lightNodes.size(); ++i)
    {
        auto* ln = static_cast<irr::scene::ILightSceneNode*>(lightNodes[i]);
        if (ln->getLightData().Type == irr::video::ELT_SPOT && ln->isVisible())
        {
            // Force the node's absolute transform to reflect the current-frame parent position.
            // The parent (TransformComponent node) was already updated by TransformSystem.
            ln->updateAbsolutePosition();

            lightData          = ln->getLightData();
            lightData.Position = ln->getAbsolutePosition();

            // Recompute direction from current absolute transform — identical to how
            // CLightSceneNode::render() derives it: rotate local (0,0,1) to world space.
            irr::core::vector3df d(0.f, 0.f, 1.f);
            ln->getAbsoluteTransformation().rotateVect(d);
            lightData.Direction = d.normalize();

            foundLight = true;
            break;
        }
    }

    m_hasShadowLight = foundLight;
    if (!foundLight)
        return;

    irr::core::vector3df lightPos = lightData.Position;
    irr::core::vector3df lightDir = lightData.Direction;
    lightDir.normalize();

    const irr::core::vector3df up = (fabsf(lightDir.Y) < 0.99f)
        ? irr::core::vector3df(0.f, 1.f, 0.f)
        : irr::core::vector3df(1.f, 0.f, 0.f);

    // Perspective projection matching the spotlight's cone.
    // OuterCone is the half-angle in degrees — full FOV is OuterCone * 2.
    // Near plane is small; far plane matches the light's attenuation radius.
    const float fovRad   = lightData.OuterCone * 2.0f * irr::core::DEGTORAD;
    const float farPlane = lightData.Radius > 0.0f ? lightData.Radius : 200.0f;
    irr::core::matrix4 lightProj;
    lightProj.buildProjectionMatrixPerspectiveFovLH(fovRad, 1.0f, 0.1f, farPlane);

    // Save active camera so we can restore it after the shadow pass.
    irr::scene::ICameraSceneNode* prevCamera = m_sceneManager->getActiveCamera();

    // Configure the shadow camera and make it active so drawAll() uses its transforms.
    m_shadowCamera->setPosition(lightPos);
    m_shadowCamera->setTarget(lightPos + lightDir);
    m_shadowCamera->setUpVector(up);
    m_shadowCamera->setProjectionMatrix(lightProj, false);  // false = perspective
    m_sceneManager->setActiveCamera(m_shadowCamera);

    // Override all solid geometry with the depth-only shader.
    // Front-face culling renders only back faces — the floor can't shadow itself,
    // and solid objects (barrel, walls) still cast shadows via their back faces.
    irr::video::SOverrideMaterial& om = m_driver->getOverrideMaterial();
    om.Material.MaterialType    = static_cast<irr::video::E_MATERIAL_TYPE>(m_shadowDepthMat);
    om.Material.FrontfaceCulling = true;
    om.Material.BackfaceCulling  = false;
    om.EnableFlags = irr::video::EMF_FRONT_FACE_CULLING | irr::video::EMF_BACK_FACE_CULLING;
    om.EnablePasses = irr::scene::ESNRP_SOLID;
    om.Enabled      = true;

    // Render scene depth from the light's POV into the shadow map RTT.
    m_driver->setRenderTarget(m_shadowMapRTT, true, true, irr::video::SColor(0xFFFFFFFF));
    m_sceneManager->drawAll();

    // Read back the view and projection matrices Irrlicht actually used for this draw.
    // Irrlicht's camera computes its own view matrix internally during drawAll(), so we
    // MUST use these post-draw values for m_lightProjView — building the matrix manually
    // before drawAll() can diverge due to the camera needing an animation tick to settle.
    irr::core::matrix4 usedProj = m_driver->getTransform(irr::video::ETS_PROJECTION);
    irr::core::matrix4 usedView = m_driver->getTransform(irr::video::ETS_VIEW);
    m_lightProjView = usedProj;
    m_lightProjView *= usedView;

    // Restore state.
    m_driver->getOverrideMaterial() = irr::video::SOverrideMaterial();
    m_sceneManager->setActiveCamera(prevCamera);

    // Return to the scene RTT so the main pass continues normally.
    if (m_sceneRTT)
        m_driver->setRenderTarget(m_sceneRTT, false, false);
    else
        m_driver->setRenderTarget(nullptr, false, false);
}

void RenderManager::drawFullscreenQuad()
{
    // Vertices in NDC space [-1,1] with identity transforms set before this call.
    // UV Y is pre-flipped here to compensate for OpenGL RTT origin convention.
    irr::video::S3DVertex verts[4] = {
        { {-1.0f, -1.0f, 0.0f}, {0,0,-1}, irr::video::SColor(255,255,255,255), {0.0f, 0.0f} },
        { { 1.0f, -1.0f, 0.0f}, {0,0,-1}, irr::video::SColor(255,255,255,255), {1.0f, 0.0f} },
        { { 1.0f,  1.0f, 0.0f}, {0,0,-1}, irr::video::SColor(255,255,255,255), {1.0f, 1.0f} },
        { {-1.0f,  1.0f, 0.0f}, {0,0,-1}, irr::video::SColor(255,255,255,255), {0.0f, 1.0f} },
    };
    irr::u16 indices[6] = { 0, 1, 2, 0, 2, 3 };
    m_driver->drawVertexPrimitiveList(verts, 4, indices, 2,
        irr::video::EVT_STANDARD, irr::scene::EPT_TRIANGLES, irr::video::EIT_16BIT);
}


void RenderManager::runPostProcessChain()
{
    if (!m_sceneRTT)
        return;

    // Collect enabled passes
    std::vector<PostProcessPass*> activePasses;
    for (auto& pass : m_postProcessPasses)
        if (pass.enabled)
            activePasses.push_back(&pass);

    if (activePasses.empty())
        return;

    const int N = static_cast<int>(activePasses.size());

    for (int i = 0; i < N; ++i)
    {
        PostProcessPass* pass = activePasses[i];
        const bool isLast = (i == N - 1);

        irr::video::ITexture* src = (i == 0) ? m_sceneRTT : m_ppRTT[(i - 1) % 2];
        irr::video::ITexture* dst = isLast ? nullptr : m_ppRTT[i % 2];

        m_driver->setRenderTarget(dst, isLast ? false : true, false, irr::video::SColor(0));

        // Vertex shader uses gl_Position = gl_Vertex directly (NDC bypass),
        // so no matrix setup is needed. Depth test and lighting must be off
        // so post-process fragments are never rejected by scene depth values.
        irr::video::SMaterial mat;
        mat.MaterialType    = static_cast<irr::video::E_MATERIAL_TYPE>(pass->materialTypeIndex);
        mat.setTexture(0, src);
        if (pass->needsOriginalScene)
            mat.setTexture(1, m_sceneRTT);
        mat.ZBuffer          = 0;
        mat.ZWriteEnable     = false;
        mat.Lighting         = false;
        mat.BackfaceCulling  = false;
        m_driver->setMaterial(mat);

        drawFullscreenQuad();
    }
}

void RenderManager::measureAndAdaptExposure(irr::f32 dt)
{
    // --- Render 8x8 scene sample into the 1x1 luminance RTT ---
    m_driver->setRenderTarget(m_lumRTT, true, false, irr::video::SColor(0));

    irr::video::SMaterial mat;
    mat.MaterialType   = static_cast<irr::video::E_MATERIAL_TYPE>(m_lumMaterial);
    mat.setTexture(0, m_sceneRTT);
    mat.ZBuffer        = 0;
    mat.ZWriteEnable   = false;
    mat.Lighting       = false;
    mat.BackfaceCulling = false;
    m_driver->setMaterial(mat);
    drawFullscreenQuad();

    m_driver->setRenderTarget(nullptr, false, false);

    // --- Read back the single encoded pixel ---
    void* pixels = m_lumRTT->lock(irr::video::ETLM_READ_ONLY);
    if (!pixels)
        return;

    // Stored as RGBA bytes; value is written to all channels so bytes[0] = R is reliable.
    float encoded = static_cast<irr::u8*>(pixels)[0] / 255.0f;
    m_lumRTT->unlock();

    // --- Decode log2-average luminance and compute target exposure ---
    float avgLogLum = encoded * 20.0f - 10.0f;
    float avgLum    = std::pow(2.0f, avgLogLum);

    // key=0.18 maps 18% grey to exposure 1.0
    static constexpr float KEY = 0.18f;
    float target = irr::core::clamp(KEY / std::max(avgLum, 0.0001f), m_minExposure, m_maxExposure);

    // --- Exponential approach — frame-rate independent ---
    float speed = (target > m_adaptedExposure) ? m_adaptSpeedUp : m_adaptSpeedDown;
    m_adaptedExposure += (target - m_adaptedExposure) * (1.0f - std::exp(-speed * dt));

    m_tonemapCallback->exposure = m_adaptedExposure;
}

RenderManager::RenderManager(const std::string& name, const std::string& args) :
	m_drawDebugStatistics(false),
	m_defaultCamera(nullptr),
	m_device(nullptr),
	m_gui(nullptr),
	m_driver(nullptr),
	m_gpu(nullptr),
	m_sceneManager(nullptr),
	m_shaderConstantCallBack(nullptr),
	m_waterCallback(nullptr),
	m_terrainCallback(nullptr),
	m_backgroundColor(255, 37, 37, 37),
	m_assimpLoader(nullptr)
{
    if (s_Instance)
    {
        Utility::Error("Pointer to class \'RenderManager\' is invalid");
    }
    s_Instance = this;

    try
    {
        std::ifstream ifs_render("config/render.xml");
        cereal::XMLInputArchive render_config(ifs_render);

        render_config(m_configuration);
    }
    catch (cereal::Exception& ex)
    {
        spdlog::warn("Failed to load render configuration: {}, default values used", ex.what());

        m_configuration = RenderConfiguration();

        std::ofstream ofs_render("config/render.xml");
        cereal::XMLOutputArchive render_config(ofs_render);

        render_config(m_configuration);
    }

	if (!Utility::GetCmdlOptionExists(args, "editor") || m_configuration.mode >= 0)
	{
		getResolutionFromMode(m_configuration.aspect, m_configuration.mode, m_configuration.width, m_configuration.height);
	}

    m_irrlichtParams.WindowId = nullptr;

	if (!m_configuration.fullscreen &&
		(Utility::GetCmdlOptionExists(args, "editor") || Utility::GetCmdlOptionExists(args, "debug")))
	{
		m_irrlichtParams.WindowSize = dimension2d<u32>(m_configuration.editor_width, m_configuration.editor_height);
	}
	else
	{
		m_irrlichtParams.WindowSize = dimension2d<u32>(m_configuration.width, m_configuration.height);
	}

    m_irrlichtParams.Bits = 32;
    m_irrlichtParams.Fullscreen = m_configuration.fullscreen;
    m_irrlichtParams.Stencilbuffer = false;
    m_irrlichtParams.AntiAlias = 0;
    m_irrlichtParams.Vsync = m_configuration.vSync;
    m_irrlichtParams.EventReceiver = &m_imguiEventReceiver;
    m_irrlichtParams.ZBufferBits = 32;

#ifdef DEBUG
   // m_irrlichtParams.LoggingLevel = ELL_DEBUG;
	m_irrlichtParams.LoggingLevel = ELL_NONE;
#else
	m_irrlichtParams.LoggingLevel = ELL_NONE;
#endif

    m_irrlichtParams.DriverType = EDT_OPENGL;

    spdlog::info("Irrlicht Version {} using API OpenGL", IRRLICHT_SDK_VERSION);
    
    m_device = createDeviceEx(m_irrlichtParams);
    if (!m_device)
    {
        spdlog::error("Failed to create the rendering device");
    }

    m_driver = m_device->getVideoDriver();
    if (!m_driver)
    {
        spdlog::error("Failed to initialize the graphics API");
    }

	// After device creation:
	irr::video::SExposedVideoData vd = m_device->getVideoDriver()->getExposedVideoData();
	HWND hwnd = (HWND)vd.OpenGLWin32.HWnd; // or vd.D3D9.HWnd if using D3D9
	BOOL dark = TRUE;
	DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

	//m_driver->setAllowZWriteOnTransparent(true);

    m_sceneManager = m_device->getSceneManager();
    if (!m_sceneManager)
    {
        spdlog::error("Failed to create the scene manager");
    }

    m_gpu = m_driver->getGPUProgrammingServices();
    if (!m_gpu)
    {
        spdlog::error("Failed to register GPU programming services");
    }

    m_shaderConstantCallBack = new ShaderConstantSetCallBack;
    if (!m_shaderConstantCallBack)
    {
        spdlog::error("Failed to create the ShaderConstantSetCallBack interface");
    }

    m_barrelHeatCallback = new BarrelHeatShaderCallback;
    m_terrainCallback    = new TerrainShaderCallback;

    m_waterCallback = new WaterShaderCallback;
    if (!m_waterCallback)
    {
        spdlog::error("Failed to create the WaterShaderCallback interface");
    }

    m_gui = m_device->getGUIEnvironment();
    if (!m_gui)
    {
        spdlog::error("Failed to create the GUI renderer");
    }

	if (!m_configuration.fullscreen && 
		Utility::GetCmdlOptionExists(args, "editor"))
	{
		m_device->maximizeWindow();
		m_device->setResizable(true);
	}

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = "config/imgui.ini";

    float scale          = static_cast<float>(m_configuration.dpi_scale);
    float uiFontSize     = std::round(15.0f * scale);
    float editorFontSize = std::round(16.0f * scale);

    ImFontConfig fontCfg;
    fontCfg.PixelSnapH = true;
    io.Fonts->AddFontFromFileTTF("content/font/roboto/static/Roboto-Regular.ttf", uiFontSize, &fontCfg);
    io.Fonts->AddFontFromFileTTF("content/font/jetbrains_mono/JetBrainsMono-Regular.ttf", editorFontSize, &fontCfg);

    ImGui_ImplOpenGL3_Init("#version 130");

    Set_IMGUI_Default_Theme();

    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding    = 6.0f;
        style.FrameRounding     = 4.0f;
        style.GrabRounding      = 4.0f;
        style.ScrollbarRounding = 4.0f;
        style.PopupRounding     = 4.0f;
        style.TabRounding       = 4.0f;
        style.FramePadding      = ImVec2(6.0f, 4.0f);
        style.ItemSpacing       = ImVec2(8.0f, 6.0f);
        style.IndentSpacing     = 16.0f;
        style.ScrollbarSize     = 12.0f;
        style.GrabMinSize       = 10.0f;
        style.WindowBorderSize  = 1.0f;
        style.FrameBorderSize   = 0.0f;
    }

    if (m_configuration.dpi_scale > 1)
        ImGui::GetStyle().ScaleAllSizes(static_cast<float>(m_configuration.dpi_scale));

    m_driver->setTextureCreationFlag(ETCF_ALWAYS_32_BIT, true);

    stringw windowName = name.c_str();
    m_device->setWindowCaption(windowName.c_str());

    m_sceneManager->setAmbientLight(SColor(255, 0, 0, 0)); // black: per-pixel shader controls ambient via uAmbientColor

    m_sceneManager->getParameters()->setAttribute(ALLOW_ZWRITE_ON_TRANSPARENT, true);

    m_driver->getMaterial2D().TextureLayer[0].BilinearFilter = true;
    m_driver->getMaterial2D().TextureLayer[0].TrilinearFilter = true;

    createDefaultShaders();

    g_DefaultTextRenderableFontSm = m_device->getGUIEnvironment()->getFont("content/texture/font/defaultfont_sm.bmp");

	m_manipulator = m_sceneManager->getMeshManipulator();

	m_assimpLoader = new IrrAssimp(m_sceneManager);
	if (!m_assimpLoader)
	{
		spdlog::error("Failed to create the ASSIMP loader");
	}

    initDefaultSkyDome();

	// Make a default camera
	m_defaultCamera = m_sceneManager->addCameraSceneNode();

	m_device->getCursorControl()->setVisible(false);

	m_AnimatedSpriteLoader.loadAnimatedSprites();

	// Init SPARK particle system
	SPK::randomSeed = m_device->getTimer()->getRealTime();
	// Sets the update step
	SPK::System::setClampStep(true, 0.1f);			// clamp the step to 100 ms
	SPK::System::useAdaptiveStep(0.001f, 0.016f);		// use an adaptive step from 1ms to 10ms (1000fps to 60fps)

	// Blood effect shader
	CBloodShader::instance().createMaterial(m_device);

	// Unreal skeletal mesh loader
	CPSKMeshFileLoader* psk_loader = new CPSKMeshFileLoader(m_sceneManager, m_device->getLogger());
	CPSAMeshFileLoader* psa_loader = new CPSAMeshFileLoader(m_sceneManager, m_device->getLogger());
	m_sceneManager->addExternalMeshLoader(psk_loader);
	m_sceneManager->addExternalMeshLoader(psa_loader);
	psk_loader->drop();
	psa_loader->drop();

    recreatePostProcessRTTs(m_irrlichtParams.WindowSize.Width, m_irrlichtParams.WindowSize.Height);
    createShadowResources();

	if (m_configuration.antialiasingFactor == 1)
	{
		setPostProcessPassEnabled("fxaa", true);
	}
	RenderManager::Get()->setBloomEnabled(true);
	RenderManager::Get()->bloomBrightCallback()->threshold = 1.0f; // lower = more glow
	RenderManager::Get()->bloomCompositeCallback()->strength = 2.0f; // intensity
	RenderManager::Get()->setTonemapEnabled(true);
	RenderManager::Get()->tonemapCallback()->exposure = 10.0f;  // brighter scene
	RenderManager::Get()->tonemapCallback()->whitePoint = 11.2f;
	RenderManager::Get()->setSharpenEnabled(true);
	RenderManager::Get()->sharpenCallback()->strength = 0.6f;  // more aggressive
	RenderManager::Get()->setAutoExposure(false);
	RenderManager::Get()->setPixelateEnabled(false);
	RenderManager::Get()->pixelateCallback()->pixelSize = 4.0f;
}
RenderManager::~RenderManager()
{
	SPK::SPKFactory::getInstance().traceAll();
	SPK::SPKFactory::getInstance().destroyAll();

    if (m_sceneRTT) m_driver->removeTexture(m_sceneRTT);
    if (m_ppRTT[0]) m_driver->removeTexture(m_ppRTT[0]);
    if (m_ppRTT[1]) m_driver->removeTexture(m_ppRTT[1]);
    if (m_lumRTT)   m_driver->removeTexture(m_lumRTT);
    delete m_blitCallback;
    delete m_fxaaCallback;
    delete m_bloomBrightCallback;
    delete m_bloomBlurHCallback;
    delete m_bloomBlurVCallback;
    delete m_bloomCompositeCallback;
    delete m_tonemapCallback;
    delete m_sharpenCallback;
    delete m_lumMeasureCallback;

    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();

    m_device->closeDevice();
	m_device->drop();

    delete s_Instance;
}

void RenderManager::beginImGui()
{
    ImGuiIO& io = ImGui::GetIO();
    auto sz = m_driver->getScreenSize();
    io.DisplaySize = ImVec2(static_cast<float>(sz.Width), static_cast<float>(sz.Height));
    io.DeltaTime = 1.0f / 60.0f;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
}

void RenderManager::draw(f32 dt)
{
	updateDebugRender();
	
	// readBuffer is the buffer we're rendering from this frame
	// Buffer swap is managed by Engine::update() and only occurs when logic runs
	int readBuffer = m_renderBufferIndex;

//#ifdef DEBUG
//	static s32 oldFps = 0;
//	if (oldFps != m_driver->getFPS())
//	{
//		core::stringw windowCaption = L"Engine - FPS: ";
//		windowCaption += m_driver->getFPS();
//		m_device->setWindowCaption(windowCaption.c_str());
//		oldFps = m_driver->getFPS();
//	}
//#endif

    if (m_driver->getScreenSize().Height != m_configuration.height ||
        m_driver->getScreenSize().Width  != m_configuration.width)
    {
        m_configuration.width  = m_driver->getScreenSize().Width;
        m_configuration.height = m_driver->getScreenSize().Height;
        recreatePostProcessRTTs(m_configuration.width, m_configuration.height);
    }

    m_driver->beginScene(true, true, SColor(0xFF000000)/*m_backgroundColor*/);

    // --- Entity Builder preview pass ---
    if (m_previewSM && m_previewRTT)
    {
        m_driver->setRenderTarget(m_previewRTT, true, true, SColor(255, 40, 40, 40));
        m_previewSM->drawAll();
        m_driver->setRenderTarget(nullptr, false, false);
    }
    // --- End preview pass ---

    // Redirect 3D scene into the RGBA16F scene RTT for post-processing
    if (m_sceneRTT)
    {
        m_driver->setRenderTarget(m_sceneRTT, true, true, SColor(0x00000000));
        // Capture the FBO ID while the scene RTT is bound — used later to blit
        // its world depth into the backbuffer for LDR effect Z-testing.
        GLint fbo = 0;
        glGetIntegerv(0x8CA6 /*GL_FRAMEBUFFER_BINDING*/, &fbo);
        m_sceneFBOId = (int)fbo;
    }

    // Hide viewmodel nodes so they are excluded from the main scene pass.
    // OnAnimate() still runs for them (visibility doesn't affect animation),
    // so their absolute transforms stay current for the manual pass below.
    std::vector<bool> viewmodelWasVisible(m_viewmodelNodes.size());
    for (size_t i = 0; i < m_viewmodelNodes.size(); ++i)
    {
        viewmodelWasVisible[i] = m_viewmodelNodes[i]->isVisible();
        m_viewmodelNodes[i]->setVisible(false);
    }

    // Hide debug nodes for the same reason — they must not enter the bloom/
    // tonemap pipeline and are drawn after post-processing instead.
    std::vector<bool> debugWasVisible(m_debugNodes.size());
    for (size_t i = 0; i < m_debugNodes.size(); ++i)
    {
        debugWasVisible[i] = m_debugNodes[i]->isVisible();
        m_debugNodes[i]->setVisible(false);
    }

    // Hide LDR effect nodes (muzzle flashes, SPARK particles). They composite
    // onto the tonemapped backbuffer after post-processing to preserve their
    // additive blending as it was in the pre-HDR pipeline.
    std::vector<bool> ldrWasVisible(m_ldrEffectNodes.size());
    for (size_t i = 0; i < m_ldrEffectNodes.size(); ++i)
    {
        ldrWasVisible[i] = m_ldrEffectNodes[i]->isVisible();
        m_ldrEffectNodes[i]->setVisible(false);
    }

    // Shadow depth pre-pass — renders the scene from the light's POV into m_shadowMapRTT.
    drawShadowPass();

    m_sceneManager->drawAll();

    // Sorted transparent pass — water, particles, crystal props (now phong_perpixel_transparent).
    drawTransparentPass();

    // Blit world geometry depth from the scene RTT into the backbuffer NOW,
    // before clearZBuffer() wipes it for the viewmodel pass.  The post-process
    // chain runs with depth-test and depth-write disabled, so this depth survives
    // untouched until the LDR effect nodes render after post-processing.
    if (m_fnBindFramebuffer && m_fnBlitFramebuffer && m_sceneFBOId > 0)
    {
        using PFNBind = void(*)(unsigned int, unsigned int);
        using PFNBlit = void(*)(int,int,int,int,int,int,int,int,unsigned int,unsigned int);
        auto sz = m_sceneRTT->getSize();
        ((PFNBind)m_fnBindFramebuffer)(0x8CA8u /*GL_READ_FRAMEBUFFER*/, (unsigned int)m_sceneFBOId);
        ((PFNBind)m_fnBindFramebuffer)(0x8CA9u /*GL_DRAW_FRAMEBUFFER*/, 0u);
        ((PFNBlit)m_fnBlitFramebuffer)(0, 0, (int)sz.Width, (int)sz.Height,
                                       0, 0, (int)sz.Width, (int)sz.Height,
                                       0x00000100u /*GL_DEPTH_BUFFER_BIT*/, 0x2600u /*GL_NEAREST*/);
        // Restore scene RTT as the active FBO — clearZBuffer() and viewmodel pass still write to it.
        ((PFNBind)m_fnBindFramebuffer)(0x8D40u /*GL_FRAMEBUFFER*/, (unsigned int)m_sceneFBOId);
    }

    // Clear depth so viewmodels render on top of all world geometry,
    // then draw each visible viewmodel with normal depth test + write
    // so they self-occlude correctly.
    if (!m_viewmodelNodes.empty())
    {
        m_driver->clearZBuffer();
        for (size_t i = 0; i < m_viewmodelNodes.size(); ++i)
        {
            if (!viewmodelWasVisible[i]) continue;
            auto* n = m_viewmodelNodes[i];
            // Node was hidden during drawAll() so OnAnimate skipped updateAbsolutePosition().
            // Force it now so the world transform reflects the current parent (camera) position.
            n->updateAbsolutePosition();
            m_driver->setTransform(ETS_WORLD, n->getAbsoluteTransformation());
            n->render();

            // render() only draws this node's geometry, not child nodes.
            // Walk the subtree so effects parented to bones (e.g. muzzle flash billboards)
            // are also rendered in this pass.
            std::vector<irr::scene::ISceneNode*> stack;
            for (auto* child : n->getChildren())
                stack.push_back(child);
            while (!stack.empty())
            {
                auto* child = stack.back(); stack.pop_back();
                if (!child->isVisible()) continue;
                child->updateAbsolutePosition();
                m_driver->setTransform(ETS_WORLD, child->getAbsoluteTransformation());
                child->render();
                for (auto* gc : child->getChildren())
                    stack.push_back(gc);
            }
        }
    }

    // Restore viewmodel visibility for game logic / next frame
    for (size_t i = 0; i < m_viewmodelNodes.size(); ++i)
        m_viewmodelNodes[i]->setVisible(viewmodelWasVisible[i]);

    // Restore debug node visibility for game logic / next frame
    for (size_t i = 0; i < m_debugNodes.size(); ++i)
        m_debugNodes[i]->setVisible(debugWasVisible[i]);

    // Restore LDR effect node visibility for game logic / next frame
    for (size_t i = 0; i < m_ldrEffectNodes.size(); ++i)
        m_ldrEffectNodes[i]->setVisible(ldrWasVisible[i]);

    // Return to backbuffer and run the post-process chain (reads m_sceneRTT)
    if (m_sceneRTT)
        m_driver->setRenderTarget(nullptr, false, false);
    if (m_autoExposure && m_lumRTT && m_lumMaterial >= 0)
        measureAndAdaptExposure(dt);

    // When pixelate is the last enabled pass and there are LDR effect nodes, split
    // the chain: run everything except pixelate into a ppRTT, composite LDR effects
    // into that RTT, then apply pixelate over the combined result.
    // Note: ppRTTs have no depth buffer, so LDR effects render without depth occlusion
    // in this path — acceptable given the deliberately low-fi pixelation aesthetic.
    std::vector<PostProcessPass*> activePasses;
    for (auto& pass : m_postProcessPasses)
        if (pass.enabled) activePasses.push_back(&pass);

    const bool pixelateCaptureLDR =
        !activePasses.empty() &&
        activePasses.back()->name == "pixelate" &&
        !m_ldrEffectNodes.empty();

    if (pixelateCaptureLDR)
    {
        const int N = static_cast<int>(activePasses.size());

        // Run all passes except the final pixelate, writing to ppRTT.
        irr::video::ITexture* preSrc = m_sceneRTT;
        for (int i = 0; i < N - 1; ++i)
        {
            PostProcessPass* pass = activePasses[i];
            irr::video::ITexture* src = (i == 0) ? m_sceneRTT : m_ppRTT[(i - 1) % 2];
            irr::video::ITexture* dst = m_ppRTT[i % 2];
            m_driver->setRenderTarget(dst, true, false, irr::video::SColor(0));
            irr::video::SMaterial mat;
            mat.MaterialType    = static_cast<irr::video::E_MATERIAL_TYPE>(pass->materialTypeIndex);
            mat.setTexture(0, src);
            if (pass->needsOriginalScene) mat.setTexture(1, m_sceneRTT);
            mat.ZBuffer         = 0;
            mat.ZWriteEnable    = false;
            mat.Lighting        = false;
            mat.BackfaceCulling = false;
            m_driver->setMaterial(mat);
            drawFullscreenQuad();
            preSrc = dst;
        }

        // Apply pixelate: preSrc → backbuffer.
        // LDR effect nodes are rendered to the backbuffer unconditionally below,
        // after the depth blit, so they always depth-test against scene geometry.
        m_driver->setRenderTarget(nullptr, false, false);
        {
            PostProcessPass* pixPass = activePasses.back();
            irr::video::SMaterial mat;
            mat.MaterialType    = static_cast<irr::video::E_MATERIAL_TYPE>(pixPass->materialTypeIndex);
            mat.setTexture(0, preSrc);
            mat.ZBuffer         = 0;
            mat.ZWriteEnable    = false;
            mat.Lighting        = false;
            mat.BackfaceCulling = false;
            m_driver->setMaterial(mat);
            drawFullscreenQuad();
        }
    }
    else
    {
        runPostProcessChain();
    }

    // Draw debug nodes directly to the backbuffer after post-processing so
    // they are never affected by bloom or tonemapping.
    for (size_t i = 0; i < m_debugNodes.size(); ++i)
    {
        if (!debugWasVisible[i]) continue;
        auto* n = m_debugNodes[i];
        n->updateAbsolutePosition();
        m_driver->setTransform(ETS_WORLD, n->getAbsoluteTransformation());
        n->render();
    }

    // Depth pre-pass: stamp viewmodel geometry into the backbuffer depth buffer so
    // LDR effects depth-test against the gun, not just world geometry.
    // Runs unconditionally — LDR nodes always render to the backbuffer (which holds the
    // blitted scene depth) so depth testing is consistent in both edit and game mode.
    if (!m_viewmodelNodes.empty())
        {
            irr::video::SMaterial depthMat;
            depthMat.ZWriteEnable = true;
            m_driver->setMaterial(depthMat);           // flush Irrlicht GL state (sets GL_LEQUAL)
            glDepthFunc(0x0207u /*GL_ALWAYS*/);        // override to always-pass so we force-write
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

            for (size_t i = 0; i < m_viewmodelNodes.size(); ++i)
            {
                if (!viewmodelWasVisible[i]) continue;
                auto* n = m_viewmodelNodes[i];

                irr::scene::IMesh* mesh = nullptr;
                const auto ntype = n->getType();
                if (ntype == irr::scene::ESNT_ANIMATED_MESH)
                {
                    auto* a = static_cast<irr::scene::IAnimatedMeshSceneNode*>(n);
                    if (a->getMesh()) mesh = a->getMesh()->getMesh(a->getFrameNr());
                }
                else if (ntype == irr::scene::ESNT_MESH || ntype == irr::scene::ESNT_OCTREE)
                    mesh = static_cast<irr::scene::IMeshSceneNode*>(n)->getMesh();

                if (!mesh) continue;
                m_driver->setTransform(irr::video::ETS_WORLD, n->getAbsoluteTransformation());
                for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); ++b)
                    m_driver->drawMeshBuffer(mesh->getMeshBuffer(b));
            }

            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            // The raw glDepthFunc(GL_ALWAYS) above desynchronises Irrlicht's state tracker:
            // Irrlicht still believes depth func = GL_LEQUAL (from the setMaterial call before
            // the override), so it won't re-emit glDepthFunc when LDR nodes render — leaving
            // OpenGL in GL_ALWAYS mode and causing all LDR fragments to pass depth.
            // Explicitly restore here so the tracker and OpenGL agree.
            glDepthFunc(0x0203u /*GL_LEQUAL*/);
    }

    // Draw LDR effect nodes (muzzle flashes, SPARK particles) onto the tonemapped
    // backbuffer. Their additive blending behaves identically to the pre-HDR pipeline
    // because the destination pixels are already in [0,1] LDR after tonemapping.
    // Parent bone transforms remain valid: the viewmodel pass updated the full subtree
    // (including hidden children's params) before post-processing ran.
    for (size_t i = 0; i < m_ldrEffectNodes.size(); ++i)
    {
        if (!ldrWasVisible[i]) continue;
        auto* n = m_ldrEffectNodes[i];
        n->updateAbsolutePosition();
        m_driver->setTransform(ETS_WORLD, n->getAbsoluteTransformation());

        // CMeshSceneNode::render() skips transparent buffers when called outside
        // drawAll() because getSceneNodeRenderPass() returns a non-transparent pass.
        // For mesh nodes, draw all buffers directly to bypass that filter.
        const auto ntype = n->getType();
        if (ntype == irr::scene::ESNT_MESH || ntype == irr::scene::ESNT_OCTREE)
        {
            auto* mn = static_cast<irr::scene::IMeshSceneNode*>(n);
            irr::scene::IMesh* mesh = mn->getMesh();
            if (mesh)
            {
                for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); ++b)
                {
                    m_driver->setMaterial(mn->getMaterial(b));
                    m_driver->drawMeshBuffer(mesh->getMeshBuffer(b));
                }
            }
        }
        else
        {
            n->render();
        }
    }

    for (auto line : m_lineRenderableList[readBuffer])
    {
        SMaterial mtl;
        mtl.Lighting = false;

        m_driver->setMaterial(mtl);
        m_driver->setTransform(ETS_WORLD, IdentityMatrix);

        m_driver->draw3DLine(line.line.start, line.line.end, line.color);
    }

    {
        SMaterial mtl;
        mtl.Lighting = false;
        m_driver->setMaterial(mtl);
        m_driver->setTransform(ETS_WORLD, IdentityMatrix);
        for (auto& line : m_overlayLineList)
            m_driver->draw3DLine(line.line.start, line.line.end, line.color);
        m_overlayLineList.clear();
    }

	m_driver->enableMaterial2D();
	{
		for (const auto& image : m_imageRenderableList[readBuffer]) {
			if (!image.image) {
				continue;
			}

			auto src_rect = irr::core::rect<s32>(0, 0, image.image->getSize().Width, image.image->getSize().Height);

			if (image.isScaled) {
				irr::video::SColor corners[4] = { image.color, image.color, image.color, image.color };
				m_driver->draw2DImage(image.image, image.destRect, src_rect, nullptr, corners, image.use_alpha);
			} else {
				m_driver->draw2DImage(image.image, image.position, src_rect, nullptr, image.color, image.use_alpha);
			}
		}
	}
	m_driver->enableMaterial2D(false);

	m_driver->enableMaterial2D();
	{
		for (const auto& rect : m_rectangleRenderableList[readBuffer]) {
			m_driver->draw2DRectangle(rect.color, rect.position);
		}
	}
	m_driver->enableMaterial2D(false);


	for (const auto& text : m_textRenderableList[readBuffer]) {
		text.font->draw(text.text, text.position, text.color, text.hcenter, text.vcenter);
	}

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

	//m_driver->runAllOcclusionQueries(false);
	//m_driver->updateAllOcclusionQueries();

    m_driver->endScene();
}

void RenderManager::createShaderMaterial(
    std::string name, std::string fragment, std::string vertex,
    E_MATERIAL_TYPE materialType, E_PIXEL_SHADER_TYPE psVersion, E_VERTEX_SHADER_TYPE vsVersion)
{
    ShaderMaterial shader(name);

    shader.material = m_gpu->addHighLevelShaderMaterialFromFiles(
        vertex.c_str(), "main", vsVersion,
        fragment.c_str(), "main", psVersion,
        m_shaderConstantCallBack, materialType, 0, EGSL_DEFAULT);

    ShaderMaterialManager::add(shader);
}

void RenderManager::createDefaultShaders()
{
    createShaderMaterial("phong_perpixel", "content/shader/phong_perpixel.frag", "content/shader/phong_perpixel.vert");

    // Additive color shader — texture * DiffuseColor uniform, additive blending.
    // Used for laser beams, muzzle flashes, and any effect that needs a tinted additive material.
    {
        m_additiveColorCallback = new AdditiveColorCallback();
        ShaderMaterial s("additive_color");
        s.material = m_gpu->addHighLevelShaderMaterialFromFiles(
            "content/shader/additive_color.vert", "main", EVST_VS_2_0,
            "content/shader/additive_color.frag", "main", EPST_PS_2_0,
            m_additiveColorCallback, EMT_TRANSPARENT_ADD_COLOR, 0, EGSL_DEFAULT);
        ShaderMaterialManager::add(s);
    }

    // Transparent variant — same shader, alpha-blended base material.
    // Used for crystal/glassy props; Fresnel and opacity driven by MaterialTypeParams[2]/[3]
    // and DiffuseColor.alpha rather than a separate shader + RTT pass.
    {
        ShaderMaterial phongTransp("phong_perpixel_transparent");
        phongTransp.material = m_gpu->addHighLevelShaderMaterialFromFiles(
            "content/shader/phong_perpixel.vert", "main", EVST_VS_2_0,
            "content/shader/phong_perpixel.frag", "main", EPST_PS_2_0,
            m_shaderConstantCallBack, EMT_TRANSPARENT_ALPHA_CHANNEL, 0, EGSL_DEFAULT);
        ShaderMaterialManager::add(phongTransp);
    }

    // Barrel heat shader — identical PBR base, plus blackbody heat glow driven
    // by uHeatLevel pushed each frame by BarrelHeatShaderCallback.
    {
        ShaderMaterial barrelHeatShader("barrel_heat");
        barrelHeatShader.material = m_gpu->addHighLevelShaderMaterialFromFiles(
            "content/shader/barrel_heat.vert", "main", EVST_VS_2_0,
            "content/shader/barrel_heat.frag", "main", EPST_PS_2_0,
            m_barrelHeatCallback, EMT_SOLID, 0, EGSL_DEFAULT);
        ShaderMaterialManager::add(barrelHeatShader);
    }

    // Water shader — uses its own callback for per-mesh color uniforms and a
    // transparent base so gl_FragColor.a controls surface opacity.
    ShaderMaterial waterShader("water");
    waterShader.material = m_gpu->addHighLevelShaderMaterialFromFiles(
        "content/shader/water.vert", "main", EVST_VS_2_0,
        "content/shader/water.frag", "main", EPST_PS_2_0,
        m_waterCallback, EMT_TRANSPARENT_ALPHA_CHANNEL, 0, EGSL_DEFAULT);
    ShaderMaterialManager::add(waterShader);

    // Foliage shader — alpha-tested leaves / billboard foliage using the standard
    // PBR vertex shader with a custom fragment that discards below an alpha threshold.
    createShaderMaterial("foliage",
        "content/shader/foliage.frag",
        "content/shader/phong_perpixel.vert",
        EMT_TRANSPARENT_ALPHA_CHANNEL_REF,
        EPST_PS_2_0, EVST_VS_2_0);

    // Grass shader — wind-animated vertex displacement on low-polygon grass patches.
    createShaderMaterial("grass",
        "content/shader/grass.frag",
        "content/shader/grass.vert",
        EMT_SOLID,
        EPST_PS_2_0, EVST_VS_2_0);

    // Terrain blend shader — splat map blending over original diffuse, reuses phong_perpixel.vert.
    {
        ShaderMaterial terrainShader("terrain_blend");
        terrainShader.material = m_gpu->addHighLevelShaderMaterialFromFiles(
            "content/shader/phong_perpixel.vert", "main", EVST_VS_2_0,
            "content/shader/terrain_blend.frag",  "main", EPST_PS_2_0,
            m_terrainCallback, EMT_SOLID, 0, EGSL_DEFAULT);
        ShaderMaterialManager::add(terrainShader);
    }

    // Shadow depth shader — used for the light-POV depth pre-pass.
    // SOverrideMaterial forces all solid geometry through it; only writes depth to R channel.
    {
        m_shadowDepthCallback = new ShadowDepthCallback();
        ShaderMaterial shadowShader("shadow_depth");
        shadowShader.material = m_gpu->addHighLevelShaderMaterialFromFiles(
            "content/shader/shadow_depth.vert", "main", EVST_VS_2_0,
            "content/shader/shadow_depth.frag", "main", EPST_PS_2_0,
            m_shadowDepthCallback, EMT_SOLID, 0, EGSL_DEFAULT);
        ShaderMaterialManager::add(shadowShader);
        m_shadowDepthMat = shadowShader.material;
    }


    // Post-process passes — blit (passthrough) and FXAA are mutually exclusive at startup.
    // Both shaders are always compiled; only one is enabled based on antialiasingFactor.
    // Use setPostProcessPassEnabled() to switch between them at runtime.
    const bool fxaaEnabled = m_configuration.antialiasingFactor > 0;

    // Bloom passes (disabled by default — call setBloomEnabled(true) to activate).
    // Order matters: bright → blur_h → blur_v → composite, then blit/fxaa on top.
    {
        m_bloomBrightCallback = new BloomBrightCallback();
        ShaderMaterial s("bloom_bright_pp");
        s.material = m_gpu->addHighLevelShaderMaterialFromFiles(
            "content/shader/fxaa.vert",       "main", EVST_VS_2_0,
            "content/shader/bloom_bright.frag","main", EPST_PS_2_0,
            m_bloomBrightCallback, EMT_SOLID, 0, EGSL_DEFAULT);
        ShaderMaterialManager::add(s);
        registerPostProcessPass(PostProcessPass("bloom_bright", s.material, m_bloomBrightCallback, false));
    }
    {
        m_bloomBlurHCallback = new BloomBlurCallback();
        m_bloomBlurHCallback->direction[0] = 1.0f;
        m_bloomBlurHCallback->direction[1] = 0.0f;
        ShaderMaterial s("bloom_blur_h_pp");
        s.material = m_gpu->addHighLevelShaderMaterialFromFiles(
            "content/shader/fxaa.vert",      "main", EVST_VS_2_0,
            "content/shader/bloom_blur.frag","main", EPST_PS_2_0,
            m_bloomBlurHCallback, EMT_SOLID, 0, EGSL_DEFAULT);
        ShaderMaterialManager::add(s);
        registerPostProcessPass(PostProcessPass("bloom_blur_h", s.material, m_bloomBlurHCallback, false));
    }
    {
        m_bloomBlurVCallback = new BloomBlurCallback();
        m_bloomBlurVCallback->direction[0] = 0.0f;
        m_bloomBlurVCallback->direction[1] = 1.0f;
        ShaderMaterial s("bloom_blur_v_pp");
        s.material = m_gpu->addHighLevelShaderMaterialFromFiles(
            "content/shader/fxaa.vert",      "main", EVST_VS_2_0,
            "content/shader/bloom_blur.frag","main", EPST_PS_2_0,
            m_bloomBlurVCallback, EMT_SOLID, 0, EGSL_DEFAULT);
        ShaderMaterialManager::add(s);
        registerPostProcessPass(PostProcessPass("bloom_blur_v", s.material, m_bloomBlurVCallback, false));
    }
    {
        m_bloomCompositeCallback = new BloomCompositeCallback();
        ShaderMaterial s("bloom_composite_pp");
        s.material = m_gpu->addHighLevelShaderMaterialFromFiles(
            "content/shader/fxaa.vert",           "main", EVST_VS_2_0,
            "content/shader/bloom_composite.frag","main", EPST_PS_2_0,
            m_bloomCompositeCallback, EMT_SOLID, 0, EGSL_DEFAULT);
        ShaderMaterialManager::add(s);
        PostProcessPass compositePass("bloom_composite", s.material, m_bloomCompositeCallback, false);
        compositePass.needsOriginalScene = true;
        registerPostProcessPass(compositePass);
    }

    // Tonemapping (ACES filmic + gamma) — sits between bloom and FXAA.
    {
        m_tonemapCallback = new TonemapCallback();
        ShaderMaterial s("tonemap_pp");
        s.material = m_gpu->addHighLevelShaderMaterialFromFiles(
            "content/shader/fxaa.vert",    "main", EVST_VS_2_0,
            "content/shader/tonemap.frag", "main", EPST_PS_2_0,
            m_tonemapCallback, EMT_SOLID, 0, EGSL_DEFAULT);
        ShaderMaterialManager::add(s);
        registerPostProcessPass(PostProcessPass("tonemap", s.material, m_tonemapCallback, true));
    }

    // Color grading — saturation boost + tint + brightness (disabled by default; enable for Unreal mode).
    {
        m_colorGradeCallback = new ColorGradeCallback();
        irr::s32 cgMat = m_gpu->addHighLevelShaderMaterialFromFiles(
            "content/shader/fxaa.vert", "main", EVST_VS_2_0,
            "content/shader/colorgrade.frag", "main", EPST_PS_2_0,
            m_colorGradeCallback, EMT_SOLID, 0, EGSL_DEFAULT);
        ShaderMaterialManager::add(ShaderMaterial("colorgrade", cgMat));
        registerPostProcessPass(PostProcessPass("colorgrade", cgMat, m_colorGradeCallback, false));
    }

    // Posterize — color banding (disabled by default; enable for Unreal mode).
    {
        m_posterizeCallback = new PosterizeCallback();
        irr::s32 pzMat = m_gpu->addHighLevelShaderMaterialFromFiles(
            "content/shader/fxaa.vert", "main", EVST_VS_2_0,
            "content/shader/posterize.frag", "main", EPST_PS_2_0,
            m_posterizeCallback, EMT_SOLID, 0, EGSL_DEFAULT);
        ShaderMaterialManager::add(ShaderMaterial("posterize", pzMat));
        registerPostProcessPass(PostProcessPass("posterize", pzMat, m_posterizeCallback, false));
    }

    // Film grain — analog noise (disabled by default).
    {
        m_filmGrainCallback = new FilmGrainCallback();
        irr::s32 fgMat = m_gpu->addHighLevelShaderMaterialFromFiles(
            "content/shader/fxaa.vert", "main", EVST_VS_2_0,
            "content/shader/filmgrain.frag", "main", EPST_PS_2_0,
            m_filmGrainCallback, EMT_SOLID, 0, EGSL_DEFAULT);
        ShaderMaterialManager::add(ShaderMaterial("filmgrain", fgMat));
        registerPostProcessPass(PostProcessPass("filmgrain", fgMat, m_filmGrainCallback, false));
    }

    {
        m_blitCallback = new FXAAShaderCallback();
        ShaderMaterial blitShader("blit_pp");
        blitShader.material = m_gpu->addHighLevelShaderMaterialFromFiles(
            "content/shader/fxaa.vert", "main", EVST_VS_2_0,
            "content/shader/blit.frag", "main", EPST_PS_2_0,
            m_blitCallback, EMT_SOLID, 0, EGSL_DEFAULT);
        ShaderMaterialManager::add(blitShader);
        m_blitMat = blitShader.material;
        registerPostProcessPass(PostProcessPass("blit", blitShader.material, m_blitCallback, !fxaaEnabled));
    }

    {
        m_fxaaCallback = new FXAAShaderCallback();
        ShaderMaterial fxaaShader("fxaa_pp");
        fxaaShader.material = m_gpu->addHighLevelShaderMaterialFromFiles(
            "content/shader/fxaa.vert", "main", EVST_VS_2_0,
            "content/shader/fxaa.frag", "main", EPST_PS_2_0,
            m_fxaaCallback, EMT_SOLID, 0, EGSL_DEFAULT);
        ShaderMaterialManager::add(fxaaShader);
        registerPostProcessPass(PostProcessPass("fxaa", fxaaShader.material, m_fxaaCallback, fxaaEnabled));
    }

    // Sharpening — applied last to recover softness introduced by FXAA.
    {
        m_sharpenCallback = new SharpenCallback();
        ShaderMaterial s("sharpen_pp");
        s.material = m_gpu->addHighLevelShaderMaterialFromFiles(
            "content/shader/fxaa.vert",     "main", EVST_VS_2_0,
            "content/shader/sharpen.frag",  "main", EPST_PS_2_0,
            m_sharpenCallback, EMT_SOLID, 0, EGSL_DEFAULT);
        ShaderMaterialManager::add(s);
        registerPostProcessPass(PostProcessPass("sharpen", s.material, m_sharpenCallback, true));
    }

    // Pixelation — creative blocky-pixel effect, disabled by default.
    {
        m_pixelateCallback = new PixelateCallback();
        ShaderMaterial s("pixelate_pp");
        s.material = m_gpu->addHighLevelShaderMaterialFromFiles(
            "content/shader/fxaa.vert",      "main", EVST_VS_2_0,
            "content/shader/pixelate.frag",  "main", EPST_PS_2_0,
            m_pixelateCallback, EMT_SOLID, 0, EGSL_DEFAULT);
        ShaderMaterialManager::add(s);
        registerPostProcessPass(PostProcessPass("pixelate", s.material, m_pixelateCallback, false));
    }

    // Luminance measurement — not a chain pass; rendered to a 1x1 RTT each frame
    // by measureAndAdaptExposure() when auto-exposure is enabled.
    {
        m_lumMeasureCallback = new LumMeasureCallback();
        ShaderMaterial s("lum_measure_pp");
        s.material = m_gpu->addHighLevelShaderMaterialFromFiles(
            "content/shader/fxaa.vert",        "main", EVST_VS_2_0,
            "content/shader/lum_measure.frag", "main", EPST_PS_2_0,
            m_lumMeasureCallback, EMT_SOLID, 0, EGSL_DEFAULT);
        ShaderMaterialManager::add(s);
        m_lumMaterial = s.material;
    }
}

void RenderManager::initDefaultSkyDome(std::string texture)
{
    m_currentSkydomeTexture = texture;

    m_driver->setTextureCreationFlag(irr::video::ETCF_CREATE_MIP_MAPS, false);

    m_defaultSkyDome = m_sceneManager->addSkyDomeSceneNode(
        m_driver->getTexture(texture.c_str()), 64, 32, 1.0f, 2.0f, 500.0f);

    m_driver->setTextureCreationFlag(irr::video::ETCF_CREATE_MIP_MAPS, true);

	setEnvMap(texture);
}

void RenderManager::removeDefaultSkyDome()
{
    if (m_defaultSkyDome) {
        m_defaultSkyDome->remove();
        m_currentSkydomeTexture = std::string();
    }

	clearEnvMap();
}

void RenderManager::swapSkyDomeTexture(std::string texture)
{
    m_currentSkydomeTexture = texture;

    if (m_defaultSkyDome) 
	{
		m_driver->setTextureCreationFlag(irr::video::ETCF_CREATE_MIP_MAPS, false);
        m_defaultSkyDome->setMaterialTexture(0, m_driver->getTexture(texture.c_str()));
		m_driver->setTextureCreationFlag(irr::video::ETCF_CREATE_MIP_MAPS, true);
    }

	setEnvMap(texture);
}

void RenderManager::setEnvMap(const std::string& texturePath)
{
    irr::video::ITexture* tex = texturePath.empty() ? nullptr : m_driver->getTexture(texturePath.c_str());
    m_shaderConstantCallBack->setEnvMap(tex);
}

void RenderManager::clearEnvMap()
{
    m_shaderConstantCallBack->setEnvMap(nullptr);
}

ISceneNode *RenderManager::getNodeFromCursorPosition(ICameraSceneNode *camera)
{
    line3d<f32> ray = m_sceneManager->getSceneCollisionManager()->getRayFromScreenCoordinates(
        m_device->getCursorControl()->getPosition(), camera ? camera : m_sceneManager->getActiveCamera());

    vector3df point;
    triangle3df triangle;

    auto node =
        m_sceneManager->getSceneCollisionManager()->getSceneNodeAndCollisionPointFromRay(ray, point, triangle, 0, nullptr, true);

	return node;
}

ISceneNode *RenderManager::getNodeFromCursorPosition(vector3df& outHit, ICameraSceneNode *camera)
{
    line3d<f32> ray = m_sceneManager->getSceneCollisionManager()->getRayFromScreenCoordinates(
        m_device->getCursorControl()->getPosition(), camera ? camera : m_sceneManager->getActiveCamera());

    triangle3df triangle;
    return m_sceneManager->getSceneCollisionManager()->getSceneNodeAndCollisionPointFromRay(
        ray, outHit, triangle, 0, nullptr, true);
}

vector3df RenderManager::getPoint3DFromCursorPosition(ICameraSceneNode *camera)
{
    line3d<f32> ray = m_sceneManager->getSceneCollisionManager()->getRayFromScreenCoordinates(
        m_device->getCursorControl()->getPosition(), camera ? camera : m_sceneManager->getActiveCamera());

    vector3df point;
    triangle3df triangle;

    m_sceneManager->getSceneCollisionManager()->getSceneNodeAndCollisionPointFromRay(ray, point, triangle);

    return point;
}

ISceneNode* RenderManager::getNodeFromRaycast(vector3df start, vector3df end)
{
    line3df ray(start, end);
    vector3df point;
    triangle3df tri;
    return m_sceneManager->getSceneCollisionManager()->getSceneNodeAndCollisionPointFromRay(ray, point, tri);
}

ISceneNode* RenderManager::getNodeFromRaycast(vector3df start, vector3df end, vector3df& outHit)
{
    line3df ray(start, end);
    triangle3df tri;
    return m_sceneManager->getSceneCollisionManager()->getSceneNodeAndCollisionPointFromRay(ray, outHit, tri);
}

ISceneNode* RenderManager::getNodeFromRaycast(vector3df start, vector3df end, vector3df& outHit, s32 idBitMask)
{
    line3df ray(start, end);
    triangle3df tri;
    return m_sceneManager->getSceneCollisionManager()->getSceneNodeAndCollisionPointFromRay(ray, outHit, tri, idBitMask);
}

bool RenderManager::getNodeTriangleTextureName(ISceneNode* node, const triangle3df& tri, std::string& texname)
{
    IMesh* mesh = 0;

    ESCENE_NODE_TYPE type = node->getType();
    if (type == ESNT_MESH || type == ESNT_OCTREE) {
        mesh = dynamic_cast<IMeshSceneNode*>(node)->getMesh();
    }
    else
        if (type == ESNT_ANIMATED_MESH) {
            mesh = dynamic_cast<IAnimatedMeshSceneNode*>(node)->getMesh()->getMesh(0);
        }
        else {
            return false;
        }

    if (!mesh) {
        return false;
    }

    vector3df ptA = tri.pointA;
    vector3df ptB = tri.pointB;
    vector3df ptC = tri.pointC;

    matrix4 matrix = node->getAbsoluteTransformation();
    matrix4 inverse;
    vector3df p0, p1, p2;

    if (matrix.getInverse(inverse)) {
        inverse.transformVect(p0, ptA);
        inverse.transformVect(p1, ptB);
        inverse.transformVect(p2, ptC);
    }
    else {
        p0 = ptA; p1 = ptB; p2 = ptC;
    }

    for (u32 i = 0; i < mesh->getMeshBufferCount(); ++i)
    {
        bool p0Found = false;
        bool p1Found = false;
        bool p2Found = false;

        IMeshBuffer* buf = mesh->getMeshBuffer(i);
        for (u32 j = 0; j < buf->getVertexCount(); ++j) {
            vector3df pos = buf->getPosition(j);

            if ((!p0Found) && (pos.equals(p0))) {
                p0Found = true;
            }

            if ((!p1Found) && (pos.equals(p1))) {
                p1Found = true;
            }

            if ((!p2Found) && (pos.equals(p2))) {
                p2Found = true;
            }
        }

        if (p0Found && p1Found && p2Found) {
            ITexture* tex = buf->getMaterial().getTexture(0);

			// Return the singular mesh texture if subtextures are not found
            if (!tex) {
				tex = node->getMaterial(i).getTexture(0);
            }

			if (!tex)
			{
				return false;
			}
			
            texname = std::string(tex->getName().getPath().c_str());

            return true;
        }
    }

    return false;
}

std::string RenderManager::getMeshMaterialFromRay(vector3df start, vector3df end)
{
    line3df ray;
    ray.start = start;
    ray.end = end;

    vector3df point;
    triangle3df tri;

    ISceneNode* selectedNode = 0;
    selectedNode = m_sceneManager->getSceneCollisionManager()->getSceneNodeAndCollisionPointFromRay(ray, point, tri);

    if (!selectedNode) {
        return std::string();
    }

    std::string texname;
    if (getNodeTriangleTextureName(selectedNode, tri, texname)) {

#ifdef DEBUG_TEXTURE_PICKING_STATS
        auto windowWidth = 320, windowHeight = 300;
        ImGui::SetNextWindowSize(ImVec2(windowWidth, windowHeight));
        ImGui::SetNextWindowPos(ImVec2(0, 175));
        if (ImGui::Begin("Texture Picking Debug", reinterpret_cast<bool*>(1),
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {

            ImGui::Text("Texture  - %s", util::filenameFromPath(texname).c_str());
            ImGui::Text("Material - %s",
                _engine->material()->getMaterialName(
                    _engine->material()->getMaterialFromTexture(
                        util::filenameFromPath(texname).c_str())).c_str());
            ImGui::Text("SX - %f", ray.start.X);
            ImGui::Text("SY - %f", ray.start.Y);
            ImGui::Text("SZ - %f", ray.start.Z);
            ImGui::Text("EX - %f", ray.end.X);
            ImGui::Text("EY - %f", ray.end.Y);
            ImGui::Text("EZ - %f", ray.end.Z);

            ImGui::End();
        }
#endif
        return Utility::FilenameFromPath(texname);
    }

    return std::string();
}

std::string RenderManager::getMeshMaterialFromCameraRay(ICameraSceneNode *camera)
{
    ICameraSceneNode* cam = camera ? camera : m_sceneManager->getActiveCamera();

    camera->updateAbsolutePosition();

    line3df ray;
    ray.start = cam->getAbsolutePosition();
    ray.end = ray.start + (cam->getTarget() - ray.start).normalize() * 1000.0f;

    vector3df point;
    triangle3df tri;

    ISceneNode* selectedNode = 0;
    selectedNode = m_sceneManager->getSceneCollisionManager()->getSceneNodeAndCollisionPointFromRay(ray, point, tri);

    if (!selectedNode) {
        return std::string();
    }

    std::string texname;
    if (getNodeTriangleTextureName(selectedNode, tri, texname)) {

#ifdef DEBUG_TEXTURE_PICKING_STATS
        auto windowWidth = 320, windowHeight = 240;
        ImGui::SetNextWindowSize(ImVec2(windowWidth, windowHeight));
        ImGui::SetNextWindowPos(ImVec2(0, 100));
        if (ImGui::Begin("Texture Picking Debug", reinterpret_cast<bool*>(1),
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {

            ImGui::Text("Texture - %s", util::filenameFromPath(texname).c_str());
            ImGui::Text("SX - %f", ray.start.X);
            ImGui::Text("SY - %f", ray.start.Y);
            ImGui::Text("SZ - %f", ray.start.Z);
            ImGui::Text("EX - %f", ray.end.X);
            ImGui::Text("EY - %f", ray.end.Y);
            ImGui::Text("EZ - %f", ray.end.Z);

            ImGui::End();
        }
#endif	
        return Utility::FilenameFromPath(texname);
    }

    return std::string();
}

void RenderManager::setPreviewScene(irr::scene::ISceneManager* sm, irr::video::ITexture* rtt)
{
    m_previewSM  = sm;
    m_previewRTT = rtt;
}

void RenderManager::clearPreviewScene()
{
    m_previewSM  = nullptr;
    m_previewRTT = nullptr;
}

// Possible memory leak
void RenderManager::setNodeMesh(IAnimatedMesh* trimesh, IAnimatedMeshSceneNode* node, std::string file)
{
    auto meshptr = m_sceneManager->getMesh(file.c_str()); // <<< here


    if (!node && meshptr) {
        trimesh = meshptr;
        node = m_sceneManager->addAnimatedMeshSceneNode(trimesh, nullptr, 0xFFFF);
        return;
    }

    if (meshptr) {
        trimesh = meshptr; // <<< and here
        node->setMesh(meshptr);
        return;
    }

    spdlog::error("Failed to load mesh \'" + file + "\' in RenderManager::loadMesh");
}

RaycastResultData RenderManager::raycastWorldPosition(vector3df start, vector3df end, bool excludeDebugNodes)
{
    line3df ray;
    ray.start = start;
    ray.end = end;

    vector3df point;
    triangle3df tri;

    RaycastResultData data;

    // Pass root scene node to ensure all triangle selectors are checked
    // idBitMask = 0 means check all nodes regardless of ID
    auto node = m_sceneManager->getSceneCollisionManager()->getSceneNodeAndCollisionPointFromRay(
        ray, point, tri, 
        0,                              // idBitMask - check all IDs
        m_sceneManager->getRootSceneNode(),  // Check from root (was nullptr)
        excludeDebugNodes
    );
    if (node) {

        data.hit = true;
        data.ray = ray;
        data.point = point;
        data.tri = tri;
        data.node = node;
        
        // Calculate surface normal from triangle
        vector3df edge1 = tri.pointB - tri.pointA;
        vector3df edge2 = tri.pointC - tri.pointA;
        data.normal = edge1.crossProduct(edge2);
        data.normal.normalize();
        
        // Ensure normal points toward ray origin (away from surface)
        // If dot product is negative, normal points into surface - flip it
        vector3df rayDir = point - start;
        if (data.normal.dotProduct(rayDir) > 0) {
            data.normal = -data.normal;
        }

        return data;
    }

    // DEPRECATED
    //log::write("Raycast operation failed in RenderManager::raycastWorldPosition - No valid node detected", LOG_WARNING);
    return RaycastResultData();
}

vector3df RenderManager::getBoneLocalPosition(IAnimatedMeshSceneNode *mesh, int bone)
{
    return mesh->getJointNode(bone)->getPosition();
}
vector3df RenderManager::getBoneWorldPosition(IAnimatedMeshSceneNode *mesh, int bone)
{
    return mesh->getJointNode(bone)->getAbsolutePosition();
}
vector3df RenderManager::getBoneLocalPosition(IAnimatedMeshSceneNode *mesh, std::string bone)
{
    return mesh->getJointNode(bone.c_str())->getPosition();
}
vector3df RenderManager::getBoneWorldPosition(IAnimatedMeshSceneNode *mesh, std::string bone)
{
    return mesh->getJointNode(bone.c_str())->getAbsolutePosition();
}

void RenderManager::renderText2D(
    irr::core::stringw text,
    TEXT_DEFAULT_FONT font,
    irr::core::rect<irr::s32> position,
    irr::video::SColor color,
    bool hcenter,
    bool vcenter) 
{
    switch (font) {
    case TEXT_DEFAULT_FONT::SMALL:
        m_textRenderableList[1 - m_renderBufferIndex].emplace_back(Text2D(g_DefaultTextRenderableFontSm, text, position, color, hcenter, vcenter));
        break;
    default:
        m_textRenderableList[1 - m_renderBufferIndex].emplace_back(Text2D(g_DefaultTextRenderableFontSm, text, position, color, hcenter, vcenter));
        break;
    }
}

void RenderManager::renderText2D(
	irr::core::stringw text,
	TEXT_DEFAULT_FONT font,
	irr::core::vector2di position,
	irr::video::SColor color,
	bool hcenter,
	bool vcenter)
{
	switch (font) {
	case TEXT_DEFAULT_FONT::SMALL:
		m_textRenderableList[1 - m_renderBufferIndex].emplace_back(Text2D(g_DefaultTextRenderableFontSm, text, irr::core::rect<irr::s32>(position.X, position.Y, 0, 0), color, hcenter, vcenter));
		break;
	default:
		m_textRenderableList[1 - m_renderBufferIndex].emplace_back(Text2D(g_DefaultTextRenderableFontSm, text, irr::core::rect<irr::s32>(position.X, position.Y, 0, 0), color, hcenter, vcenter));
		break;
	}
}


irr::core::vector3df RenderManager::GetInVector(irr::scene::ISceneNode* node)
{
    if (node)
    {
        irr::core::matrix4 mat = node->getRelativeTransformation();
        irr::core::vector3df in(mat[8], mat[9], mat[10]);
        in.normalize();
        return in;
    }

    return irr::core::vector3df(0, 0, 0);
}
irr::core::vector3df RenderManager::GetLeftVector(irr::scene::ISceneNode* node)
{
    if (node)
    {
        irr::core::matrix4 mat = node->getRelativeTransformation();
        irr::core::vector3df left(mat[0], mat[1], mat[2]);
        left.normalize();
        return left;
    }

    return irr::core::vector3df(0, 0, 0);
}
irr::core::vector3df RenderManager::GetUpVector(irr::scene::ISceneNode* node)
{
    if (node)
    {
        irr::core::matrix4 mat = node->getRelativeTransformation();
        irr::core::vector3df up(mat[4], mat[5], mat[6]);
        up.normalize();
        return up;
    }

    return irr::core::vector3df(0, 0, 0);
}

void RenderManager::saveConfiguration(RenderConfiguration& configuration)
{
	std::ofstream ofs_render("config/render.xml");
	cereal::XMLOutputArchive render_config(ofs_render);

	m_configuration = configuration;
	render_config(configuration);
}

void RenderManager::getResolutionFromMode(int aspect, int mode, int& width, int& height)
{
	switch (aspect)
	{
	case 0:
	{
		switch (mode)
		{
		case 0:
		{
			width = 640;
			height = 480;

			break;
		}
		case 1:
		{
			width = 800;
			height = 600;

			break;
		}
		case 2:
		{
			width = 1024;
			height = 768;

			break;
		}
		case 3:
		{
			width = 1280;
			height = 960;

			break;
		}
		case 4:
		{
			width = 1600;
			height = 1200;

			break;
		}
		case 5:
		{
			width = 2048;
			height = 1536;

			break;
		}
		default: break;
		}

		break;
	}
	case 1:
	{
		switch (mode)
		{
		case 0:
		{
			width = 1280;
			height = 1024;

			break;
		}
		case 1:
		{
			width = 2560;
			height = 2048;

			break;
		}
		default: break;
		}

		break;
	}
	case 2:
	{
		switch (mode)
		{
		case 0:
		{
			width = 1280;
			height = 720;

			break;
		}
		case 1:
		{
			width = 1366;
			height = 768;

			break;
		}
		case 2:
		{
			width = 1600;
			height = 900;

			break;
		}
		case 3:
		{
			width = 1920;
			height = 1080;

			break;
		}
		case 4:
		{
			width = 2550;
			height = 1440;

			break;
		}
		case 5:
		{
			width = 3840;
			height = 2160;

			break;
		}
		default: break;
		}

		break;
	}
	case 3:
	{
		switch (mode)
		{
		case 0:
		{
			width = 1280;
			height = 800;

			break;
		}
		case 1:
		{
			width = 1440;
			height = 900;

			break;
		}
		case 2:
		{
			width = 1680;
			height = 1050;

			break;
		}
		case 3:
		{
			width = 1920;
			height = 1200;

			break;
		}
		case 4:
		{
			width = 2560;
			height = 1600;

			break;
		}
		case 5:
		{
			width = 3840;
			height = 2400;

			break;
		}
		default: break;
		}

		break;
	}
	default: break;
	}
}

// F9 Bounding Boxes, F10 Debug Sprites, F11 Entity Information
void RenderManager::updateDebugRender()
{
	// Draw bounding boxes
	static bool f9 = false, render_bound = false;
	if (InputManager::Get()->getKeyPressOnce(KEYBOARD_KEY::KEY_F9, &f9))
	{
		render_bound = !render_bound;
	}
	if (render_bound)
	{
		auto& sceneNodes = RenderManager::Get()->sceneManager()->getRootSceneNode()->getChildren();
		for (auto node : sceneNodes)
		{
			// Check AABB intersection
			auto nodeBox = node->getTransformedBoundingBox();

			// DEBUG: Draw candidate collision box  min (yellow) and max (magenta)
			// Write directly to the read buffer — updateDebugRender() runs inside draw(),
			// so renderLine3D() (which writes to the write buffer) would never be seen.
			auto& lines = m_lineRenderableList[m_renderBufferIndex];
			lines.emplace_back(Line3D(irr::core::line3df(nodeBox.MinEdge, irr::core::vector3df(nodeBox.MaxEdge.X, nodeBox.MinEdge.Y, nodeBox.MinEdge.Z)), irr::video::SColor(255, 255, 255, 0)));
			lines.emplace_back(Line3D(irr::core::line3df(nodeBox.MinEdge, irr::core::vector3df(nodeBox.MinEdge.X, nodeBox.MaxEdge.Y, nodeBox.MinEdge.Z)), irr::video::SColor(255, 255, 255, 0)));
			lines.emplace_back(Line3D(irr::core::line3df(nodeBox.MinEdge, irr::core::vector3df(nodeBox.MinEdge.X, nodeBox.MinEdge.Y, nodeBox.MaxEdge.Z)), irr::video::SColor(255, 255, 255, 0)));
			lines.emplace_back(Line3D(irr::core::line3df(nodeBox.MaxEdge, irr::core::vector3df(nodeBox.MinEdge.X, nodeBox.MaxEdge.Y, nodeBox.MaxEdge.Z)), irr::video::SColor(255, 255, 0, 255)));
			lines.emplace_back(Line3D(irr::core::line3df(nodeBox.MaxEdge, irr::core::vector3df(nodeBox.MaxEdge.X, nodeBox.MinEdge.Y, nodeBox.MaxEdge.Z)), irr::video::SColor(255, 255, 0, 255)));
			lines.emplace_back(Line3D(irr::core::line3df(nodeBox.MaxEdge, irr::core::vector3df(nodeBox.MaxEdge.X, nodeBox.MaxEdge.Y, nodeBox.MinEdge.Z)), irr::video::SColor(255, 255, 0, 255)));
		}
	}

	// Toggle debug sprites
	static auto f10_pressed = false;
	if (InputManager::Get()->getKeyPressOnce(KEYBOARD_KEY::KEY_F10, &f10_pressed))
	{
		WorldManager::Get()->renderSystem()->setDebugSpriteVisible(
			!WorldManager::Get()->renderSystem()->isDebugSpriteVisible());
	}

	static bool f11 = false, add_text = false;
	static std::vector<irr::scene::ITextSceneNode*> text_nodes;
	static std::vector<entityid> text_node_ids;
	if (InputManager::Get()->getKeyPressOnce(KEYBOARD_KEY::KEY_F11, &f11))
	{
		add_text = !add_text;

		if (!add_text)
		{
			if (!text_nodes.empty())
			{
				for (auto* text : text_nodes)
				{
					text->remove();
				}

				text_nodes.clear();
				text_node_ids.clear();
			}
		}
	}

	if (add_text)
	{
		auto& sceneNodes = RenderManager::Get()->sceneManager()->getRootSceneNode()->getChildren();
		for (auto node : sceneNodes)
		{
			auto it = std::find(text_node_ids.begin(), text_node_ids.end(), node->getID());
			
			if (it != text_node_ids.end())
			{
				continue;
			}
			else
			{
				text_node_ids.emplace_back(node->getID());

				auto entity = WorldManager::Get()->managerSystem()->getEntityByID(node->getID());

				if (entity.isValid() && entity.hasComponent<DescriptorComponent>() && entity.hasComponent<TransformComponent>())
				{
					std::string name = entity.getComponent<DescriptorComponent>().name + " ID: " + std::to_string(entity.getComponent<DescriptorComponent>().id);
					std::wstring text(name.begin(), name.end());

					text_nodes.emplace_back(m_sceneManager->addTextSceneNode(m_gui->getBuiltInFont(),
						text.c_str(),
						video::SColor(255, 255, 255, 255), entity.getComponent<TransformComponent>().node));
				}
			}
		}
	}
}

// =============================================================================
// Gather the closest dynamic lights to an object for per-pixel lighting upload
// =============================================================================
std::vector<LightUploadData> RenderManager::gatherClosestLights(
    irr::video::IVideoDriver*   driver,
    irr::scene::ISceneManager*  sceneManager,
    const irr::core::vector3df& objectWorldPos,
    int maxLights)
{
    std::vector<LightUploadData> result;

    const irr::u32 count = driver->getDynamicLightCount();
    result.reserve(count);

    // Get the view matrix to transform light positions to view space.
    // The shader computes lighting in view space (using gl_ModelViewMatrix built-ins),
    // so light positions must be in the same space as the fragment's vViewPos.
    const irr::core::matrix4 viewMatrix = driver->getTransform(irr::video::ETS_VIEW);

    // Grab the camera frustum so we can prioritize in-frustum lights.
    // Out-of-frustum lights get a distSq penalty, keeping visible lights
    // sticky in the top-8 slot as the camera rotates (reduces pop-in).
    const irr::scene::SViewFrustum* frustum = nullptr;
    if (sceneManager)
    {
        if (auto* cam = sceneManager->getActiveCamera())
            frustum = cam->getViewFrustum();
    }

    // Penalty multiplier applied to the sort key of out-of-frustum lights.
    // 4.0 makes them sort as if they were 2x farther away; raise to strengthen
    // the preference for in-frustum lights, lower to relax it.
    static constexpr float OUT_OF_FRUSTUM_PENALTY = 4.0f;

    for (irr::u32 i = 0; i < count; ++i)
    {
        const irr::video::SLight& light = driver->getDynamicLight(i);

        LightUploadData d;

        // Raw squared distance used for sorting
        irr::core::vector3df diff = light.Position - objectWorldPos;
        float rawDistSq = diff.dotProduct(diff);

        // Frustum visibility check: test the light position against every
        // frustum half-space. A single ISREL3D_BACK result means it's outside.
        bool inFrustum = true;
        if (frustum)
        {
            for (int p = 0; p < irr::scene::SViewFrustum::VF_PLANE_COUNT; ++p)
            {
                if (frustum->planes[p].classifyPointRelation(light.Position)
                        == irr::core::ISREL3D_BACK)
                {
                    inFrustum = false;
                    break;
                }
            }
        }

        // Apply penalty so out-of-frustum lights sort lower-priority
        d.distSq = inFrustum ? rawDistSq : rawDistSq * OUT_OF_FRUSTUM_PENALTY;

        // Upload position in VIEW SPACE so the shader can subtract it from vViewPos directly
        irr::core::vector3df viewSpacePos = light.Position;
        viewMatrix.transformVect(viewSpacePos);
        d.pos = viewSpacePos;

        d.color  = light.DiffuseColor;
        d.radius = light.Radius > 0.0f ? light.Radius : 1000.0f;

        d.isSpot = (light.Type == irr::video::ELT_SPOT);
        if (d.isSpot)
        {
            // Transform the spot direction to view space (rotation only, no translation).
            irr::core::vector3df dir = light.Direction;
            viewMatrix.rotateVect(dir);
            d.direction = dir.normalize();
            // OuterCone and InnerCone are half-angles in degrees (matches GL_SPOT_CUTOFF).
            d.cosOuter = cosf(light.OuterCone * irr::core::DEGTORAD);
            d.cosInner = cosf(light.InnerCone * irr::core::DEGTORAD);
        }

        result.push_back(d);
    }

    // Sort ascending by penalized distSq — in-frustum lights naturally
    // bubble to the front for any given distance bracket.
    std::sort(result.begin(), result.end(),
        [](const LightUploadData& a, const LightUploadData& b) { return a.distSq < b.distSq; });

    // Cap to maxLights
    if (static_cast<int>(result.size()) > maxLights)
        result.resize(static_cast<size_t>(maxLights));

    return result;
}

// =============================================================================
// Transparent sort pass - collects transparent mesh buffers from the scene,
// sorts them back-to-front relative to the camera, and re-renders them with
// depth write disabled so they blend correctly over the opaque scene.
// =============================================================================
void RenderManager::drawTransparentPass()
{
    irr::scene::ICameraSceneNode* camera = m_sceneManager->getActiveCamera();
    if (!camera)
        return;

    const irr::core::vector3df camPos = camera->getAbsolutePosition();

    std::vector<TransparentMeshBuffer> transparents;

    // Walk the entire scene graph from root
    std::vector<irr::scene::ISceneNode*> nodeStack;
    nodeStack.push_back(m_sceneManager->getRootSceneNode());

    while (!nodeStack.empty())
    {
        irr::scene::ISceneNode* node = nodeStack.back();
        nodeStack.pop_back();

        if (!node->isVisible())
            continue;

        // Queue children for traversal
        for (auto* child : node->getChildren())
            nodeStack.push_back(child);

        // Only pull mesh buffers from mesh and animated mesh nodes
        irr::scene::IMesh* mesh = nullptr;
        irr::scene::ESCENE_NODE_TYPE ntype = node->getType();

        if (ntype == irr::scene::ESNT_MESH || ntype == irr::scene::ESNT_OCTREE)
        {
            mesh = static_cast<irr::scene::IMeshSceneNode*>(node)->getMesh();
        }
        else if (ntype == irr::scene::ESNT_ANIMATED_MESH)
        {
            auto* animNode = static_cast<irr::scene::IAnimatedMeshSceneNode*>(node);
            if (animNode->getMesh())
                mesh = animNode->getMesh()->getMesh(animNode->getFrameNr());
        }

        if (!mesh)
            continue;

        const irr::core::vector3df nodePos = node->getAbsolutePosition();
        irr::core::vector3df toNode = nodePos - camPos;
        float distSq = toNode.dotProduct(toNode);

        for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); ++b)
        {
            // Use the SCENE NODE's material type (set by applyNodeShaders), not the
            // mesh buffer's baked-in type. The mesh file never knows about our custom shaders.
            irr::video::E_MATERIAL_TYPE nodeMatType = node->getMaterial(b).MaterialType;

            // Check if this buffer uses a transparent or refraction material type
            bool isTransparent =
                nodeMatType == irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL         ||
                nodeMatType == irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF      ||
                nodeMatType == irr::video::EMT_TRANSPARENT_ADD_COLOR              ||
                nodeMatType == irr::video::EMT_TRANSPARENT_VERTEX_ALPHA;

            if (isTransparent)
            {
                TransparentMeshBuffer entry;
                entry.node        = node;
                entry.bufferIndex = b;
                entry.distSq      = distSq;
                transparents.push_back(entry);
            }
        }
    }

    if (transparents.empty())
        return;

    // Sort far-to-near (painter's algorithm)
    std::sort(transparents.begin(), transparents.end(),
        [](const TransparentMeshBuffer& a, const TransparentMeshBuffer& b)
        { return a.distSq > b.distSq; });

    // Render each transparent buffer in sorted order
    for (const auto& entry : transparents)
    {
        irr::scene::IMesh* mesh = nullptr;
        irr::scene::ESCENE_NODE_TYPE ntype = entry.node->getType();

        if (ntype == irr::scene::ESNT_MESH || ntype == irr::scene::ESNT_OCTREE)
            mesh = static_cast<irr::scene::IMeshSceneNode*>(entry.node)->getMesh();
        else if (ntype == irr::scene::ESNT_ANIMATED_MESH)
        {
            auto* animNode = static_cast<irr::scene::IAnimatedMeshSceneNode*>(entry.node);
            if (animNode->getMesh())
                mesh = animNode->getMesh()->getMesh(animNode->getFrameNr());
        }

        if (!mesh || entry.bufferIndex >= mesh->getMeshBufferCount())
            continue;

        irr::scene::IMeshBuffer* buf = mesh->getMeshBuffer(entry.bufferIndex);

        m_driver->setTransform(irr::video::ETS_WORLD, entry.node->getAbsoluteTransformation());

        irr::video::SMaterial mat = entry.node->getMaterial(entry.bufferIndex);
        mat.ZWriteEnable = false;
        m_driver->setMaterial(mat);
        m_driver->drawMeshBuffer(buf);
    }

    // Restore depth write for subsequent passes (debug lines, UI)
    irr::video::SMaterial restore;
    restore.ZWriteEnable = true;
    m_driver->setMaterial(restore);
}
