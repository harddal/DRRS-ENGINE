#pragma once

#include <vector>
#include <string>
#include <cstdint>

#include "irrlicht.h"

#include "cereal/cereal.hpp"
#include "cereal/archives/xml.hpp"
#include "cereal/types/vector.hpp"
#include "cereal/types/string.hpp"

// Forward declarations
struct MeshLightmap;
struct SplatMap;
namespace physx { class PxRigidStatic; }

// ---------------------------------------------------------------------------
// PropBufferShader
// Overrides the default shader for a specific material buffer index.
// ---------------------------------------------------------------------------
struct PropBufferShader
{
    uint32_t    bufferIndex = 0;
    std::string shaderName;

    template<class Archive>
    void serialize(Archive& ar)
    {
        ar(CEREAL_NVP(bufferIndex), CEREAL_NVP(shaderName));
    }
};

// ---------------------------------------------------------------------------
// StaticProp
// A lightweight non-ECS decorative scene object with its own shader and
// optional lightmap participation.  No gameplay interaction.
// ---------------------------------------------------------------------------
struct StaticProp
{
    uint32_t    id     = 0;
    std::string mesh;                                   // content-relative path
    std::vector<std::string>     textures;              // per slot, same as MeshComponent
    std::string defaultShader    = "phong_perpixel";    // ShaderMaterialManager key
    std::vector<PropBufferShader> bufferShaderOverrides; // optional per-buffer overrides

    // Transform (world space)
    irr::core::vector3df position = { 0.f, 0.f, 0.f };
    irr::core::vector3df rotation = { 0.f, 0.f, 0.f };
    irr::core::vector3df scale    = { 1.f, 1.f, 1.f };

    // Lightmap control
    bool receivesLightmap = false; // if true, this prop is baked by LightmapBaker
    bool castShadows      = true;  // contributes shadow geometry even if not baked

    // Collision
    bool hasCollision      = false; // if true, a PxRigidStatic is created for this prop
    bool useConvexCollision = false; // false = exact triangle mesh, true = convex hull

    // Vegetation
    bool  isVegetation  = false;   // disables backface culling on all material slots
    float cullDistance  = 0.0f;    // world-unit radius beyond which node is hidden; 0 = disabled

    // PBR material
    float roughness = 0.0f;  // maps to Shininess on all material buffers
    float emissive  = 0.0f;  // maps to SpecularColor alpha [0-1] on all material buffers

    // Crystal shader params (only active when defaultShader == "crystal")
    // Encoded into Shininess (refraction), SpecularColor.alpha (transparency),
    // and AmbientColor (color tint) so the global CrystalShaderCallback can read them per-mesh.
    float crystalRefraction   = 0.02f; // UV distortion strength (0=no distortion)
    float crystalTransparency = 0.5f;  // 0=fully see-through, 1=fully opaque tint
    float crystalGlow         = 0.6f;  // Fresnel rim brightness (maps to MaterialTypeParam)
    float crystalShimmer      = 1.0f;  // normal map animation speed (maps to MaterialTypeParam2)
    float crystalColorR       = 1.0f;  // RGB tint applied to the composite (white = no tint)
    float crystalColorG       = 1.0f;
    float crystalColorB       = 1.0f;

    // Texture painting — serialized fields
    bool        isPaintable      = false;
    std::string splatMapPath;                    // content-relative path for save/load
    std::string detailTextures[4];               // paths for blend layers A-D
    float       detailTiling[4] = {8,8,8,8};    // UV tiling per layer

    // Runtime — NOT serialized
    MeshLightmap*                          lightmap      = nullptr;
    SplatMap*                              splatMap      = nullptr;
    irr::scene::IAnimatedMesh*             trimesh       = nullptr;
    irr::scene::IAnimatedMeshSceneNode*    node          = nullptr;
    physx::PxRigidStatic*                  physicsActor  = nullptr;

    template<class Archive>
    void save(Archive& ar) const
    {
        ar(CEREAL_NVP(id),
           CEREAL_NVP(mesh),
           CEREAL_NVP(textures),
           CEREAL_NVP(defaultShader),
           CEREAL_NVP(bufferShaderOverrides),
           CEREAL_NVP(position.X), CEREAL_NVP(position.Y), CEREAL_NVP(position.Z),
           CEREAL_NVP(rotation.X), CEREAL_NVP(rotation.Y), CEREAL_NVP(rotation.Z),
           CEREAL_NVP(scale.X),    CEREAL_NVP(scale.Y),    CEREAL_NVP(scale.Z),
           CEREAL_NVP(receivesLightmap),
           CEREAL_NVP(castShadows),
           CEREAL_NVP(hasCollision),
           CEREAL_NVP(useConvexCollision),
           CEREAL_NVP(isVegetation),
           CEREAL_NVP(cullDistance),
           CEREAL_NVP(isPaintable),
           CEREAL_NVP(splatMapPath),
           CEREAL_NVP(detailTextures[0]), CEREAL_NVP(detailTextures[1]),
           CEREAL_NVP(detailTextures[2]), CEREAL_NVP(detailTextures[3]),
           CEREAL_NVP(detailTiling[0]),   CEREAL_NVP(detailTiling[1]),
           CEREAL_NVP(detailTiling[2]),   CEREAL_NVP(detailTiling[3]),
           CEREAL_NVP(roughness),
           CEREAL_NVP(emissive),
           CEREAL_NVP(crystalRefraction),
           CEREAL_NVP(crystalTransparency),
           CEREAL_NVP(crystalGlow),
           CEREAL_NVP(crystalShimmer),
           CEREAL_NVP(crystalColorR),
           CEREAL_NVP(crystalColorG),
           CEREAL_NVP(crystalColorB));
    }

    template<class Archive>
    void load(Archive& ar)
    {
        ar(CEREAL_NVP(id),
           CEREAL_NVP(mesh),
           CEREAL_NVP(textures),
           CEREAL_NVP(defaultShader),
           CEREAL_NVP(bufferShaderOverrides),
           CEREAL_NVP(position.X), CEREAL_NVP(position.Y), CEREAL_NVP(position.Z),
           CEREAL_NVP(rotation.X), CEREAL_NVP(rotation.Y), CEREAL_NVP(rotation.Z),
           CEREAL_NVP(scale.X),    CEREAL_NVP(scale.Y),    CEREAL_NVP(scale.Z),
           CEREAL_NVP(receivesLightmap),
           CEREAL_NVP(castShadows),
           CEREAL_NVP(hasCollision),
           CEREAL_NVP(useConvexCollision),
           CEREAL_NVP(isVegetation),
           CEREAL_NVP(cullDistance),
           CEREAL_NVP(isPaintable),
           CEREAL_NVP(splatMapPath),
           CEREAL_NVP(detailTextures[0]), CEREAL_NVP(detailTextures[1]),
           CEREAL_NVP(detailTextures[2]), CEREAL_NVP(detailTextures[3]),
           CEREAL_NVP(detailTiling[0]),   CEREAL_NVP(detailTiling[1]),
           CEREAL_NVP(detailTiling[2]),   CEREAL_NVP(detailTiling[3]));
        try { ar(CEREAL_NVP(roughness), CEREAL_NVP(emissive)); }
        catch (cereal::Exception&) {}
        try { ar(CEREAL_NVP(crystalRefraction), CEREAL_NVP(crystalTransparency)); }
        catch (cereal::Exception&) {}
        try { ar(CEREAL_NVP(crystalGlow), CEREAL_NVP(crystalShimmer)); }
        catch (cereal::Exception&) {}
        try { ar(CEREAL_NVP(crystalColorR), CEREAL_NVP(crystalColorG), CEREAL_NVP(crystalColorB)); }
        catch (cereal::Exception&) {}
    }
};

// ---------------------------------------------------------------------------
// VegetationBatch
// A single merged scene node covering all vegetation instances that share a
// mesh path.  Owned by PropManager; exists only while batched mode is active.
// ---------------------------------------------------------------------------
struct VegetationBatch
{
    std::string                  meshPath;
    irr::scene::IMeshSceneNode*  node = nullptr;
};

// ---------------------------------------------------------------------------
// PropManager
// Singleton that owns all static props in the current scene.
// Manages scene nodes directly — no ECS involvement.
// ---------------------------------------------------------------------------
class PropManager
{
public:
    PropManager();
    ~PropManager();

    static PropManager* Get() { return s_Instance; }

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    // Create a scene node for the prop, assign shaders, and add to list.
    // Returns the prop's id, or UINT32_MAX on failure.
    uint32_t addProp(StaticProp prop);

    // Destroy the scene node and remove from list.
    void removeProp(uint32_t id);

    // Destroy all props and their scene nodes.  Called alongside killAllEntities.
    void clearAll();

    // Per-frame: toggle visibility of vegetation props based on camera distance.
    void update();

    // -----------------------------------------------------------------------
    // Access
    // -----------------------------------------------------------------------
    StaticProp*                       getProp(uint32_t id);
    const std::vector<StaticProp>&    getAllProps() const { return m_props; }

    // Sync scene node position/rotation/scale from the prop's stored data.
    // Call after editing transform in the editor.
    void applyTransform(uint32_t id);

    // Re-assign shader materials on the scene node from the prop's data.
    // Call after editing shader fields in the editor.
    void applyShaders(uint32_t id);

    // Rebuild (or destroy) the PhysX actor from the prop's current collision settings.
    // Call after toggling hasCollision or useConvexCollision in the editor.
    void applyCollision(uint32_t id);

    // Return the prop whose scene node matches the given pointer, or nullptr.
    // Used by SceneInteractionManager for click-selection.
    StaticProp* getPropFromNode(irr::scene::ISceneNode* node);

    // -----------------------------------------------------------------------
    // Vegetation batching
    // -----------------------------------------------------------------------

    // Merge all vegetation props that share a mesh path into single scene
    // nodes and hide the individual nodes.  Call when entering play mode.
    void setVegetationBatched(bool batched);

    bool isVegetationBatched() const { return m_batchedMode; }

    // -----------------------------------------------------------------------
    // Texture painting
    // -----------------------------------------------------------------------

    // Enable splat-map texture painting on a prop.
    // Generates UV1 (via LightmapBaker::unwrapMesh) if the mesh lacks it,
    // creates a blank splat map, and switches the prop to the terrain_blend shader.
    // splatResolution should be a power of two (256, 512, 1024).
    void enablePainting(uint32_t id, int splatResolution = 512);

    // Write all dirty splat maps to their savePath on disk.
    void saveSplatMaps();

    // Load splat maps from disk for all paintable props after scene load.
    void loadSplatMaps();

    // -----------------------------------------------------------------------
    // Lightmap integration
    // -----------------------------------------------------------------------

    // Entry used by LightmapBaker — a prop pointer plus its cached world transform.
    struct BakeTarget
    {
        StaticProp*         prop;
        irr::core::matrix4  worldTransform;
    };

    // Return all props that have receivesLightmap == true.
    std::vector<BakeTarget> getBakeTargets();

    // In-memory representation of one prop's baked lightmap files.
    struct PropLightmapFiles
    {
        uint32_t             propId;
        std::vector<uint8_t> pngBytes;
        std::vector<uint8_t> uvmeshBytes;
    };

    // Encode baked lightmaps for all props to PNG + uvmesh byte buffers.
    // tempDir must be writable; temporary PNG files are deleted on return.
    std::vector<PropLightmapFiles> collectPropLightmapFiles(
        irr::video::IVideoDriver* driver,
        const std::string&        tempDir);

    // Read and apply prop lightmaps from the currently mounted ZIP archive.
    void loadPropLightmaps(
        irr::io::IFileSystem*     fs,
        irr::video::IVideoDriver* driver);

    // -----------------------------------------------------------------------
    // Serialization  (called by WorldManager during import/export)
    // -----------------------------------------------------------------------
    void serialize(cereal::XMLOutputArchive& ar);
    void deserialize(cereal::XMLInputArchive& ar);

private:
    static PropManager* s_Instance;

    std::vector<StaticProp>        m_props;
    uint32_t                       m_nextId = 0;

    std::vector<VegetationBatch>   m_vegetationBatches;
    bool                           m_batchedMode = false;

    void rebuildVegetationBatches();

    // Spawn an IAnimatedMeshSceneNode for an already-configured prop.
    void spawnNode(StaticProp& prop);

    // Apply all shader assignments from prop data to the live scene node.
    void applyNodeShaders(StaticProp& prop);
};
