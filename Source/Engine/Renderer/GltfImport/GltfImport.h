#ifndef GLTFIMPORT_H
#define GLTFIMPORT_H

// Public interface of the fastgltf-based glTF/GLB import backend.
//
// IMPORTANT: This header must remain plain C++14 — it is included by C++14
// translation units (RenderManager, RenderSystem, ...). All fastgltf usage
// lives in GltfImport.cpp, which is compiled as C++17 via a per-file
// LanguageStandard override in the .vcxproj. Never add fastgltf types,
// std::optional/std::variant/std::string_view or any other C++17-only
// constructs to this header.

#include <string>

#include <IAnimatedMesh.h>
#include <path.h>

namespace irr { namespace scene { class ISceneManager; } }

class GltfImport
{
public:
    explicit GltfImport(irr::scene::ISceneManager* smgr);
    ~GltfImport();

    /*  Get a mesh loaded with fastgltf.
        Like IrrAssimp::getMesh: checks the scene manager's mesh cache first,
        loads and caches on a miss. Returns nullptr on failure (see getError).
        Also writes a .anim clip-range file next to the mesh if animations
        are present and no .anim file exists yet (same as the Assimp path). */
    irr::scene::IAnimatedMesh* getMesh(const irr::io::path& path);

    // Error of the last load. Empty string if no error.
    const std::string& getError() const { return m_error; }

    // Check if the file has a loadable extension (.gltf / .glb)
    static bool isLoadable(const irr::io::path& path);

private:
    irr::scene::ISceneManager* m_sceneManager;
    std::string m_error;
};

#endif // GLTFIMPORT_H
