#pragma once

#include <algorithm>
#include <vector>

#include "irrlicht.h"

#include "cereal/cereal.hpp"
#include <IMGUI/imgui.h>
#include "IrrAssimp/IrrAssimp.h"
#include "Engine/Input/IrrImGuiEventReceiver.h"

#include "Engine/Resource/AnimatedSpriteLoader.h"

#include <SPK.h>
#include <SPK_IRR.h>

#define DPI_SCALED_IMVEC2(x, y) ImVec2(x * RenderManager::Get()->getConfiguration().dpi_scale, y * RenderManager::Get()->getConfiguration().dpi_scale)

struct RenderConfiguration
{
    bool
        fullscreen,
        vSync,
		pointFilterOverride;

    int
        api,
        width,
        height,
		editor_width,
		editor_height,
        depth,
		aspect,
		mode,
		dpi_scale,
        anisotropyFactor,
        antialiasingFactor,
        frameLimit;

    template <class Archive>
    void serialize(Archive& archive)
    {
        archive(
            CEREAL_NVP(fullscreen),
            CEREAL_NVP(vSync),
			CEREAL_NVP(pointFilterOverride),
            CEREAL_NVP(api),
            CEREAL_NVP(width),
            CEREAL_NVP(height),
			CEREAL_NVP(editor_width),
			CEREAL_NVP(editor_height),
            CEREAL_NVP(depth),
			CEREAL_NVP(aspect),
			CEREAL_NVP(mode),
			CEREAL_NVP(dpi_scale),
            CEREAL_NVP(anisotropyFactor),
            CEREAL_NVP(antialiasingFactor),
            CEREAL_NVP(frameLimit));
    }

    RenderConfiguration() :
        fullscreen(false),
        vSync(false),
		pointFilterOverride(false),
        api(0),
        width(1024),
        height(768),
		editor_width(1024),
		editor_height(768),
        depth(32),
		aspect(0),
		mode(0),
		dpi_scale(1),
        anisotropyFactor(0),
        antialiasingFactor(0),
        frameLimit(60)
    {}
};

enum class TEXT_DEFAULT_FONT
{
    // 16x10
    SMALL
};

// 16x10
extern irr::gui::IGUIFont* g_DefaultTextRenderableFontSm;

struct Text2D
{
    Text2D() : hcenter(false), vcenter(false) {}
    Text2D(
        irr::gui::IGUIFont* font,
        irr::core::stringw text,
        irr::core::rect<irr::s32> position = irr::core::rect<irr::s32>(0, 0, 0, 0),
        irr::video::SColor color = irr::video::SColor(255, 255, 255, 255),
        bool hcenter = false,
        bool vcenter = false)
    {
        this->font = font;
        this->text = text;
        this->position = position;
        this->color = color;
        this->hcenter = hcenter;
        this->vcenter = vcenter;
    }

    bool hcenter, vcenter;

    irr::gui::IGUIFont* font;
    irr::core::stringw text;
    irr::core::rect<irr::s32> position;
    irr::video::SColor color;
};

struct Line3D
{
    irr::core::line3df line;
    irr::video::SColor color;

    Line3D() {}
    Line3D(irr::core::line3df line, irr::video::SColor color = irr::video::SColor(255, 255, 255, 255))
    {
        this->line = line;
        color.setAlpha(255);
        this->color = color;
    }
};

struct Image2D
{
    Image2D(
        irr::video::ITexture* image,
        irr::core::vector2di position = irr::core::vector2di(0, 0),
        irr::video::SColor color = irr::video::SColor(255, 255, 255, 255),
        bool use_alpha = true,
        bool use_center = false)
    {
        this->image = image;
        this->use_alpha = use_alpha;
        this->position = position;
        this->color = color;
        this->use_center = use_center;
        this->isScaled = false;
    }

    Image2D(
        irr::video::ITexture* image,
        irr::core::rect<irr::s32> destRect,
        irr::video::SColor color = irr::video::SColor(255, 255, 255, 255),
        bool use_alpha = true)
    {
        this->image = image;
        this->destRect = destRect;
        this->color = color;
        this->use_alpha = use_alpha;
        this->use_center = false;
        this->isScaled = true;
    }

    bool use_alpha;
    // NOIMP
    bool use_center;
    bool isScaled;

    irr::core::vector2di position;
    irr::core::rect<irr::s32> destRect;

    irr::video::ITexture* image;

    irr::video::SColor color;
};

struct Rectangle2D
{
    Rectangle2D(
        irr::core::rect<irr::s32> position,
        irr::video::SColor color = irr::video::SColor(255, 255, 255, 255))
    {
        this->position = position;
        this->color = color;
    }

    irr::core::rect<irr::s32> position;
    irr::video::SColor color;
};

struct RaycastResultData
{
    bool hit;

    irr::core::line3df ray;
    irr::core::vector3df point;
    irr::core::vector3df normal;  // Surface normal at hit point
    irr::core::triangle3df tri;

    irr::scene::ISceneNode *node;

    RaycastResultData() :
        hit(false), node(nullptr), normal(0, 0, 0) {}
};

// Data for a single dynamic light to be uploaded to a shader
struct LightUploadData
{
    irr::core::vector3df pos;
    irr::video::SColorf  color;
    float                radius;
    float                distSq;    // squared dist to object (used for sorting, not uploaded)

    // Spotlight-only fields — ignored for point lights
    irr::core::vector3df direction; // view-space direction the spot is pointing
    float                cosOuter;  // cos(outerCone half-angle); used to clip outside cone
    float                cosInner;  // cos(innerCone half-angle); used for soft edge falloff
    bool                 isSpot = false;
};

// A transparent mesh buffer queued for the sorted transparent pass
struct TransparentMeshBuffer
{
    irr::scene::ISceneNode*  node;
    irr::u32                 bufferIndex;
    float                    distSq;       // squared distance to camera (for sort)
    bool                     isRefraction = false;
};

struct ShaderMaterial
{
    std::string name;
    irr::s32    material;
    bool        isRefraction = false;

    ShaderMaterial(std::string name, irr::s32 material = 0, bool isRefraction = false)
        : name(std::move(name)), material(material), isRefraction(isRefraction) {}
};

class ShaderMaterialManager
{
public:
    static void add(ShaderMaterial material);
    static irr::video::E_MATERIAL_TYPE get(std::string name);
    static bool isRefraction(irr::s32 materialType);
    static const std::vector<ShaderMaterial>& getAll() { return s_ShaderMaterialList; }

private:
    static std::vector<ShaderMaterial> s_ShaderMaterialList;
};

struct PostProcessPass
{
    std::string                             name;
    irr::s32                                materialTypeIndex;
    irr::video::IShaderConstantSetCallBack* callback;
    bool                                    enabled;
    bool                                    needsOriginalScene = false; // binds m_sceneRTT as texture 1

    PostProcessPass(std::string n, irr::s32 mat, irr::video::IShaderConstantSetCallBack* cb, bool en = true)
        : name(std::move(n)), materialTypeIndex(mat), callback(cb), enabled(en) {}
};

class ShaderConstantSetCallBack : public irr::video::IShaderConstantSetCallBack
{
public:
    // Called by Irrlicht before OnSetConstants with the SMaterial about to be rendered.
    // Cache it here — IMaterialRendererServices::getMaterial() doesn't exist in this
    // Irrlicht version, so this is the only reliable way to read per-mesh material data.
    void OnSetMaterial(const irr::video::SMaterial& material) override { m_currentMaterial = material; }

    void OnSetConstants(irr::video::IMaterialRendererServices* services, irr::s32 userData) override;

    void setEnvMap(irr::video::ITexture* t) { m_envMap = t; }

    // Fog — edit directly from the editor, same pattern as bloom/sharpen callbacks
    float fogColor[3] = { 0.0f, 0.3f, 0.6f };
    float fogDensity  = 0.0f;   // 0 = fog off; ~0.02 = dense zone fog
    float fogStart    = 5.0f;   // view-space distance before fog begins

private:
    irr::video::SMaterial m_currentMaterial;
    irr::video::ITexture* m_envMap = nullptr;
};

// Callback for the barrel heat shader.
// Extends ShaderConstantSetCallBack with a single extra uniform — uHeatLevel —
// updated each frame by Weapon_Minigun.  When uHeatLevel is 0 the output is
// identical to phong_perpixel, so it costs nothing when the gun is cold.
class BarrelHeatShaderCallback : public ShaderConstantSetCallBack
{
public:
    void OnSetMaterial(const irr::video::SMaterial& material) override
    {
        ShaderConstantSetCallBack::OnSetMaterial(material);
    }

    void OnSetConstants(irr::video::IMaterialRendererServices* services, irr::s32 userData) override
    {
        // Upload all base PBR + lighting uniforms first.
        ShaderConstantSetCallBack::OnSetConstants(services, userData);
        // Then push the heat-specific uniforms.  fTime is already uploaded by the base class.
        services->setPixelShaderConstant("uHeatLevel",   &m_heatLevel,  1);
        services->setPixelShaderConstant("uBarrelZMin",  &m_barrelZMin, 1);
        services->setPixelShaderConstant("uBarrelZMax",  &m_barrelZMax, 1);
    }

    void setHeatLevel(float heat)                    { m_heatLevel   = heat; }
    float getHeatLevel() const                       { return m_heatLevel;  }
    void setBarrelZRange(float zMin, float zMax)     { m_barrelZMin  = zMin; m_barrelZMax = zMax; }

private:
    float m_heatLevel  = 0.0f;
    float m_barrelZMin = 0.0f;
    float m_barrelZMax = 1.0f;  // safe default: no divide-by-zero until init sets real bounds
};

// Callback for the terrain_blend shader.
// Extends ShaderConstantSetCallBack: suppresses the env map (slot 2 is the splat map)
// and uploads the five terrain-specific sampler indices + detail tiling.
// Splat map and detail textures are pre-bound by PropManager::enablePainting on the
// node's SMaterial TextureLayer[2..6]; the callback just tells the shader which units.
class TerrainShaderCallback : public ShaderConstantSetCallBack
{
public:
    void OnSetMaterial(const irr::video::SMaterial& material) override
    {
        ShaderConstantSetCallBack::OnSetMaterial(material);
    }

    void OnSetConstants(irr::video::IMaterialRendererServices* services, irr::s32 userData) override;

    // Global tiling applied to all terrain detail layers (A, B, C, D).
    float tiling[4] = { 8.f, 8.f, 8.f, 8.f };
};

// Dedicated callback for the water shader.
// Per-entity colors are encoded by RenderSystem into the material's AmbientColor
// (shallow) and DiffuseColor (deep, alpha = opacity) so the single global callback
// still delivers per-mesh values via the OnSetMaterial cache.
class WaterShaderCallback : public irr::video::IShaderConstantSetCallBack
{
public:
    void OnSetMaterial(const irr::video::SMaterial& material) override { m_currentMaterial = material; }

    void OnSetConstants(irr::video::IMaterialRendererServices* services, irr::s32 userData) override;

private:
    irr::video::SMaterial m_currentMaterial;
};


// Callback for the FXAA/blit post-process passes.
class FXAAShaderCallback : public irr::video::IShaderConstantSetCallBack
{
public:
    void OnSetMaterial(const irr::video::SMaterial&) override {}
    void OnSetConstants(irr::video::IMaterialRendererServices* services, irr::s32 userData) override;
};

// Extracts pixels above a luminance threshold for bloom.
class BloomBrightCallback : public irr::video::IShaderConstantSetCallBack
{
public:
    float threshold = 0.6f;
    void OnSetMaterial(const irr::video::SMaterial&) override {}
    void OnSetConstants(irr::video::IMaterialRendererServices* services, irr::s32) override;
};

// Separable Gaussian blur — set direction to (1,0) or (0,1) before registering.
class BloomBlurCallback : public irr::video::IShaderConstantSetCallBack
{
public:
    float direction[2] = { 1.0f, 0.0f };
    void OnSetMaterial(const irr::video::SMaterial&) override {}
    void OnSetConstants(irr::video::IMaterialRendererServices* services, irr::s32) override;
};

// Adds the blurred bloom buffer onto the original scene.
class BloomCompositeCallback : public irr::video::IShaderConstantSetCallBack
{
public:
    float strength = 1.5f;
    void OnSetMaterial(const irr::video::SMaterial&) override {}
    void OnSetConstants(irr::video::IMaterialRendererServices* services, irr::s32) override;
};

// Uncharted 2 filmic tonemapping + sRGB gamma in one pass.
class TonemapCallback : public irr::video::IShaderConstantSetCallBack
{
public:
    float exposure   = 1.0f;
    float whitePoint = 11.2f;
    void OnSetMaterial(const irr::video::SMaterial&) override {}
    void OnSetConstants(irr::video::IMaterialRendererServices* services, irr::s32) override;
};

// Unsharp-mask sharpening — applied after FXAA to recover softness.
class SharpenCallback : public irr::video::IShaderConstantSetCallBack
{
public:
    float strength = 0.4f;
    void OnSetMaterial(const irr::video::SMaterial&) override {}
    void OnSetConstants(irr::video::IMaterialRendererServices* services, irr::s32) override;
};

// Pixelation effect — snaps UVs to a coarser grid. pixelSize=1 is no effect.
class PixelateCallback : public irr::video::IShaderConstantSetCallBack
{
public:
    float pixelSize = 4.0f;
    void OnSetMaterial(const irr::video::SMaterial&) override {}
    void OnSetConstants(irr::video::IMaterialRendererServices* services, irr::s32) override;
};

// Saturation boost + multiplicative color tint + brightness lift (LDR, after tonemap).
class ColorGradeCallback : public irr::video::IShaderConstantSetCallBack
{
public:
    float saturation   = 1.0f;             // >1 = boosted saturation (Unreal mode: ~1.6)
    float brightness   = 0.0f;             // additive lift; 0 = no change
    float colorTint[3] = { 1.0f, 1.0f, 1.0f }; // multiplicative RGB tint
    void OnSetMaterial(const irr::video::SMaterial&) override {}
    void OnSetConstants(irr::video::IMaterialRendererServices* services, irr::s32) override;
};

// Posterize — stepped color banding simulating a limited palette (Unreal 1 8-bit look).
class PosterizeCallback : public irr::video::IShaderConstantSetCallBack
{
public:
    float levels   = 24.0f; // color steps per channel; lower = more banding
    float strength = 0.7f;  // 0=off, 1=full banding
    void OnSetMaterial(const irr::video::SMaterial&) override {}
    void OnSetConstants(irr::video::IMaterialRendererServices* services, irr::s32) override;
};

// Film grain — per-frame hash noise simulating analog film/CRT texture.
class FilmGrainCallback : public irr::video::IShaderConstantSetCallBack
{
public:
    float strength = 0.025f; // 0=off; 0.02-0.05 = subtle; >0.1 = heavy
    void OnSetMaterial(const irr::video::SMaterial&) override {}
    void OnSetConstants(irr::video::IMaterialRendererServices* services, irr::s32) override;
};

// No-op callback for the shadow depth pass — the depth shader only uses GL built-in
// matrices so no custom uniforms are needed.
class ShadowDepthCallback : public irr::video::IShaderConstantSetCallBack
{
public:
    void OnSetMaterial(const irr::video::SMaterial&) override {}
    void OnSetConstants(irr::video::IMaterialRendererServices*, irr::s32) override {}
};

// Samples an 8x8 grid of the scene and writes log-average luminance to a 1x1 RTT.
class LumMeasureCallback : public irr::video::IShaderConstantSetCallBack
{
public:
    void OnSetMaterial(const irr::video::SMaterial&) override {}
    void OnSetConstants(irr::video::IMaterialRendererServices* services, irr::s32) override
    {
        int slot = 0;
        services->setPixelShaderConstant("tScene", &slot, 1);
    }
};

class RenderManager
{
public:
    RenderManager& operator =(const RenderManager&) = delete;

    RenderManager(const std::string& name = std::string(), const std::string& args = std::string());
    ~RenderManager();

	void getResolutionFromMode(int aspect, int mode, int& width, int& height);
	
    void beginImGui();

    void draw(irr::f32 dt);

    // Buffer management - called by Engine::update() to control when buffers swap
    void swapGameBuffers() { m_renderBufferIndex = 1 - m_renderBufferIndex; }
    void clearGameWriteBuffer()
    {
        int writeBuffer = 1 - m_renderBufferIndex;
        m_textRenderableList[writeBuffer].clear();
        m_imageRenderableList[writeBuffer].clear();
        m_lineRenderableList[writeBuffer].clear();
        m_rectangleRenderableList[writeBuffer].clear();
    }

    void onWindowResizeEvent();

    void createShaderMaterial(
        std::string name, std::string fragment, std::string vertex,
        irr::video::E_MATERIAL_TYPE materialType = irr::video::EMT_SOLID,
        irr::video::E_PIXEL_SHADER_TYPE psVersion = irr::video::EPST_PS_2_0,
        irr::video::E_VERTEX_SHADER_TYPE vsVersion = irr::video::EVST_VS_2_0);

    void createDefaultShaders();

	// Gather up to maxLights closest lights to objectWorldPos from the active scene.
	// Lights inside the camera frustum are prioritized to reduce camera-rotation pop-in.
	static std::vector<LightUploadData> gatherClosestLights(
		irr::video::IVideoDriver* driver,
		irr::scene::ISceneManager* sceneManager,
		const irr::core::vector3df& objectWorldPos,
		int maxLights);

    irr::IrrlichtDevice*                 device()             const { return m_device; }
    irr::video::IVideoDriver*            driver()             const { return m_driver; }
    irr::scene::ISceneManager*           sceneManager()       const { return m_sceneManager; }
    irr::video::IGPUProgrammingServices* gpu()                const { return m_gpu; }
    irr::gui::IGUIEnvironment*           gui()                const { return m_gui; }
    ShaderConstantSetCallBack*           mainShaderCallback()  const { return m_shaderConstantCallBack; }
    BarrelHeatShaderCallback*            barrelHeatCallback()  const { return m_barrelHeatCallback; }
    TerrainShaderCallback*               terrainCallback()     const { return m_terrainCallback; }
	irr::scene::IMeshManipulator*        manipulator()  const { return m_manipulator; }
	IrrAssimp*                           assimp()       const { return m_assimpLoader; }

    void setAmbientLight(irr::video::SColor color = irr::video::SColor(255, 255, 255, 255)) const { m_sceneManager->setAmbientLight(color); }

    void initDefaultSkyDome(std::string texture = "content/texture/color/black.png");
    void removeDefaultSkyDome();
    void swapSkyDomeTexture(std::string texture = "content/texture/color/black.png");
    std::string getCurrentSkydomeTexture() const { return m_currentSkydomeTexture; }

    // Set an equirectangular (lat-long) texture as the ambient specular environment map.
    // All phong_perpixel and barrel_heat surfaces will reflect it based on their metallic/roughness.
    // Pass an empty string to disable.
    void setEnvMap(const std::string& texturePath);
    void clearEnvMap();

    irr::scene::ISceneNode *getNodeFromCursorPosition(irr::scene::ICameraSceneNode *camera = nullptr);
    irr::scene::ISceneNode *getNodeFromCursorPosition(irr::core::vector3df& outHit, irr::scene::ICameraSceneNode *camera = nullptr);
    irr::core::vector3df getPoint3DFromCursorPosition(irr::scene::ICameraSceneNode *camera = nullptr);

    irr::scene::ISceneNode* getNodeFromRaycast(irr::core::vector3df start, irr::core::vector3df end);
    irr::scene::ISceneNode* getNodeFromRaycast(irr::core::vector3df start, irr::core::vector3df end, irr::core::vector3df& outHit);
    irr::scene::ISceneNode* getNodeFromRaycast(irr::core::vector3df start, irr::core::vector3df end, irr::core::vector3df& outHit, irr::s32 idBitMask);

    float getRaycastLength(irr::core::vector3df startpoint, RaycastResultData& data) const { return hypot(hypot(startpoint.X - data.point.X, startpoint.Y - data.point.Y), startpoint.Z - data.point.Z); }

    bool getNodeTriangleTextureName(irr::scene::ISceneNode* node, const irr::core::triangle3df& tri, std::string& texname);
    std::string getMeshMaterialFromRay(irr::core::vector3df start, irr::core::vector3df end);
    std::string getMeshMaterialFromCameraRay(irr::scene::ICameraSceneNode *camera = nullptr);

    RaycastResultData raycastWorldPosition(irr::core::vector3df start, irr::core::vector3df end, bool excludeDebugNodes = false);

	static bool getAABBIntersection(irr::scene::ISceneNode* n1, irr::scene::ISceneNode* n2) { return n1->getTransformedBoundingBox().intersectsWithBox(n2->getTransformedBoundingBox()); }
	static bool calculate_bbox_collision(irr::scene::IAnimatedMeshSceneNode* sn1, irr::scene::IAnimatedMeshSceneNode* sn2) { return sn1->getTransformedBoundingBox().intersectsWithBox(sn2->getTransformedBoundingBox()); }

    void setNodeMesh(irr::scene::IAnimatedMesh* trimesh, irr::scene::IAnimatedMeshSceneNode* node, std::string file);

    void setPreviewScene(irr::scene::ISceneManager* sm, irr::video::ITexture* rtt);
    void clearPreviewScene();

    irr::core::vector3df getBoneLocalPosition(irr::scene::IAnimatedMeshSceneNode *mesh, int bone);
    irr::core::vector3df getBoneWorldPosition(irr::scene::IAnimatedMeshSceneNode *mesh, int bone);
    irr::core::vector3df getBoneLocalPosition(irr::scene::IAnimatedMeshSceneNode *mesh, std::string bone);
    irr::core::vector3df getBoneWorldPosition(irr::scene::IAnimatedMeshSceneNode *mesh, std::string bone);

	AnimatedSprite getAnimatedSprite(const std::string& name) { return m_AnimatedSpriteLoader.getAnimatedSprite(name); }

    void renderLine3D(const Line3D& line) { m_lineRenderableList[1 - m_renderBufferIndex].emplace_back(line); }

    void renderBox3D(const irr::core::aabbox3df& box, irr::video::SColor color)
    {
        using irr::core::vector3df;
        using irr::core::line3df;
        const vector3df& mn = box.MinEdge;
        const vector3df& mx = box.MaxEdge;
        auto& buf = m_lineRenderableList[1 - m_renderBufferIndex];
        // bottom face
        buf.emplace_back(Line3D(line3df({mn.X,mn.Y,mn.Z},{mx.X,mn.Y,mn.Z}), color));
        buf.emplace_back(Line3D(line3df({mx.X,mn.Y,mn.Z},{mx.X,mn.Y,mx.Z}), color));
        buf.emplace_back(Line3D(line3df({mx.X,mn.Y,mx.Z},{mn.X,mn.Y,mx.Z}), color));
        buf.emplace_back(Line3D(line3df({mn.X,mn.Y,mx.Z},{mn.X,mn.Y,mn.Z}), color));
        // top face
        buf.emplace_back(Line3D(line3df({mn.X,mx.Y,mn.Z},{mx.X,mx.Y,mn.Z}), color));
        buf.emplace_back(Line3D(line3df({mx.X,mx.Y,mn.Z},{mx.X,mx.Y,mx.Z}), color));
        buf.emplace_back(Line3D(line3df({mx.X,mx.Y,mx.Z},{mn.X,mx.Y,mx.Z}), color));
        buf.emplace_back(Line3D(line3df({mn.X,mx.Y,mx.Z},{mn.X,mx.Y,mn.Z}), color));
        // verticals
        buf.emplace_back(Line3D(line3df({mn.X,mn.Y,mn.Z},{mn.X,mx.Y,mn.Z}), color));
        buf.emplace_back(Line3D(line3df({mx.X,mn.Y,mn.Z},{mx.X,mx.Y,mn.Z}), color));
        buf.emplace_back(Line3D(line3df({mx.X,mn.Y,mx.Z},{mx.X,mx.Y,mx.Z}), color));
        buf.emplace_back(Line3D(line3df({mn.X,mn.Y,mx.Z},{mn.X,mx.Y,mx.Z}), color));
    }

    // Call from updateUI() — renders this frame, not subject to double-buffer swap timing
    void renderBox3DOverlay(const irr::core::aabbox3df& box, irr::video::SColor color)
    {
        using irr::core::vector3df;
        using irr::core::line3df;
        const vector3df& mn = box.MinEdge;
        const vector3df& mx = box.MaxEdge;
        auto& buf = m_overlayLineList;
        buf.emplace_back(Line3D(line3df({mn.X,mn.Y,mn.Z},{mx.X,mn.Y,mn.Z}), color));
        buf.emplace_back(Line3D(line3df({mx.X,mn.Y,mn.Z},{mx.X,mn.Y,mx.Z}), color));
        buf.emplace_back(Line3D(line3df({mx.X,mn.Y,mx.Z},{mn.X,mn.Y,mx.Z}), color));
        buf.emplace_back(Line3D(line3df({mn.X,mn.Y,mx.Z},{mn.X,mn.Y,mn.Z}), color));
        buf.emplace_back(Line3D(line3df({mn.X,mx.Y,mn.Z},{mx.X,mx.Y,mn.Z}), color));
        buf.emplace_back(Line3D(line3df({mx.X,mx.Y,mn.Z},{mx.X,mx.Y,mx.Z}), color));
        buf.emplace_back(Line3D(line3df({mx.X,mx.Y,mx.Z},{mn.X,mx.Y,mx.Z}), color));
        buf.emplace_back(Line3D(line3df({mn.X,mx.Y,mx.Z},{mn.X,mx.Y,mn.Z}), color));
        buf.emplace_back(Line3D(line3df({mn.X,mn.Y,mn.Z},{mn.X,mx.Y,mn.Z}), color));
        buf.emplace_back(Line3D(line3df({mx.X,mn.Y,mn.Z},{mx.X,mx.Y,mn.Z}), color));
        buf.emplace_back(Line3D(line3df({mx.X,mn.Y,mx.Z},{mx.X,mx.Y,mx.Z}), color));
        buf.emplace_back(Line3D(line3df({mn.X,mn.Y,mx.Z},{mn.X,mx.Y,mx.Z}), color));
    }

    // Call from updateUI() — same-frame rendering, not subject to double-buffer timing
    void renderLine3DOverlay(const Line3D& line)
    {
        m_overlayLineList.emplace_back(line);
    }

    void renderText2D(
        irr::core::stringw text,
        irr::gui::IGUIFont* font = g_DefaultTextRenderableFontSm,
        irr::core::rect<irr::s32> position = irr::core::rect<irr::s32>(0, 0, 0, 0),
        irr::video::SColor color = irr::video::SColor(255, 255, 255, 255),
        bool hcenter = false,
        bool vcenter = false) { m_textRenderableList[1 - m_renderBufferIndex].emplace_back(Text2D(font, text, position, color, hcenter, vcenter)); }
    void renderText2D(
        irr::core::stringw text,
        TEXT_DEFAULT_FONT font,
        irr::core::rect<irr::s32> position = irr::core::rect<irr::s32>(0, 0, 0, 0),
        irr::video::SColor color = irr::video::SColor(255, 255, 255, 255),
        bool hcenter = false,
        bool vcenter = false);

	void renderText2D(
		irr::core::stringw text,
		irr::gui::IGUIFont* font = g_DefaultTextRenderableFontSm,
		irr::core::vector2di position = irr::core::vector2di(0, 0),
		irr::video::SColor color = irr::video::SColor(255, 255, 255, 255),
		bool hcenter = false,
		bool vcenter = false) {
		m_textRenderableList[1 - m_renderBufferIndex].emplace_back(Text2D(font, text, irr::core::rect<irr::s32>(position.X, position.Y, 0, 0), color, hcenter, vcenter));
	}
	void renderText2D(
		irr::core::stringw text,
		TEXT_DEFAULT_FONT font,
		irr::core::vector2di position = irr::core::vector2di(0, 0),
		irr::video::SColor color = irr::video::SColor(255, 255, 255, 255),
		bool hcenter = false,
		bool vcenter = false);

    void renderImage2D(
        irr::video::ITexture* image,
        irr::core::vector2di position = irr::core::vector2di(0, 0),
        irr::video::SColor color = irr::video::SColor(255, 255, 255, 255),
        bool use_alpha = true,
        bool use_center = false) { m_imageRenderableList[1 - m_renderBufferIndex].emplace_back(Image2D(image, position, color, use_alpha, use_center)); }

    void renderImage2DScaled(
        irr::video::ITexture* image,
        irr::core::rect<irr::s32> destRect,
        irr::video::SColor color = irr::video::SColor(255, 255, 255, 255),
        bool use_alpha = true) { m_imageRenderableList[1 - m_renderBufferIndex].emplace_back(Image2D(image, destRect, color, use_alpha)); }

    void renderRectangle2D(
        irr::core::rect<irr::s32> position,
        irr::video::SColor color = irr::video::SColor(255, 255, 255, 255)) { m_rectangleRenderableList[1 - m_renderBufferIndex].emplace_back(Rectangle2D(position, color)); }

    static irr::core::vector3df GetInVector(irr::scene::ISceneNode* node);
    static irr::core::vector3df GetLeftVector(irr::scene::ISceneNode* node);
    static irr::core::vector3df GetUpVector(irr::scene::ISceneNode* node);

    RenderConfiguration getConfiguration() const { return m_configuration; }
	void saveConfiguration(RenderConfiguration& configuration);

    static RenderManager* Get() { return s_Instance; }

    void registerViewmodelNode(irr::scene::ISceneNode* node)   { m_viewmodelNodes.push_back(node); }
    void unregisterViewmodelNode(irr::scene::ISceneNode* node)
    {
        auto it = std::find(m_viewmodelNodes.begin(), m_viewmodelNodes.end(), node);
        if (it != m_viewmodelNodes.end()) m_viewmodelNodes.erase(it);
    }

    // Nodes registered here are hidden during the main scene pass so they are
    // excluded from bloom/tonemap, then drawn directly to the backbuffer after
    // the post-process chain. Use this for debug meshes and debug billboards.
    void registerDebugNode(irr::scene::ISceneNode* node)   { m_debugNodes.push_back(node); }
    void unregisterDebugNode(irr::scene::ISceneNode* node)
    {
        auto it = std::find(m_debugNodes.begin(), m_debugNodes.end(), node);
        if (it != m_debugNodes.end()) m_debugNodes.erase(it);
    }


    // Additive/transparent effect nodes (muzzle flashes, particle systems) that were
    // tuned for LDR. Like debug nodes they are excluded from the HDR scene capture and
    // composited onto the tonemapped backbuffer, preserving their intended appearance.
    void registerLDREffectNode(irr::scene::ISceneNode* node)   { m_ldrEffectNodes.push_back(node); }
    void unregisterLDREffectNode(irr::scene::ISceneNode* node)
    {
        auto it = std::find(m_ldrEffectNodes.begin(), m_ldrEffectNodes.end(), node);
        if (it != m_ldrEffectNodes.end()) m_ldrEffectNodes.erase(it);
    }

    void registerPostProcessPass(PostProcessPass pass) { m_postProcessPasses.push_back(std::move(pass)); }
    void setPostProcessPassEnabled(const std::string& name, bool enabled)
    {
        for (auto& pass : m_postProcessPasses)
            if (pass.name == name) { pass.enabled = enabled; return; }
    }
    bool getPostProcessPassEnabled(const std::string& name) const
    {
        for (const auto& pass : m_postProcessPasses)
            if (pass.name == name) return pass.enabled;
        return false;
    }
    void setBloomEnabled(bool enabled)
    {
        setPostProcessPassEnabled("bloom_bright",    enabled);
        setPostProcessPassEnabled("bloom_blur_h",    enabled);
        setPostProcessPassEnabled("bloom_blur_v",    enabled);
        setPostProcessPassEnabled("bloom_composite", enabled);
    }
    BloomBrightCallback*    bloomBrightCallback()    const { return m_bloomBrightCallback; }
    BloomCompositeCallback* bloomCompositeCallback() const { return m_bloomCompositeCallback; }
    TonemapCallback*        tonemapCallback()        const { return m_tonemapCallback; }
    SharpenCallback*        sharpenCallback()        const { return m_sharpenCallback; }
    PixelateCallback*       pixelateCallback()       const { return m_pixelateCallback; }
    ColorGradeCallback*     colorGradeCallback()     const { return m_colorGradeCallback; }
    PosterizeCallback*      posterizeCallback()      const { return m_posterizeCallback; }
    FilmGrainCallback*      filmGrainCallback()      const { return m_filmGrainCallback; }

    void setTonemapEnabled(bool enabled)     { setPostProcessPassEnabled("tonemap",     enabled); }
    void setSharpenEnabled(bool enabled)     { setPostProcessPassEnabled("sharpen",     enabled); }
    void setPixelateEnabled(bool enabled)    { setPostProcessPassEnabled("pixelate",    enabled); }
    void setColorGradeEnabled(bool enabled)  { setPostProcessPassEnabled("colorgrade",  enabled); }
    void setPosterizeEnabled(bool enabled)   { setPostProcessPassEnabled("posterize",   enabled); }
    void setFilmGrainEnabled(bool enabled)   { setPostProcessPassEnabled("filmgrain",   enabled); }

    bool isPixelateEnabled()   const { return getPostProcessPassEnabled("pixelate");   }
    bool isColorGradeEnabled() const { return getPostProcessPassEnabled("colorgrade"); }
    bool isPosterizeEnabled()  const { return getPostProcessPassEnabled("posterize");  }
    bool isFilmGrainEnabled()  const { return getPostProcessPassEnabled("filmgrain");  }

    // Shadow mapping — directional shadow map driven by the first ELT_DIRECTIONAL light.
    void  setShadowBias(float b)     { m_shadowBias = b; }
    float getShadowBias()    const   { return m_shadowBias; }
    bool  hasShadowLight()   const   { return m_hasShadowLight; }
    irr::video::ITexture* getShadowMapRTT()       const { return m_shadowMapRTT; }
    const irr::core::matrix4& getLightProjView()  const { return m_lightProjView; }

    // Adaptive (auto) exposure — eye-adaptation effect.
    // When enabled, manual tonemapCallback()->exposure is overridden each frame.
    void setAutoExposure(bool enabled)                        { m_autoExposure = enabled; }
    bool getAutoExposure() const                              { return m_autoExposure; }
    void setAdaptSpeed(float brighten, float darken)          { m_adaptSpeedUp = brighten; m_adaptSpeedDown = darken; }
    void setExposureRange(float minExp, float maxExp)         { m_minExposure = minExp; m_maxExposure = maxExp; }
    float getAdaptedExposure() const                          { return m_adaptedExposure; }

private:
    static RenderManager* s_Instance;

	void updateDebugRender();

    // Sorted transparent pass - called from draw() after opaque scene
    void drawTransparentPass();

    bool m_drawDebugStatistics;

    irr::scene::ICameraSceneNode* m_defaultCamera;

    RenderConfiguration m_configuration;

    irr::SIrrlichtCreationParameters m_irrlichtParams;

    irr::IrrlichtDevice* m_device;
    irr::gui::IGUIEnvironment* m_gui;
    irr::video::IVideoDriver* m_driver;
    irr::video::IGPUProgrammingServices* m_gpu;
	irr::scene::IMeshManipulator* m_manipulator;
    irr::scene::ISceneManager* m_sceneManager;

    ShaderConstantSetCallBack* m_shaderConstantCallBack;
    WaterShaderCallback*       m_waterCallback;
    BarrelHeatShaderCallback*  m_barrelHeatCallback;
    TerrainShaderCallback*     m_terrainCallback;

    IrrImGuiEventReceiver m_imguiEventReceiver;

    irr::video::SColor m_backgroundColor;

    std::string m_currentSkydomeTexture;
    irr::scene::ISceneNode* m_defaultSkyDome;

    // Double-buffered 2D renderable lists to avoid race conditions with decoupled rendering
    std::vector<Text2D> m_textRenderableList[2];
    std::vector<Image2D> m_imageRenderableList[2];
    std::vector<Line3D> m_lineRenderableList[2];
    std::vector<Rectangle2D> m_rectangleRenderableList[2];
    int m_renderBufferIndex = 0;  // Which buffer to render from (0 or 1)

    // Single-buffer overlay lines — written during updateUI(), consumed and cleared each draw()
    std::vector<Line3D> m_overlayLineList;

	irr::video::ITexture* m_effectRenderTarget;

    // Entity Builder preview — independent scene manager rendered to RTT each frame
    irr::scene::ISceneManager* m_previewSM  = nullptr;
    irr::video::ITexture*      m_previewRTT = nullptr;

	AnimatedSpriteLoader m_AnimatedSpriteLoader;

	IrrAssimp* m_assimpLoader;

    // Viewmodel nodes are rendered in a separate pass after clearing the depth
    // buffer so they never clip into world geometry.
    std::vector<irr::scene::ISceneNode*> m_viewmodelNodes;

    // Debug nodes are hidden during the main scene pass (excluded from bloom/
    // tonemap) and drawn directly to the backbuffer after post-processing.
    std::vector<irr::scene::ISceneNode*> m_debugNodes;

    // LDR-composited effect nodes — same hide/restore/render-after-PP pattern as
    // debug nodes, but for gameplay effects (muzzle flashes, SPARK particles).
    std::vector<irr::scene::ISceneNode*> m_ldrEffectNodes;

    // GL extension function pointers for depth blitting (GL 3.0 / ARB_framebuffer_object).
    // Loaded once in recreatePostProcessRTTs(); null if the driver doesn't support them.
    void* m_fnBindFramebuffer = nullptr;
    void* m_fnBlitFramebuffer = nullptr;
    // FBO ID of the scene RTT, captured each frame after setRenderTarget(m_sceneRTT).
    // Used to blit world depth into the backbuffer before LDR effect rendering.
    int   m_sceneFBOId        = -1;

    // -------------------------------------------------------------------------
    // Post-process pipeline
    // -------------------------------------------------------------------------
    void recreatePostProcessRTTs(irr::u32 width, irr::u32 height);
    void createShadowResources();
    void drawShadowPass();
    void runPostProcessChain();
    void drawFullscreenQuad();
    void measureAndAdaptExposure(irr::f32 dt);
    irr::video::ITexture* m_sceneRTT      = nullptr;
    irr::video::ITexture* m_ppRTT[2]      = { nullptr, nullptr };
    irr::video::ITexture* m_lumRTT        = nullptr;
    irr::s32              m_lumMaterial  = -1;
    irr::s32              m_blitMat      = -1;

    std::vector<PostProcessPass> m_postProcessPasses;
    FXAAShaderCallback*          m_blitCallback            = nullptr;
    FXAAShaderCallback*          m_fxaaCallback            = nullptr;
    BloomBrightCallback*         m_bloomBrightCallback     = nullptr;
    BloomBlurCallback*           m_bloomBlurHCallback      = nullptr;
    BloomBlurCallback*           m_bloomBlurVCallback      = nullptr;
    BloomCompositeCallback*      m_bloomCompositeCallback  = nullptr;
    TonemapCallback*             m_tonemapCallback         = nullptr;
    SharpenCallback*             m_sharpenCallback         = nullptr;
    PixelateCallback*            m_pixelateCallback        = nullptr;
    ColorGradeCallback*          m_colorGradeCallback      = nullptr;
    PosterizeCallback*           m_posterizeCallback       = nullptr;
    FilmGrainCallback*           m_filmGrainCallback       = nullptr;
    LumMeasureCallback*          m_lumMeasureCallback      = nullptr;

    // Shadow mapping
    irr::video::ITexture*          m_shadowMapRTT   = nullptr;
    irr::s32                       m_shadowDepthMat = -1;
    ShadowDepthCallback*           m_shadowDepthCallback = nullptr;
    irr::scene::ICameraSceneNode*  m_shadowCamera   = nullptr;
    irr::core::matrix4             m_lightProjView;
    bool                           m_hasShadowLight = false;
    float                          m_shadowBias     = 0.002f;

    // Adaptive exposure state
    float m_adaptedExposure  = 1.0f;
    float m_adaptSpeedUp     = 3.0f;  // how fast exposure rises  (dark scene entered)
    float m_adaptSpeedDown   = 1.0f;  // how fast exposure drops  (bright scene entered)
    float m_minExposure      = 0.1f;
    float m_maxExposure      = 10.0f;
    bool  m_autoExposure     = false;
};
