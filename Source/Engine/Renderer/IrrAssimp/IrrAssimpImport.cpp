#include "IrrAssimpImport.h"

#include <cstdio>
#include <regex>
#include <sstream>
#include <spdlog/spdlog.h>
#include <ISceneManager.h>
#include <IVideoDriver.h>
#include <IMeshManipulator.h>

using namespace irr;

IrrAssimpImport::IrrAssimpImport(scene::ISceneManager* smgr) :
    m_sceneManager(smgr),
    m_fileSystem(smgr->getFileSystem()),
    m_assimpScene(0),
    m_irrMesh(0)
{
    //ctor
}

IrrAssimpImport::~IrrAssimpImport()
{
    //dtor
}

core::stringc assimpToIrrString(aiString str)
{
    return core::stringc(str.C_Str());
}

core::vector3df assimpToIrrVector3(const aiVector3D& vect)
{
    return core::vector3df(vect.x, vect.y, vect.z);
}

core::vector2df assimpToIrrVector2(const aiVector3D& vect)
{
    return core::vector2df(vect.x, vect.y);
}

core::quaternion assimpToIrrQuaternion(const aiQuaternion& quat)
{
    return core::quaternion(quat.x, quat.y, quat.z, -quat.w);
}

core::matrix4 assimpToIrrMatrix(aiMatrix4x4 assimpMatrix)
{
    core::matrix4 irrMatrix;

    irrMatrix[0] = assimpMatrix.a1;
    irrMatrix[1] = assimpMatrix.b1;
    irrMatrix[2] = assimpMatrix.c1;
    irrMatrix[3] = assimpMatrix.d1;

    irrMatrix[4] = assimpMatrix.a2;
    irrMatrix[5] = assimpMatrix.b2;
    irrMatrix[6] = assimpMatrix.c2;
    irrMatrix[7] = assimpMatrix.d2;

    irrMatrix[8] = assimpMatrix.a3;
    irrMatrix[9] = assimpMatrix.b3;
    irrMatrix[10] = assimpMatrix.c3;
    irrMatrix[11] = assimpMatrix.d3;

    irrMatrix[12] = assimpMatrix.a4;
    irrMatrix[13] = assimpMatrix.b4;
    irrMatrix[14] = assimpMatrix.c4;
    irrMatrix[15] = assimpMatrix.d4;

    return irrMatrix;
}

video::SColor assimpToIrrColor4(const aiColor4D& color)
{
    return video::SColor(static_cast<u32>(color.a * 255), static_cast<u32>(color.r * 255), static_cast<u32>(color.g * 255), static_cast<u32>(color.b * 255));
}


scene::ISkinnedMesh::SJoint* IrrAssimpImport::findJoint(const core::stringc jointName)
{
    for (unsigned int i = 0; i < m_irrMesh->getJointCount(); ++i)
    {
        if (core::stringc(m_irrMesh->getJointName(i)) == jointName)
            return m_irrMesh->getAllJoints()[i];
    }
    //std::cout << "Error, no joint" << std::endl;
    return 0;
}

aiNode* IrrAssimpImport::findNode(const aiString jointName)
{
    if (m_assimpScene->mRootNode->mName == jointName)
        return m_assimpScene->mRootNode;

    return m_assimpScene->mRootNode->FindNode(jointName);
}

void IrrAssimpImport::createNode(const aiNode* node)
{
    scene::ISkinnedMesh::SJoint* jointParent = 0;

    if (node->mParent != 0)
    {
        jointParent = findJoint(assimpToIrrString(node->mParent->mName));
    }

    scene::ISkinnedMesh::SJoint* joint = m_irrMesh->addJoint(jointParent);
    joint->Name = assimpToIrrString(node->mName);
    joint->LocalMatrix = assimpToIrrMatrix(node->mTransformation);

    if (jointParent)
        joint->GlobalMatrix = jointParent->GlobalMatrix * joint->LocalMatrix;
    else
        joint->GlobalMatrix = joint->LocalMatrix;

    for (unsigned int i = 0; i < node->mNumMeshes; ++i)
    {
        joint->AttachedMeshes.push_back(node->mMeshes[i]);
    }

    for (unsigned int i = 0; i < node->mNumChildren; ++i)
    {
        aiNode* childNode = node->mChildren[i];
        createNode(childNode);
    }
}

video::ITexture* IrrAssimpImport::getTexture(core::stringc path, core::stringc fileDir)
{
    video::ITexture* texture = 0;

    // Embedded texture — Assimp uses '*<index>' as the path
    if (path.size() > 0 && path[0] == '*')
    {
        unsigned int index = (unsigned int)atoi(path.c_str() + 1);
        if (index < m_assimpScene->mNumTextures)
        {
            const aiTexture* embTex = m_assimpScene->mTextures[index];
            core::stringc texName = m_filePath;
            texName += "_embedded_";
            texName += (int)index;

            texture = m_sceneManager->getVideoDriver()->findTexture(texName.c_str());
            if (texture)
                return texture;

            if (embTex->mHeight == 0)
            {
                // Compressed data (PNG, JPG, etc.)
                io::IReadFile* memFile = m_fileSystem->createMemoryReadFile(
                    embTex->pcData, embTex->mWidth, texName.c_str());
                if (memFile)
                {
                    texture = m_sceneManager->getVideoDriver()->getTexture(memFile);
                    memFile->drop();
                }
            }
            else
            {
                // Uncompressed BGRA pixels
                video::IImage* image = m_sceneManager->getVideoDriver()->createImageFromData(
                    video::ECF_A8R8G8B8,
                    core::dimension2d<u32>(embTex->mWidth, embTex->mHeight),
                    embTex->pcData, false, false);
                if (image)
                {
                    texture = m_sceneManager->getVideoDriver()->addTexture(texName.c_str(), image);
                    image->drop();
                }
            }
        }
        return texture;
    }

    if (m_fileSystem->existFile(path.c_str()))
        texture = m_sceneManager->getVideoDriver()->getTexture(path.c_str());
    else if (m_fileSystem->existFile(fileDir + "/" + path.c_str()))
        texture = m_sceneManager->getVideoDriver()->getTexture(fileDir + "/" + path.c_str());
    else if (m_fileSystem->existFile(fileDir + "/" + m_fileSystem->getFileBasename(path.c_str())))
        texture = m_sceneManager->getVideoDriver()->getTexture(fileDir + "/" + m_fileSystem->getFileBasename(path.c_str()));

    return texture;
}


void IrrAssimpImport::collectVisibleMeshes(const aiNode* node, core::array<bool>& visible)
{
    bool nodeVisible = true;
    if (node->mMetaData)
        node->mMetaData->Get("Visibility", nodeVisible);

    if (nodeVisible)
    {
        for (unsigned int i = 0; i < node->mNumMeshes; ++i)
        {
            if (node->mMeshes[i] < visible.size())
                visible[node->mMeshes[i]] = true;
        }
    }

    for (unsigned int i = 0; i < node->mNumChildren; ++i)
        collectVisibleMeshes(node->mChildren[i], visible);
}


bool IrrAssimpImport::isALoadableFileExtension(const io::path& filename) const
{
    Assimp::Importer importer;

    io::path extension;
    core::getFileNameExtension(extension, filename);
    return importer.IsExtensionSupported (extension.c_str());
}

scene::IAnimatedMesh* IrrAssimpImport::createMesh(io::IReadFile* file)
{
    m_filePath = file->getFileName();
    m_animRanges.clear();
    Assimp::Importer importer;
    importer.SetPropertyInteger(AI_CONFIG_PP_SLM_VERTEX_LIMIT, 65535);
    importer.SetPropertyInteger(AI_CONFIG_PP_SLM_TRIANGLE_LIMIT, 65535);

    const std::string filePath = irrToAssimpPath(file->getFileName()).C_Str();
    const bool isGltf = filePath.size() >= 4 &&
        (filePath.compare(filePath.size() - 4, 4, ".glb") == 0 ||
         filePath.compare(filePath.size() - 5, 5, ".gltf") == 0);

    unsigned int flags = aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_SplitLargeMeshes | aiProcess_FlipUVs;
    if (isGltf)
    {
        flags |= aiProcess_MakeLeftHanded | aiProcess_FlipWindingOrder | aiProcess_GlobalScale;
        // Assimp imports glTF ~10x smaller than the fastgltf path, so when we
        // fall back to Assimp for a .glb/.gltf the model comes out tiny (and the
        // fixed-world-radius SSAO halo then looks oversized around it). Scale the
        // whole scene back up via aiProcess_GlobalScale, which handles vertices,
        // bone offsets, node transforms and animation keys uniformly. Tune this
        // constant if a fallback-loaded model comes out wrong-sized.
        static const float kGltfFallbackScale = 10.0f;
        importer.SetPropertyFloat(AI_CONFIG_GLOBAL_SCALE_FACTOR_KEY, kGltfFallbackScale);
    }

    m_assimpScene = importer.ReadFile(filePath.c_str(), flags);
    if (!m_assimpScene)
    {
        error = importer.GetErrorString();
        return 0;
    }
    else
        error = "";

    // Create mesh
    m_irrMesh = m_sceneManager->createSkinnedMesh();

    // Load materials
    createMaterials();

    // Load nodes
    createNode(m_assimpScene->mRootNode);

    // Collect visible mesh indices from node visibility metadata
    m_visibleMeshes.set_used(m_assimpScene->mNumMeshes);
    for (unsigned int i = 0; i < m_assimpScene->mNumMeshes; ++i)
        m_visibleMeshes[i] = false;
    collectVisibleMeshes(m_assimpScene->mRootNode, m_visibleMeshes);

    // Load meshes
    createMeshes();

    // Resolve AttachedMeshes to buffer indices, then fold the static node
    // transforms into the vertices so raw vertex readers agree with the renderer
    m_animatedNodes.clear();
    markAnimatedNodes(m_assimpScene->mRootNode, false);
    remapAttachedMeshes();
    bakeStaticNodeTransforms();

    // Load animations
    createAnimation();

    m_irrMesh->setDirty();
    m_irrMesh->finalize();
    applyBoneOffsets();

    writeAnimFile();

    return m_irrMesh;
}

void IrrAssimpImport::createMaterials()
{
    const core::stringc fileDir = m_fileSystem->getFileDir(m_filePath);
    m_materials.clear();
    for (unsigned int i = 0; i < m_assimpScene->mNumMaterials; ++i)
    {
        video::SMaterial irrMaterial;
        irrMaterial.MaterialType = video::EMT_SOLID;

        aiMaterial* material = m_assimpScene->mMaterials[i];

        aiColor4D color;
        if (AI_SUCCESS == aiGetMaterialColor(material, AI_MATKEY_COLOR_DIFFUSE, &color)) {
            irrMaterial.DiffuseColor = assimpToIrrColor4(color);
        }
        if (AI_SUCCESS == aiGetMaterialColor(material, AI_MATKEY_COLOR_AMBIENT, &color)) {
            irrMaterial.AmbientColor = assimpToIrrColor4(color);
        }
        if (AI_SUCCESS == aiGetMaterialColor(material, AI_MATKEY_COLOR_EMISSIVE, &color)) {
            irrMaterial.EmissiveColor = assimpToIrrColor4(color);
        }
        if (AI_SUCCESS == aiGetMaterialColor(material, AI_MATKEY_COLOR_SPECULAR, &color)) {
            irrMaterial.SpecularColor = assimpToIrrColor4(color);
        }
        float shininess;
        if (AI_SUCCESS == aiGetMaterialFloat(material, AI_MATKEY_SHININESS, &shininess)) {
            irrMaterial.Shininess = shininess;
        }

        // GLTF 2.0 PBR: base color is reported under aiTextureType_BASE_COLOR in Assimp 5.x,
        // with a legacy mirror to aiTextureType_DIFFUSE. Prefer DIFFUSE for FBX/OBJ compat,
        // fall back to BASE_COLOR for GLB/GLTF that don't populate DIFFUSE.
        if (material->GetTextureCount(aiTextureType_DIFFUSE) > 0)
        {
            aiString path;
            material->GetTexture(aiTextureType_DIFFUSE, 0, &path);
            irrMaterial.TextureLayer[0].Texture = getTexture(assimpToIrrString(path), fileDir);
        }
        else if (material->GetTextureCount(aiTextureType_BASE_COLOR) > 0)
        {
            aiString path;
            material->GetTexture(aiTextureType_BASE_COLOR, 0, &path);
            irrMaterial.TextureLayer[0].Texture = getTexture(assimpToIrrString(path), fileDir);
        }

        // Don't bind normal maps to slot 1 — slot 1 is reserved for baked lightmaps.
        // The phong_perpixel shader treats any non-null slot 1 as a lightmap.

        m_materials.push_back(irrMaterial);
    }
}

void IrrAssimpImport::createMeshes()
{
    // Skipped meshes compact the buffer list, so buffer index != Assimp mesh
    // index.  SJoint::AttachedMeshes holds Assimp indices at this point and is
    // remapped through this table by remapAttachedMeshes().
    m_meshBufferIndex.set_used(m_assimpScene->mNumMeshes);
    for (unsigned int i = 0; i < m_assimpScene->mNumMeshes; ++i)
        m_meshBufferIndex[i] = -1;
    m_bufferSkinned.clear();

    for (unsigned int i = 0; i < m_assimpScene->mNumMeshes; ++i)
    {
        if (!m_visibleMeshes[i])
            continue;

        aiMesh* mesh = m_assimpScene->mMeshes[i];
        m_meshBufferIndex[i] = (irr::s32)m_irrMesh->getMeshBufferCount();
        m_bufferSkinned.push_back(mesh->mNumBones > 0);
        const unsigned int uvCount = mesh->GetNumUVChannels();
        const unsigned int verticesCount = mesh->mNumVertices;

        scene::SSkinMeshBuffer* irrBuffer = m_irrMesh->addMeshBuffer();
        irrBuffer->VertexType = (uvCount > 1 && mesh->mTextureCoords[1]) ? video::EVT_2TCOORDS : (mesh->HasTangentsAndBitangents() ? video::EVT_TANGENTS : video::EVT_STANDARD);

        // Resize Buffer
        switch (irrBuffer->VertexType)
        {
        case video::EVT_STANDARD:
            irrBuffer->Vertices_Standard.set_used(verticesCount);
            break;
        case video::EVT_2TCOORDS:
            irrBuffer->Vertices_2TCoords.set_used(verticesCount);
            break;
        case video::EVT_TANGENTS:
            irrBuffer->Vertices_Tangents.set_used(verticesCount);
            break;
        }


        for (unsigned int j = 0; j < verticesCount; ++j)
        {
            irrBuffer->getPosition(j) = assimpToIrrVector3(mesh->mVertices[j]);
            if (mesh->HasNormals())
            {
                irrBuffer->getNormal(j) = assimpToIrrVector3(mesh->mNormals[j]);
            }

            video::SColor irrColor = video::SColor(255, 255, 255, 255);
            if (mesh->HasVertexColors(0))
            {
                irrColor = assimpToIrrColor4(mesh->mColors[0][j]);
            }

            core::vector2df irrUv = core::vector2df(0.f, 0.f);
            if (uvCount > 0)
            {
                irrUv = assimpToIrrVector2(mesh->mTextureCoords[0][j]);
            }
            irrBuffer->getTCoords(j) = irrUv;

            switch (irrBuffer->VertexType)
            {
                case video::EVT_STANDARD:
                {
                    irrBuffer->Vertices_Standard[j].Color = irrColor;
                    break;
                }
                case video::EVT_2TCOORDS:
                {
                    irrBuffer->Vertices_2TCoords[j].Color = irrColor;
                    irrBuffer->Vertices_2TCoords[j].TCoords2 = assimpToIrrVector2(mesh->mTextureCoords[1][j]);
                    break;
                }
                case video::EVT_TANGENTS:
                {
                    irrBuffer->Vertices_Tangents[j].Color = irrColor;
                    irrBuffer->Vertices_Tangents[j].Tangent = assimpToIrrVector3(mesh->mTangents[j]);
                    irrBuffer->Vertices_Tangents[j].Binormal = assimpToIrrVector3(mesh->mBitangents[j]);
                    break;
                }
            }
        }


        irrBuffer->Indices.set_used(mesh->mNumFaces * 3);
        for (unsigned int j = 0; j < mesh->mNumFaces; ++j)
        {
            const aiFace face = mesh->mFaces[j];

            irrBuffer->Indices[3*j] = face.mIndices[0];
            irrBuffer->Indices[3*j + 1] = face.mIndices[1];
            irrBuffer->Indices[3*j + 2] = face.mIndices[2];
        }

        if (mesh->mMaterialIndex < m_materials.size())
            irrBuffer->Material = m_materials[mesh->mMaterialIndex];

        // Compute bounding box directly from Assimp vertices to avoid virtual dispatch into Irrlicht.dll
        if (verticesCount > 0)
        {
            core::aabbox3df bbox(assimpToIrrVector3(mesh->mVertices[0]));
            for (unsigned int j = 1; j < verticesCount; ++j)
                bbox.addInternalPoint(assimpToIrrVector3(mesh->mVertices[j]));
            irrBuffer->BoundingBox = bbox;
        }
        else
        {
            irrBuffer->BoundingBox.reset(0.f, 0.f, 0.f);
        }


        // Skinning — assign weights to joints and let ISkinnedMesh handle deformation at runtime
        for (unsigned int j = 0; j < mesh->mNumBones; ++j)
        {
            aiBone* bone = mesh->mBones[j];

            scene::ISkinnedMesh::SJoint* irrJoint = findJoint(assimpToIrrString(bone->mName));
            if (irrJoint == 0)
                continue;

            const u32 bufferId = m_irrMesh->getMeshBufferCount() - 1;
            const u32 bufferVertexCount = m_irrMesh->getMeshBuffer(bufferId)->getVertexCount();
            for (unsigned int h = 0; h < bone->mNumWeights; ++h)
            {
                const aiVertexWeight weight = bone->mWeights[h];
                if (weight.mVertexId >= bufferVertexCount)
                    continue;
                scene::ISkinnedMesh::SWeight* irrWeight = m_irrMesh->addWeight(irrJoint);
                irrWeight->buffer_id = bufferId;
                irrWeight->strength = weight.mWeight;
                irrWeight->vertex_id = weight.mVertexId;
            }
        }
    }
}

// Apply a world matrix to a buffer's vertices in place: positions directly,
// directions through the inverse transpose (the rotation part alone would tilt
// normals off the surface under non-uniform scale), winding reversed when the
// matrix mirrors.  Bounding box recomputed from the result.
static void bakeMatrixIntoBuffer(scene::SSkinMeshBuffer* buffer, const core::matrix4& world)
{
    const u32 vertexCount = buffer->getVertexCount();
    if (vertexCount == 0)
        return;

    core::matrix4 normalMatrix;
    const bool haveNormalMatrix = world.getInverse(normalMatrix);
    if (haveNormalMatrix)
        normalMatrix = normalMatrix.getTransposed();

    for (u32 v = 0; v < vertexCount; ++v)
    {
        world.transformVect(buffer->getPosition(v));
        if (haveNormalMatrix)
        {
            normalMatrix.rotateVect(buffer->getNormal(v));
            buffer->getNormal(v).normalize();
        }
    }

    if (haveNormalMatrix && buffer->VertexType == video::EVT_TANGENTS)
    {
        for (u32 v = 0; v < vertexCount; ++v)
        {
            video::S3DVertexTangents& vert = buffer->Vertices_Tangents[v];
            normalMatrix.rotateVect(vert.Tangent);
            vert.Tangent.normalize();
            normalMatrix.rotateVect(vert.Binormal);
            vert.Binormal.normalize();
        }
    }

    // A negative 3x3 determinant means the transform mirrors, which reverses
    // triangle winding and would invert backface culling and PhysX face normals.
    const f32* m = world.pointer();
    const core::vector3df ax(m[0], m[1], m[2]);
    const core::vector3df ay(m[4], m[5], m[6]);
    const core::vector3df az(m[8], m[9], m[10]);
    if (ax.crossProduct(ay).dotProduct(az) < 0.f)
    {
        for (u32 i = 0; i + 2 < buffer->Indices.size(); i += 3)
        {
            const u16 tmp = buffer->Indices[i + 1];
            buffer->Indices[i + 1] = buffer->Indices[i + 2];
            buffer->Indices[i + 2] = tmp;
        }
    }

    core::aabbox3df bbox(buffer->getPosition(0));
    for (u32 v = 1; v < vertexCount; ++v)
        bbox.addInternalPoint(buffer->getPosition(v));
    buffer->BoundingBox = bbox;
}

// Collect every node an animation can move, ancestors included: a child
// inherits its parent's motion.  Gates bakeStaticNodeTransforms().
void IrrAssimpImport::markAnimatedNodes(const aiNode* node, bool parentAnimated)
{
    bool animated = parentAnimated;
    for (unsigned int a = 0; a < m_assimpScene->mNumAnimations && !animated; ++a)
    {
        const aiAnimation* anim = m_assimpScene->mAnimations[a];
        for (unsigned int c = 0; c < anim->mNumChannels; ++c)
        {
            if (anim->mChannels[c]->mNodeName == node->mName)
            {
                animated = true;
                break;
            }
        }
    }

    if (animated)
        m_animatedNodes.insert(std::string(node->mName.C_Str()));

    for (unsigned int i = 0; i < node->mNumChildren; ++i)
        markAnimatedNodes(node->mChildren[i], animated);
}

// createNode() runs before createMeshes(), so it fills AttachedMeshes with
// Assimp mesh indices.  Those only match buffer indices when no mesh was
// skipped as invisible — translate them and drop the ones with no buffer.
void IrrAssimpImport::remapAttachedMeshes()
{
    const core::array<scene::ISkinnedMesh::SJoint*>& joints = m_irrMesh->getAllJoints();
    for (u32 j = 0; j < joints.size(); ++j)
    {
        core::array<u32> remapped;
        for (u32 k = 0; k < joints[j]->AttachedMeshes.size(); ++k)
        {
            const u32 assimpIndex = joints[j]->AttachedMeshes[k];
            if (assimpIndex < m_meshBufferIndex.size() && m_meshBufferIndex[assimpIndex] >= 0)
                remapped.push_back((u32)m_meshBufferIndex[assimpIndex]);
        }
        joints[j]->AttachedMeshes = remapped;
    }
}

// Fold each static node's world transform into its vertices.
//
// Irrlicht carries a non-skinned node's transform on
// SSkinMeshBuffer::Transformation, and only CAnimatedMeshSceneNode::render()
// applies it (AbsoluteTransformation * mb->Transformation).  PhysX cooking, the
// SSAO geometry pre-pass, navmesh generation, lightmap unwrap and octree
// triangle selectors all read the vertex buffers straight and miss it, so an
// authored node offset put collision and AO somewhere the mesh visibly is not.
// Baking makes the stored positions the one source of truth and leaves
// Transformation identity, so both paths agree.
//
// Mirrors GltfImport::bakeNodeTransform().  A baked buffer is detached from its
// joint — leaving it attached would apply the transform a second time.
void IrrAssimpImport::bakeStaticNodeTransforms()
{
    const u32 bufferCount = m_irrMesh->getMeshBufferCount();
    if (bufferCount == 0)
        return;

    const core::array<scene::ISkinnedMesh::SJoint*>& joints = m_irrMesh->getAllJoints();

    // A buffer shared by several nodes (mesh instancing) has no single transform
    // to bake, so those stay on Transformation.
    core::array<u32> refCount;
    refCount.set_used(bufferCount);
    for (u32 b = 0; b < bufferCount; ++b)
        refCount[b] = 0;
    for (u32 j = 0; j < joints.size(); ++j)
        for (u32 k = 0; k < joints[j]->AttachedMeshes.size(); ++k)
            if (joints[j]->AttachedMeshes[k] < bufferCount)
                ++refCount[joints[j]->AttachedMeshes[k]];

    core::array<scene::SSkinMeshBuffer*>& buffers = m_irrMesh->getMeshBuffers();
    u32 bakedCount = 0;

    for (u32 j = 0; j < joints.size(); ++j)
    {
        scene::ISkinnedMesh::SJoint* joint = joints[j];
        const bool animated =
            m_animatedNodes.find(std::string(joint->Name.c_str())) != m_animatedNodes.end();

        core::array<u32> kept;
        for (u32 k = 0; k < joint->AttachedMeshes.size(); ++k)
        {
            const u32 bufIdx = joint->AttachedMeshes[k];
            const bool skinned =
                bufIdx < m_bufferSkinned.size() && m_bufferSkinned[bufIdx];

            // Skinned buffers are placed by their weights, animated nodes need a
            // live Transformation, and an identity transform has nothing to bake.
            if (bufIdx >= bufferCount || animated || skinned ||
                refCount[bufIdx] != 1 || joint->GlobalMatrix.isIdentity())
            {
                kept.push_back(bufIdx);
                continue;
            }

            bakeMatrixIntoBuffer(buffers[bufIdx], joint->GlobalMatrix);
            ++bakedCount;
        }
        joint->AttachedMeshes = kept;
    }

    if (bakedCount)
        spdlog::debug("IrrAssimpImport: '{}' baked node transforms into {} of {} mesh buffers",
            m_filePath.c_str(), bakedCount, bufferCount);
}

static std::string cleanAnimName(const std::string& raw)
{
    // Split on '|'
    std::vector<std::string> tokens;
    std::stringstream ss(raw);
    std::string token;
    while (std::getline(ss, token, '|'))
        if (!token.empty())
            tokens.push_back(token);

    // Drop tokens that are pure noise (case-insensitive)
    static const std::regex noise("armature|baselayer|base[_ ]layer", std::regex::icase);
    std::vector<std::string> kept;
    for (const std::string& t : tokens)
        if (!std::regex_match(t, noise))
            kept.push_back(t);

    if (kept.empty())
        return raw;

    // If multiple meaningful tokens remain, join with '_'
    std::string result;
    for (size_t i = 0; i < kept.size(); ++i)
    {
        if (i > 0) result += '_';
        result += kept[i];
    }
    return result;
}

void IrrAssimpImport::createAnimation()
{
    double frameOffset = 0.f;
    for (unsigned int i = 0; i < m_assimpScene->mNumAnimations; ++i)
    {
        aiAnimation* anim = m_assimpScene->mAnimations[i];

        const double ticksPerSecond = (anim->mTicksPerSecond > 0.0) ? anim->mTicksPerSecond : 25.0;
        const double targetFPS = 30.0;
        const double timeScale = targetFPS / ticksPerSecond;
        printf("[IrrAssimp] anim[%u]: mTicksPerSecond=%.2f, mDuration=%.2f, duration=%.2fs, timeScale=%.4f\n",
            i, anim->mTicksPerSecond, anim->mDuration, anim->mDuration / ticksPerSecond, timeScale);
        m_irrMesh->setAnimationSpeed((float)targetFPS);

        for (unsigned int j = 0; j < anim->mNumChannels; ++j)
        {
            aiNodeAnim* nodeAnim = anim->mChannels[j];
            scene::ISkinnedMesh::SJoint* joint = findJoint(assimpToIrrString(nodeAnim->mNodeName));

            for (unsigned int k = 0; k < nodeAnim->mNumPositionKeys; ++k)
            {
                aiVectorKey key = nodeAnim->mPositionKeys[k];

                scene::ISkinnedMesh::SPositionKey* irrKey = m_irrMesh->addPositionKey(joint);
                irrKey->frame = (float)(key.mTime * timeScale + frameOffset);
                irrKey->position = assimpToIrrVector3(key.mValue);
            }
            for (unsigned int k = 0; k < nodeAnim->mNumRotationKeys; ++k)
            {
                aiQuatKey key = nodeAnim->mRotationKeys[k];

                scene::ISkinnedMesh::SRotationKey* irrKey = m_irrMesh->addRotationKey(joint);
                irrKey->frame = (float)(key.mTime * timeScale + frameOffset);
                irrKey->rotation = assimpToIrrQuaternion(key.mValue);
            }
            for (unsigned int k = 0; k < nodeAnim->mNumScalingKeys; ++k)
            {
                aiVectorKey key = nodeAnim->mScalingKeys[k];

                scene::ISkinnedMesh::SScaleKey* irrKey = m_irrMesh->addScaleKey(joint);
                irrKey->frame = (float)(key.mTime * timeScale + frameOffset);
                irrKey->scale = assimpToIrrVector3(key.mValue);
            }
        }

        const double clipDuration = (anim->mDuration / ticksPerSecond) * targetFPS;
        AnimRange range;
        range.name      = cleanAnimName(anim->mName.C_Str());
        range.startFrame = (int)frameOffset;
        range.endFrame   = (int)(frameOffset + clipDuration);
        range.loop       = true;
        m_animRanges.push_back(range);

        frameOffset += clipDuration;
    }
}
void generateAnimFile(const std::string& meshPath)
{
    const size_t dot = meshPath.find_last_of('.');
    const std::string animPath = (dot != std::string::npos ? meshPath.substr(0, dot) : meshPath) + ".anim";

    // Don't overwrite an existing file
    FILE* test = fopen(animPath.c_str(), "r");
    if (test) { fclose(test); return; }

    Assimp::Importer importer;
    // Minimal flags — we only need animation metadata, no mesh processing
    const aiScene* scene = importer.ReadFile(meshPath.c_str(), aiProcess_Triangulate);
    if (!scene || scene->mNumAnimations == 0)
        return;

    const double targetFPS = 30.0;
    std::vector<AnimRange> ranges;
    double frameOffset = 0.0;

    for (unsigned int i = 0; i < scene->mNumAnimations; ++i)
    {
        const aiAnimation* anim = scene->mAnimations[i];
        const double ticksPerSecond = (anim->mTicksPerSecond > 0.0) ? anim->mTicksPerSecond : 25.0;
        const double clipDuration = (anim->mDuration / ticksPerSecond) * targetFPS;

        AnimRange r;
        r.name       = cleanAnimName(anim->mName.C_Str());
        r.startFrame = (int)frameOffset;
        r.endFrame   = (int)(frameOffset + clipDuration);
        r.loop       = true;
        ranges.push_back(r);

        frameOffset += clipDuration;
    }

    FILE* f = fopen(animPath.c_str(), "w");
    if (!f)
    {
        spdlog::error("IrrAssimp: failed to write .anim file '{}'", animPath);
        return;
    }

    fprintf(f, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
    fprintf(f, "<animation>\n");
    fprintf(f, "\t<fps>30</fps>\n");
    fprintf(f, "\t<animationList size=\"dynamic\">\n");
    for (size_t i = 0; i < ranges.size(); ++i)
    {
        const AnimRange& r = ranges[i];
        fprintf(f, "\t\t<a%zu  name=\"%s\" loop=\"%d\">%d,%d</a%zu>\n",
            i, r.name.c_str(), r.loop ? 1 : 0, r.startFrame, r.endFrame, i);
    }
    fprintf(f, "\t</animationList>\n");
    fprintf(f, "</animation>\n");
    fclose(f);

    spdlog::info("IrrAssimp: generated '{}' — {} clip(s):", animPath, ranges.size());
    for (const AnimRange& r : ranges)
        spdlog::info("  '{}' frames {}-{}", r.name, r.startFrame, r.endFrame);
}

void IrrAssimpImport::writeAnimFile()
{
    if (m_animRanges.empty())
        return;

    // Build .anim path by replacing the mesh file extension
    std::string meshPath = m_filePath.c_str();
    const size_t dot = meshPath.find_last_of('.');
    const std::string animPath = (dot != std::string::npos ? meshPath.substr(0, dot) : meshPath) + ".anim";

    // Don't overwrite an existing hand-edited file
    if (m_fileSystem->existFile(animPath.c_str()))
        return;

    FILE* f = fopen(animPath.c_str(), "w");
    if (!f)
    {
        spdlog::error("IrrAssimp: failed to write .anim file '{}'", animPath);
        return;
    }

    fprintf(f, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
    fprintf(f, "<animation>\n");
    fprintf(f, "\t<fps>30</fps>\n");
    fprintf(f, "\t<animationList size=\"dynamic\">\n");

    for (size_t i = 0; i < m_animRanges.size(); ++i)
    {
        const AnimRange& r = m_animRanges[i];
        fprintf(f, "\t\t<a%zu  name=\"%s\" loop=\"%d\">%d,%d</a%zu>\n",
            i, r.name.c_str(), r.loop ? 1 : 0, r.startFrame, r.endFrame, i);
    }

    fprintf(f, "\t</animationList>\n");
    fprintf(f, "</animation>\n");
    fclose(f);
}

// Adapted from http://sourceforge.net/p/assimp/discussion/817654/thread/5462cbf5
void IrrAssimpImport::skinJoint(scene::ISkinnedMesh::SJoint* joint, aiBone* bone)
{
	if (bone->mNumWeights)
	{
        core::matrix4 boneOffset = assimpToIrrMatrix(bone->mOffsetMatrix);
	    core::matrix4 boneMat = joint->GlobalMatrix * boneOffset; //* InverseRootNodeWorldTransform;

        const u32 bufferId = m_irrMesh->getMeshBufferCount() - 1;

		for (u32 i = 0; i < bone->mNumWeights; ++i)
		{
		    const aiVertexWeight weight = bone->mWeights[i];
			const u32 vertexId = weight.mVertexId;

            core::vector3df sourcePos = m_irrMesh->getMeshBuffer(bufferId)->getPosition(vertexId);
            core::vector3df sourceNorm = m_irrMesh->getMeshBuffer(bufferId)->getNormal(vertexId);
			core::vector3df destPos, destNormal;
			boneMat.transformVect(destPos, sourcePos);
			boneMat.rotateVect(destNormal, sourceNorm);

            m_skinnedVertex[vertexId].moved = true;
            m_skinnedVertex[vertexId].position += destPos * weight.mWeight;
            m_skinnedVertex[vertexId].normal += destNormal * weight.mWeight;
		}
	}
}

void IrrAssimpImport::buildSkinnedVertexArray(scene::IMeshBuffer* buffer)
{
    m_skinnedVertex.clear();

    m_skinnedVertex.reallocate(buffer->getVertexCount());
    for (u32 i = 0; i < buffer->getVertexCount(); ++i)
    {
        m_skinnedVertex.push_back(SkinnedVertex());
    }

}

void IrrAssimpImport::applySkinnedVertexArray(scene::IMeshBuffer* buffer)
{
    for (u32 i = 0; i < buffer->getVertexCount(); ++i)
    {
        if (m_skinnedVertex[i].moved)
        {
            buffer->getPosition(i) = m_skinnedVertex[i].position;
            buffer->getNormal(i) = m_skinnedVertex[i].normal;
        }
    }
    m_skinnedVertex.clear();
}

void IrrAssimpImport::applyBoneOffsets()
{
    for (unsigned int i = 0; i < m_assimpScene->mNumMeshes; ++i)
    {
        aiMesh* mesh = m_assimpScene->mMeshes[i];
        for (unsigned int j = 0; j < mesh->mNumBones; ++j)
        {
            aiBone* bone = mesh->mBones[j];
            scene::ISkinnedMesh::SJoint* joint = findJoint(assimpToIrrString(bone->mName));
            if (joint)
                joint->GlobalInversedMatrix = assimpToIrrMatrix(bone->mOffsetMatrix);
        }
    }
}
