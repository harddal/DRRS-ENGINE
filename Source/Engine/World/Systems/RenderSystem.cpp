#include "RenderSystem.h"

#include "Engine/Engine.h"
#include "Engine/Renderer/RenderManager.h"
#include "Engine/Resource/FilePaths.h"
#include "Game/Components/WaterComponent.h"

#include "Editor/SceneInteractionManager.h"
#include "Editor/EditorState.h"

#define DEBUG_MESH_LIGHT_SPHERE_SCALE 0.75f;

using namespace anax;
using namespace irr;
using namespace core;
using namespace scene;
using namespace video;

void RenderSystem::setMeshComponentData(Entity& entity)
{
    auto& meshComponent = entity.getComponent<MeshComponent>();

    if (meshComponent.node) {
        if (meshComponent.isViewmodel)
            RenderManager::Get()->unregisterViewmodelNode(meshComponent.node);
        meshComponent.node->remove();
    }
    if (meshComponent.selector) {
        meshComponent.selector->drop();
    }

	bool no_mesh = false;

    if (meshComponent.mesh == "_primitive_cube") {
        meshComponent.isPrimitive = true;
        meshComponent.trimesh = RenderManager::Get()->sceneManager()->getMesh("content/mesh/primitive/cube.obj");
    }
    else if (meshComponent.mesh == "_primitive_sphere") {
        meshComponent.isPrimitive = true;
        meshComponent.trimesh = RenderManager::Get()->sceneManager()->getMesh("content/mesh/primitive/sphere.obj");
    }
	else if (meshComponent.mesh == "_primitive_cylinder") {
		meshComponent.isPrimitive = true;
		meshComponent.trimesh = RenderManager::Get()->sceneManager()->getMesh("content/mesh/primitive/cylinder.obj");
	}
    else if (meshComponent.mesh == "_primitive_double_tetrahedron") {
        meshComponent.isPrimitive = true;
        meshComponent.trimesh = RenderManager::Get()->sceneManager()->getMesh("content/mesh/primitive/double_tetrahedron.obj");
    }
    // TODO: Check if a valid file path was specified 
    else if (!meshComponent.mesh.empty()) {

		auto str = Utility::FileExtensionFromPath(meshComponent.mesh);

		if (str == "fbx")
		{
			meshComponent.trimesh = RenderManager::Get()->assimp()->getMesh(meshComponent.mesh.c_str());
		}
		else if (str == "glb")
		{
			meshComponent.trimesh = RenderManager::Get()->assimp()->getMesh(meshComponent.mesh.c_str());
		}
		else if (str == "gltf")
		{
			meshComponent.trimesh = RenderManager::Get()->assimp()->getMesh(meshComponent.mesh.c_str());
		}
		else
		{
			meshComponent.trimesh = RenderManager::Get()->sceneManager()->getMesh(meshComponent.mesh.c_str());
		}

		if (!meshComponent.trimesh)
			goto error_no_mesh;
    }
    else
    {
	error_no_mesh:

		spdlog::error("Failed to load mesh \'{0}\' for entity \'{1}\'", meshComponent.mesh, entity.getComponent<DescriptorComponent>().name);

		meshComponent.mesh = "_invalid_mesh";
		meshComponent.isPrimitive = true;

		// Load a debug mesh instead
		meshComponent.trimesh = RenderManager::Get()->sceneManager()->getMesh("content/mesh/primitive/double_tetrahedron.obj");

		no_mesh = true;
    }

	if (meshComponent.trimesh)
	{
		meshComponent.node = RenderManager::Get()->sceneManager()->addAnimatedMeshSceneNode(meshComponent.trimesh, nullptr, entity.getComponent<DescriptorComponent>().id);
	}
	else
	{
		Utility::Warning("Unable to load default mesh \'double_tetrahedron\' in RenderSystem::setMeshComponentData, file missing or corrupted. WARNING: meshComponent.trimesh and meshComponent.node are nullptr!");

		return;
	}

	if (no_mesh)
	{
		auto* t = RenderManager::Get()->driver()->getTexture("content/texture/color/magenta.png");
		meshComponent.node->setMaterialTexture(0, t);
	}

	if (meshComponent.recalculateNormals)
	{
		RenderManager::Get()->manipulator()->recalculateNormals(meshComponent.node->getMesh(), true);
	}

	//RenderManager::Get()->driver()->addOcclusionQuery(meshComponent.node, meshComponent.trimesh);
	
    if (!meshComponent.textures.empty()) 
	{
		auto iter = 0U;
		for (auto t : meshComponent.textures)
		{
			meshComponent.node->setMaterialTexture(iter++, RenderManager::Get()->driver()->getTexture(t.c_str()));
		}
    }

    meshComponent.node->setID(entity.getComponent<DescriptorComponent>().id);

    auto transform = entity.getComponent<TransformComponent>();


    if (entity.getComponent<DescriptorComponent>().isDebug && Engine::Get()->isGameMode()) {
        meshComponent.node->setIsDebugObject(true);
    }

    meshComponent.node->setParent(transform.node);

    // No MSAA with deferred rendering
    meshComponent.node->setMaterialFlag(EMF_ANTI_ALIASING, true);

    // Gouraud shading is irrelevant when a GLSL shader is active — the shader
    // handles all lighting. Disable it to avoid confusion.
    meshComponent.node->setMaterialFlag(EMF_GOURAUD_SHADING, false);

    // Per-pixel shader handles lighting; keep the Irrlicht lighting flag on so
    // that light data flows through the pipeline to our GLSL uniforms.
    meshComponent.node->setMaterialFlag(EMF_LIGHTING, true);

    meshComponent.node->setMaterialFlag(EMF_NORMALIZE_NORMALS, true);

	if (meshComponent.texturePointFilter || RenderManager::Get()->getConfiguration().pointFilterOverride)
	{
		meshComponent.node->setMaterialFlag(EMF_BILINEAR_FILTER, false);
		meshComponent.node->setMaterialFlag(EMF_TRILINEAR_FILTER, false);
		meshComponent.node->setMaterialFlag(EMF_ANISOTROPIC_FILTER, true);
	}
	else
	{
		meshComponent.node->setMaterialFlag(EMF_BILINEAR_FILTER, false);
		meshComponent.node->setMaterialFlag(EMF_TRILINEAR_FILTER, true);
		meshComponent.node->setMaterialFlag(EMF_ANISOTROPIC_FILTER, true);
	}

    meshComponent.node->setMaterialFlag(EMF_ZBUFFER, !meshComponent.disableZDraw);

	if (meshComponent.transparent)
	{
		//meshComponent.renderMaterial = EMT_TRANSPARENT_ALPHA_CHANNEL;
		meshComponent.node->setMaterialType(meshComponent.renderMaterial);
		meshComponent.node->setMaterialFlag(EMF_ZWRITE_ENABLE, true);

		meshComponent.node->setMaterialFlag(EMF_BLEND_OPERATION, true);
	}
	else
	{
		// Apply the per-pixel Blinn-Phong shader to all solid (non-transparent) meshes.
		// Without this the node defaults to Irrlicht's built-in vertex lighting.
		auto perpixelMat = ShaderMaterialManager::get("phong_perpixel");
		if (perpixelMat != EMT_SOLID) // only assign if shader compiled successfully
		{
			meshComponent.node->setMaterialType(perpixelMat);
		}
	}

       
	/*
		// Painted metal (gun, vehicles)
		node->getMaterial(i).Shininess = 96.f;          // roughness ≈ 0.13
		node->getMaterial(i).SpecularColor.setAlpha(220); // nearly full metal

		// Stone/concrete (walls)
		node->getMaterial(i).Shininess = 0.f;           // roughness = 1.0
		node->getMaterial(i).SpecularColor.setAlpha(0);   // dielectric, no metal

		// Plastic (crates, props)
		node->getMaterial(i).Shininess = 48.f;          // roughness ≈ 0.39
		node->getMaterial(i).SpecularColor.setAlpha(0);   // dielectric
	*/

	for (auto i = 0; i < meshComponent.node->getMaterialCount(); i++)
	{
		meshComponent.node->getMaterial(i).Shininess = 0.f;
		meshComponent.node->getMaterial(i).SpecularColor.setAlpha(0);
	}

	// Apply water shader if entity has water component
	if (entity.hasComponent<WaterComponent>())
	{
		auto& waterComp = entity.getComponent<WaterComponent>();
		auto* driver    = RenderManager::Get()->driver();

		auto waterMat = ShaderMaterialManager::get("water");
		if (waterMat != EMT_SOLID)
			meshComponent.node->setMaterialType(waterMat);

		// Disable z-write so transparent water sorts correctly behind solid geometry.
		meshComponent.node->setMaterialFlag(EMF_ZWRITE_ENABLE, false);
		meshComponent.node->setMaterialFlag(EMF_LIGHTING, false);
		// Render back faces so the surface is visible from below.
		meshComponent.node->setMaterialFlag(EMF_BACK_FACE_CULLING, false);

		// Encode shallow/deep colors into material slots so WaterShaderCallback
		// can read them without needing per-entity state.
		// AmbientColor  → uShallowColor
		// DiffuseColor  → uDeepColor   (alpha = surface opacity, 204 ≈ 0.8)
		const auto& sc = waterComp.shallowColor;
		const auto& dc = waterComp.deepColor;
		for (u32 i = 0; i < meshComponent.node->getMaterialCount(); ++i)
		{
			auto& mat = meshComponent.node->getMaterial(i);
			mat.AmbientColor.set(255,
				static_cast<u32>(sc[0] * 255), static_cast<u32>(sc[1] * 255), static_cast<u32>(sc[2] * 255));
			mat.DiffuseColor.set(204,
				static_cast<u32>(dc[0] * 255), static_cast<u32>(dc[1] * 255), static_cast<u32>(dc[2] * 255));
		}

		// Load optional second texture (normal/overlay layer) into slot 1.
		// Specify it as textures[1] in the .ent file alongside the primary texture.
		if (meshComponent.textures.size() > 1)
		{
			auto* t1 = driver->getTexture(meshComponent.textures[1].c_str());
			if (t1)
				meshComponent.node->setMaterialTexture(1, t1);
		}
	}
	else if (meshComponent.transparent)
	{
		//meshComponent.renderMaterial = EMT_TRANSPARENT_ALPHA_CHANNEL;
		meshComponent.node->setMaterialType(meshComponent.renderMaterial);
		meshComponent.node->setMaterialFlag(EMF_ZWRITE_ENABLE, true);

		meshComponent.node->setMaterialFlag(EMF_BLEND_OPERATION, true);
	}

    /* --- DEBUG DRAWING --- */
    //meshComponent.node->setDebugDataVisible(EDS_NORMALS | EDS_BBOX_ALL);

    meshComponent.node->updateAbsolutePosition();

    if (meshComponent.isAnimated) {
        meshComponent.node->setAnimationSpeed(static_cast<irr::f32>(meshComponent.fps));
        meshComponent.node->setLoopMode(false);
        meshComponent.node->setFrameLoop(0, 0);

        meshComponent.animation_call_back = std::make_shared<AnimationCallback>();
        meshComponent.node->setAnimationEndCallback(meshComponent.animation_call_back.get());
    }

    //if (meshComponent.trimesh && meshComponent.trimesh->getMeshType() == irr::scene::EAMT_SKINNED)
    //{
    //    // IrrAssimp (GLB/GLTF/FBX) produces ISkinnedMesh. Irrlicht's finalize() sets
    //    // Buffer->Transformation = Scale(100) (from aiProcess_GlobalScale on the root
    //    // node), which makes updateBoundingBox() produce a 100x-inflated bounding box.
    //    // Both the AABB pre-filter and the animated-node triangle selector are broken
    //    // as a result. Use the static IMesh* overload instead: it reads the raw
    //    // bind-pose vertex positions (correct 100x engine-unit coordinates) directly
    //    // from the buffer and is immune to skinning/bounding-box corruption.
    //    meshComponent.selector = RenderManager::Get()->sceneManager()->createTriangleSelector(
    //        static_cast<irr::scene::IMesh*>(meshComponent.trimesh),
    //        meshComponent.node);
    //}
    //else 
	if (meshComponent.isAnimated)
    {
        meshComponent.selector = RenderManager::Get()->sceneManager()->createTriangleSelectorFromBoundingBox(meshComponent.node);
    }
    else
    {
        meshComponent.selector = RenderManager::Get()->sceneManager()->createTriangleSelector(meshComponent.node);
    }
    meshComponent.node->setTriangleSelector(meshComponent.selector);

	// Does it do anything? meshComponent.renderMaterial isnt currently used I don't think
  /*  for (u32 i = 0; i < meshComponent.node->getMaterialCount(); i++) {
        meshComponent.node->getMaterial(i).MaterialType = meshComponent.renderMaterial;
    }*/

#ifndef USE_MULTI_PASS_RENDER
	if (!meshComponent.disableDeferredRendering || !meshComponent.transparent)
	{
		//RenderManager::Get()->renderer()->getMaterialSwapper()->swapMaterials(meshComponent.node);
	}
#else
	RenderManager::Get()->effect()->addShadowToNode(meshComponent.node, E_FILTER_TYPE::EFT_8PCF, ESM_BOTH);
#endif

	if (meshComponent.isViewmodel)
	{
		RenderManager::Get()->registerViewmodelNode(meshComponent.node);
		meshComponent.node->setVisible(false);
	}

	if (entity.getComponent<MeshComponent>().isPreview)
	{
		meshComponent.node->setVisible(false);
	}
	else
	{
		meshComponent.node->setVisible(entity.getComponent<RenderComponent>().isVisible);
	}
}
void RenderSystem::setDebugMeshComponentData(anax::Entity& entity)
{
    bool isPointer = false;

    auto& meshComponent = entity.getComponent<DebugMeshComponent>();

    if (meshComponent.node) {
        entity.getComponent<DebugMeshComponent>().node->remove();
    }
    if (meshComponent.selector) {
        meshComponent.selector->drop();
    }

    if (meshComponent.mesh == "_primitive_cube") {
        meshComponent.trimesh = RenderManager::Get()->sceneManager()->getMesh("content/mesh/primitive/cube.obj");
        meshComponent.node = RenderManager::Get()->sceneManager()->addAnimatedMeshSceneNode(meshComponent.trimesh, nullptr, entity.getComponent<DescriptorComponent>().id);
    }
    else if (meshComponent.mesh == "_primitive_sphere") {
        meshComponent.trimesh = RenderManager::Get()->sceneManager()->getMesh("content/mesh/primitive/sphere_low.obj");
        meshComponent.node = RenderManager::Get()->sceneManager()->addAnimatedMeshSceneNode(meshComponent.trimesh, nullptr, entity.getComponent<DescriptorComponent>().id);
    }
    else if (meshComponent.mesh == "_primitive_cylinder") {
        meshComponent.trimesh = RenderManager::Get()->sceneManager()->getMesh("content/mesh/primitive/cylinder_low.obj");
        meshComponent.node = RenderManager::Get()->sceneManager()->addAnimatedMeshSceneNode(meshComponent.trimesh, nullptr, entity.getComponent<DescriptorComponent>().id);
    }
    else if (meshComponent.mesh == "_primitive_double_tetrahedron") {
        meshComponent.trimesh = RenderManager::Get()->sceneManager()->getMesh("content/mesh/primitive/double_tetrahedron.obj");
        meshComponent.node = RenderManager::Get()->sceneManager()->addAnimatedMeshSceneNode(meshComponent.trimesh, nullptr, entity.getComponent<DescriptorComponent>().id);
    }
    else if (meshComponent.mesh == "_primitive_pointer") {
        meshComponent.trimesh = RenderManager::Get()->sceneManager()->getMesh("content/mesh/primitive/cube.obj");
        meshComponent.node = RenderManager::Get()->sceneManager()->addAnimatedMeshSceneNode(meshComponent.trimesh, nullptr, entity.getComponent<DescriptorComponent>().id);
        meshComponent.node->setScale(vector3df(.01f, .01f, 0.5f));
        isPointer = true;
    }
    else
    {
        meshComponent.mesh = "_primitive_cube";
        meshComponent.trimesh = RenderManager::Get()->sceneManager()->getMesh("content/mesh/primitive/cube.obj");
        meshComponent.node = RenderManager::Get()->sceneManager()->addAnimatedMeshSceneNode(meshComponent.trimesh, nullptr, entity.getComponent<DescriptorComponent>().id);
    }

    auto* t = RenderManager::Get()->driver()->getTexture(meshComponent.texture.c_str());
    if (t) {
        meshComponent.node->setMaterialTexture(0, t);
    }

    meshComponent.node->setID(entity.getComponent<DescriptorComponent>().id);

    auto transform = entity.getComponent<TransformComponent>();

	if (entity.getComponent<DescriptorComponent>().isDebug && Engine::Get()->isGameMode())
	{
		meshComponent.node->setIsDebugObject(true);
	}

    meshComponent.node->setParent(transform.node);
    if (isPointer) {
        meshComponent.node->setPosition(vector3df(0, 0, .5f));
    }

    // DEPRECATED
    //meshComponent.node->setPosition(transform.position);
    //meshComponent.node->setRotation(transform.rotation);
    //meshComponent.node->setScale(transform.scale);

    // No MSAA with deferred rendering
    meshComponent.node->setMaterialFlag(EMF_ANTI_ALIASING, false);

    meshComponent.node->setMaterialFlag(EMF_WIREFRAME, true);

    meshComponent.node->setMaterialFlag(EMF_GOURAUD_SHADING, false);

    meshComponent.node->setMaterialFlag(EMF_LIGHTING, false);
    meshComponent.node->setMaterialFlag(EMF_NORMALIZE_NORMALS, false);

    meshComponent.node->setMaterialFlag(EMF_BILINEAR_FILTER, true);
    meshComponent.node->setMaterialFlag(EMF_TRILINEAR_FILTER, true);
    meshComponent.node->setMaterialFlag(EMF_ANISOTROPIC_FILTER, false);


    /* --- DEBUG DRAWING --- */
    //meshComponent.node->setDebugDataVisible(EDS_NORMALS | EDS_BBOX_ALL);

    meshComponent.node->updateAbsolutePosition();

    meshComponent.selector = RenderManager::Get()->sceneManager()->createTriangleSelector(meshComponent.node);
    meshComponent.node->setTriangleSelector(meshComponent.selector);

    //RenderManager::Get()->renderer()->getMaterialSwapper()->swapMaterials(meshComponent.node);

	if (entity.hasComponent<LightComponent>() && meshComponent.mesh == "_primitive_sphere")
	{
		auto lscale = entity.getComponent<LightComponent>().radius * DEBUG_MESH_LIGHT_SPHERE_SCALE;
		meshComponent.node->setScale(irr::core::vector3df(lscale, lscale, lscale));

		meshComponent.node->setVisible(false);

		meshComponent.node->setIsDebugObject(true);
	}
	else
	{
		meshComponent.node->setVisible(m_drawDebugSprites);
	}

    if (entity.getComponent<DescriptorComponent>().isDebug && Engine::Get()->isGameMode()) {
        meshComponent.node->setIsDebugObject(true);
		entity.getComponent<RenderComponent>().isDebug = true;
    }

	RenderManager::Get()->registerDebugNode(meshComponent.node);
}
void RenderSystem::setLightComponentData(anax::Entity& entity)
{
    auto& lightComponent = entity.getComponent<LightComponent>();
    auto &transform = entity.getComponent<TransformComponent>();

	if (lightComponent.mode == LM_DYNAMIC || lightComponent.mode == LM_DYNAMIC_SHADOW)
	{
		if (lightComponent.node) {
			lightComponent.node->remove();
		}

		lightComponent.node =
			RenderManager::Get()->sceneManager()->addLightSceneNode(transform.node);

		// DEPRECATED
		//lightComponent.node->setPosition(transform.position + lightComponent.offset);

		lightComponent.node->setID(entity.getComponent<DescriptorComponent>().id);

		if (entity.getComponent<DescriptorComponent>().isDebug && Engine::Get()->isGameMode()) {
			lightComponent.node->setIsDebugObject(true);
		}

		setLightData(lightComponent);
	}
}
void RenderSystem::setBillboardComponentData(anax::Entity& entity)
{
    auto& spriteComponent = entity.getComponent<BillboardSpriteComponent>();
    auto &transform = entity.getComponent<TransformComponent>();

    if (spriteComponent.selectorNode) {
        spriteComponent.selectorNode->remove();
    }
    if (spriteComponent.selector) {
        spriteComponent.selector->drop();
    }
    if (spriteComponent.node) {
        spriteComponent.node->remove();
    }

    // Selector
    spriteComponent.selectorMesh =
        RenderManager::Get()->sceneManager()->getMesh("content/mesh/primitive/sphere_low.obj");

    spriteComponent.selectorNode =
        RenderManager::Get()->sceneManager()->addAnimatedMeshSceneNode(
            spriteComponent.selectorMesh,
            /*entity.getComponent<TransformComponent>().node*/nullptr,
            entity.getComponent<DescriptorComponent>().id);

    spriteComponent.selectorNode->setScale(vector3df(0.5f, 0.5f, 0.5f));
    spriteComponent.selector = RenderManager::Get()->sceneManager()->createTriangleSelector(spriteComponent.selectorNode);
    spriteComponent.selectorNode->setTriangleSelector(spriteComponent.selector);
    spriteComponent.selectorNode->setMaterialFlag(EMF_LIGHTING, false);
    spriteComponent.selectorNode->setMaterialFlag(EMF_WIREFRAME, true);
    spriteComponent.selectorNode->setMaterialFlag(EMF_FRONT_FACE_CULLING, true);
    spriteComponent.selectorNode->setVisible(m_drawDebugSprites);
    // DEPRECATED
    //spriteComponent.selectorNode->setPosition(transform.position);
    spriteComponent.selectorNode->setID(entity.getComponent<DescriptorComponent>().id);
    RenderManager::Get()->registerDebugNode(spriteComponent.selectorNode);

    if (entity.getComponent<DescriptorComponent>().isDebug && Engine::Get()->isGameMode()) {
        spriteComponent.selectorNode->setIsDebugObject(true);
    }

    spriteComponent.node =
        RenderManager::Get()->sceneManager()->addBillboardSceneNode(entity.getComponent<TransformComponent>().node);

    spriteComponent.node->setSize(dimension2d<f32>(spriteComponent.scale_x, spriteComponent.scale_y));

	///////////////////////////////////////////////////////////////////////////////////
	///// BUG: MASSIVE MEMORY LEAK, ALLOCATES ~30MB per billboard sprite, doesn't release it when entity is destroyed
	/////      Spawn a few to many and you get 1+ GB of memory leaked and a heap corruption crash.
	////
	////       spriteComponent.texture_list stores pointers to the textures used for the animation,
	////       the lists need only to be created once at program start then referenced for each sprite aka resource manager.
	///////////////////////////////////////////////////////////////////////////////////
    if (spriteComponent.animated) {

		/*static bool flag = false;
		static std::vector<irr::video::ITexture*> texture_list;
		if (!flag)
		{
			auto source_image = RenderManager::Get()->driver()->createImageFromFile(std::string(g_texture_path + spriteComponent.sprite).c_str());
			IImage* temp_image = RenderManager::Get()->driver()->createImage(source_image->getColorFormat(), irr::core::dimension2d<u32>(spriteComponent.split_x, spriteComponent.split_y));

			auto counter = 0;
			for (auto i = 0U; i < source_image->getDimension().Height / spriteComponent.split_y; i++) {
				for (auto j = 0U; j < source_image->getDimension().Width / spriteComponent.split_x; j++) {

					source_image->copyTo(temp_image, irr::core::vector2d<s32>(0, 0), irr::core::rect<int>(
						spriteComponent.split_x * j, spriteComponent.split_y * i, spriteComponent.split_x * (i + 8), spriteComponent.split_y * (j + 8)));

					texture_list.push_back(RenderManager::Get()->driver()->addTexture(std::to_string(counter++).c_str(), temp_image));
				}
			}

			temp_image->drop();
			source_image->drop();

			flag = true;
		}*/

		auto animated_sprite = RenderManager::Get()->getAnimatedSprite(spriteComponent.sprite);
		if (animated_sprite.id < 0)
		{
			animated_sprite = RenderManager::Get()->getAnimatedSprite("error");
		}

		spriteComponent.texture_list = animated_sprite.texture_list;

        spriteComponent.node->setMaterialTexture(0, spriteComponent.texture_list.at(0));

        /* for (auto texture : spriteComponent.texture_list) {
             RenderManager::Get()->driver()->makeColorKeyTexture(texture, core::position2d<s32>(0, 0));
         }*/
    } /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    else {
        spriteComponent.node->setMaterialTexture(0, RenderManager::Get()->driver()->getTexture(
            std::string(g_texture_path + spriteComponent.sprite).c_str()));
    }

    spriteComponent.node->setMaterialType(EMT_TRANSPARENT_ADD_COLOR); // EMT_TRANSPARENT_ALPHA_CHANNEL_REF might be better for billboards sprites I guess? needs testing...
    spriteComponent.node->setMaterialFlag(EMF_LIGHTING, false);
    spriteComponent.node->setMaterialFlag(EMF_BILINEAR_FILTER, true);
    spriteComponent.node->setMaterialFlag(EMF_TRILINEAR_FILTER, true);
    spriteComponent.node->setMaterialFlag(EMF_ANISOTROPIC_FILTER, true);
    spriteComponent.node->setID(entity.getComponent<DescriptorComponent>().id);

	

    spriteComponent.selectorNode->setPosition(spriteComponent.node->getAbsolutePosition()); 
}
void RenderSystem::setDebugSpriteComponentData(anax::Entity& entity)
{
    auto& debugspriteComponent = entity.getComponent<DebugSpriteComponent>();
    auto &transform = entity.getComponent<TransformComponent>();

    if (debugspriteComponent.selectorNode) {
        debugspriteComponent.selectorNode->remove();
    }
    if (debugspriteComponent.selector) {
        debugspriteComponent.selector->drop();
    }
    if (debugspriteComponent.node) {
        debugspriteComponent.node->remove();
    }

    // Selector
    debugspriteComponent.selectorMesh =
        RenderManager::Get()->sceneManager()->getMesh("content/mesh/primitive/sphere_low.obj");

    debugspriteComponent.selectorNode =
        RenderManager::Get()->sceneManager()->addAnimatedMeshSceneNode(
            debugspriteComponent.selectorMesh,
            /*entity.getComponent<TransformComponent>().node*/nullptr,
            entity.getComponent<DescriptorComponent>().id);

    debugspriteComponent.selectorNode->setScale(vector3df(0.5f, 0.5f, 0.5f));
    debugspriteComponent.selector = RenderManager::Get()->sceneManager()->createTriangleSelector(debugspriteComponent.selectorNode);
    debugspriteComponent.selectorNode->setTriangleSelector(debugspriteComponent.selector);
    debugspriteComponent.selectorNode->setMaterialFlag(EMF_LIGHTING, false);
    debugspriteComponent.selectorNode->setMaterialFlag(EMF_WIREFRAME, true);
    debugspriteComponent.selectorNode->setMaterialFlag(EMF_FRONT_FACE_CULLING, true);
    debugspriteComponent.selectorNode->setVisible(m_drawDebugSprites);
    // DEPRECATED
    //debugspriteComponent.selectorNode->setPosition(transform.position);
    debugspriteComponent.selectorNode->setID(entity.getComponent<DescriptorComponent>().id);
    RenderManager::Get()->registerDebugNode(debugspriteComponent.selectorNode);

    if (entity.getComponent<DescriptorComponent>().isDebug && Engine::Get()->isGameMode()) {
        debugspriteComponent.selectorNode->setIsDebugObject(true);
    }

    debugspriteComponent.node =
        RenderManager::Get()->sceneManager()->addBillboardSceneNode(entity.getComponent<TransformComponent>().node);

    debugspriteComponent.node->setSize(dimension2d<f32>(0.5f, 0.5f));
    // DEPRECATED
    //debugspriteComponent.node->setPosition(transform.position);
    if (entity.hasComponent<LightComponent>()) {
        auto color = entity.getComponent<LightComponent>().color_diffuse;
        debugspriteComponent.node->setColor(SColor(255, static_cast<irr::u32>(color.r * 255.0), static_cast<irr::u32>(color.g * 255.0f), static_cast<irr::u32>(color.b * 255.0f)));
    }

    debugspriteComponent.node->setMaterialTexture(0, RenderManager::Get()->driver()->getTexture(
        std::string(g_texture_path + debugspriteComponent.sprite).c_str()));
    debugspriteComponent.node->setMaterialType(EMT_TRANSPARENT_ALPHA_CHANNEL);
    debugspriteComponent.node->setMaterialFlag(EMF_LIGHTING, false);
    debugspriteComponent.node->setMaterialFlag(EMF_BILINEAR_FILTER, true);
    debugspriteComponent.node->setMaterialFlag(EMF_TRILINEAR_FILTER, true);
    debugspriteComponent.node->setMaterialFlag(EMF_ANISOTROPIC_FILTER, true);
    debugspriteComponent.node->setVisible(m_drawDebugSprites);
    debugspriteComponent.node->setID(entity.getComponent<DescriptorComponent>().id);

    if (entity.getComponent<DescriptorComponent>().isDebug && Engine::Get()->isGameMode()) {
        debugspriteComponent.node->setIsDebugObject(true);
    }

    debugspriteComponent.selectorNode->setPosition(debugspriteComponent.node->getAbsolutePosition());

	RenderManager::Get()->registerDebugNode(debugspriteComponent.node);
	entity.getComponent<RenderComponent>().isDebug = true;
}

void RenderSystem::onEntityAdded(Entity& entity)
{
	if (entity.hasComponent<MeshComponent>()) {
        setMeshComponentData(entity);
	}

    if (entity.hasComponent<DebugMeshComponent>()) {
        setDebugMeshComponentData(entity);
    }

	if (entity.hasComponent<LightComponent>()) {
        setLightComponentData(entity);
	}

    if (entity.hasComponent<BillboardSpriteComponent>()) {
        setBillboardComponentData(entity);
    }

	if (entity.hasComponent<DebugSpriteComponent>()) {
        setDebugSpriteComponentData(entity);
	}

    forceTransformUpdate();
}

void RenderSystem::onEntityRemoved(Entity& entity) {
	if (entity.hasComponent<MeshComponent>()) {
		//RenderManager::Get()->driver()->removeOcclusionQuery(entity.getComponent<MeshComponent>().node);

#ifdef USE_MULTI_PASS_RENDER
		RenderManager::Get()->effect()->removeShadowFromNode(entity.getComponent<MeshComponent>().node);
#endif
		auto& mc = entity.getComponent<MeshComponent>();
		if (mc.isViewmodel && mc.node)
			RenderManager::Get()->unregisterViewmodelNode(mc.node);
		if (entity.getComponent<RenderComponent>().isDebug && mc.node)
			RenderManager::Get()->unregisterDebugNode(mc.node);

		mc.node->remove();
        mc.selector->drop();
	}
	
	if (entity.hasComponent<LightComponent>()) {
		
		if (entity.getComponent<LightComponent>().mode != LM_STATIC)
			entity.getComponent<LightComponent>().node->remove();
	}

	if (entity.hasComponent<BillboardSpriteComponent>()) {

		/*for (auto texture : entity.getComponent<BillboardSpriteComponent>().texture_list)
		{
			texture->drop();
		}*/

		entity.getComponent<BillboardSpriteComponent>().node->remove();
		RenderManager::Get()->unregisterDebugNode(entity.getComponent<BillboardSpriteComponent>().selectorNode);
		entity.getComponent<BillboardSpriteComponent>().selectorNode->remove();
        entity.getComponent<BillboardSpriteComponent>().selector->drop();
	}

    if (entity.hasComponent<DebugMeshComponent>()) {
        auto& dmc = entity.getComponent<DebugMeshComponent>();
        if (dmc.node)
            RenderManager::Get()->unregisterDebugNode(dmc.node);
        if (dmc.node)
            dmc.node->remove();
        if (dmc.selector)
            dmc.selector->drop();
    }

    if (entity.hasComponent<DebugSpriteComponent>()) {
		if (entity.getComponent<RenderComponent>().isDebug && entity.getComponent<DebugSpriteComponent>().node)
			RenderManager::Get()->unregisterDebugNode(entity.getComponent<DebugSpriteComponent>().node);
        entity.getComponent<DebugSpriteComponent>().node->remove();
        RenderManager::Get()->unregisterDebugNode(entity.getComponent<DebugSpriteComponent>().selectorNode);
        entity.getComponent<DebugSpriteComponent>().selectorNode->remove();
        entity.getComponent<DebugSpriteComponent>().selector->drop();
    }
}

void RenderSystem::update()
{
    auto& entities = getEntities();

    for (auto& entity : entities) {
		auto& render = entity.getComponent<RenderComponent>();
		auto& transform = entity.getComponent<TransformComponent>();
        
		if (entity.hasComponent<MeshComponent>()) {
			auto& node = entity.getComponent<MeshComponent>().node;

            node->setID(entity.getComponent<DescriptorComponent>().id);

			if (entity.getComponent<MeshComponent>().isPreview)
			{
				node->setVisible(false);
			}
			else
			{
				node->setVisible(render.isVisible);
			}

			node->updateAbsolutePosition();
		}
        
		if (entity.hasComponent<LightComponent>()) {
			auto& lightComponent = entity.getComponent<LightComponent>();

			if (lightComponent.mode == LM_DYNAMIC || lightComponent.mode == LM_DYNAMIC_SHADOW)
			{
				lightComponent.node->setID(entity.getComponent<DescriptorComponent>().id);

				lightComponent.node->setVisible(render.isVisible && lightComponent.visible);

				if (lightComponent.update_component_data) {
					lightComponent.update_component_data = false;
					setLightData(lightComponent);
				}
			}

			if (entity.hasComponent<DebugSpriteComponent>()) {
				auto color = lightComponent.color_diffuse;
				entity.getComponent<DebugSpriteComponent>().node->setColor(SColor(255, static_cast<irr::u32>(color.r * 255.0f), static_cast<irr::u32>(color.g * 255.0f), static_cast<irr::u32>(color.b * 255.0f)));
			}
		}

        if (entity.hasComponent<BillboardSpriteComponent>()) {
            entity.getComponent<BillboardSpriteComponent>().node->setID(entity.getComponent<DescriptorComponent>().id);
            entity.getComponent<BillboardSpriteComponent>().selectorNode->setID(entity.getComponent<DescriptorComponent>().id);
            entity.getComponent<BillboardSpriteComponent>().selectorNode->setPosition(entity.getComponent<BillboardSpriteComponent>().node->getAbsolutePosition());

            if (Engine::Get()->isGameMode()) {
                if (entity.getComponent<BillboardSpriteComponent>().animated) {
                    auto &billboard = entity.getComponent<BillboardSpriteComponent>();

                    if (billboard.counter < billboard.texture_list.size() - 1 && !billboard.finished) {
                        if (Engine::Get()->getCurrentTime() - billboard.last_update_time > (1000.0f / billboard.fps)) {
                            billboard.counter++;
                            billboard.last_update_time = Engine::Get()->getCurrentTime();
                        }
                    } else {
                        if (!billboard.loop) {
                            billboard.finished = true;

							if (billboard.destroyOnFinish)
							{
								WorldManager::Get()->killEntityByID(entity.getComponent<DescriptorComponent>().id);
							}
                        }
                    }

                    billboard.node->setMaterialTexture(0, billboard.texture_list.at(billboard.counter));
                }
            }
        }

		if (entity.hasComponent<DebugSpriteComponent>()) {
			entity.getComponent<DebugSpriteComponent>().node->setID(entity.getComponent<DescriptorComponent>().id);
			entity.getComponent<DebugSpriteComponent>().selectorNode->setID(entity.getComponent<DescriptorComponent>().id);
			entity.getComponent<DebugSpriteComponent>().selectorNode->setPosition(entity.getComponent<DebugSpriteComponent>().node->getAbsolutePosition());
		}

        if (entity.hasComponent<DebugMeshComponent>()) {
            auto& node = entity.getComponent<DebugMeshComponent>().node;

            node->setID(entity.getComponent<DescriptorComponent>().id);

			if (g_sceneInteractor.getConfiguration().drawPointLightBounds)
			{
				if (entity.hasComponent<LightComponent>())
				{
					auto lscale = entity.getComponent<LightComponent>().radius * DEBUG_MESH_LIGHT_SPHERE_SCALE;

					node->setScale(irr::core::vector3df(lscale, lscale, lscale));

					node->setVisible(false);
				}
			}
        }
    }

	if (m_applyDebugSpriteVisSetting) {
		for (auto& entity : entities) {
			if (entity.hasComponent<DebugSpriteComponent>()) {
				entity.getComponent<DebugSpriteComponent>().node->setVisible(m_drawDebugSprites);
				entity.getComponent<DebugSpriteComponent>().selectorNode->setVisible(m_drawDebugSprites);
			}
            if (entity.hasComponent<DebugMeshComponent>()) {
				auto& dmc = entity.getComponent<DebugMeshComponent>();
				if (entity.hasComponent<LightComponent>() && dmc.mesh == "_primitive_sphere")
				{
					dmc.node->setVisible(false);
				}
				else
				{
					dmc.node->setVisible(m_drawDebugSprites);
				}
            }
		}
		m_applyDebugSpriteVisSetting = false;
	}
}

void RenderSystem::forceTransformUpdate(bool updateLights)
{
    auto& entities = getEntities();

    for (auto& entity : entities) {
        auto& render = entity.getComponent<RenderComponent>();
        auto& transform = entity.getComponent<TransformComponent>();

        if (entity.hasComponent<MeshComponent>()) {
            auto& node = entity.getComponent<MeshComponent>().node;

            node->setID(entity.getComponent<DescriptorComponent>().id);

			if (entity.getComponent<MeshComponent>().isPreview)
			{
				node->setVisible(false);
			}
			else
			{
				node->setVisible(render.isVisible);
			}
        }

        if (entity.hasComponent<LightComponent>()) {
            auto& lightComponent = entity.getComponent<LightComponent>();

			if (lightComponent.mode == LM_DYNAMIC || lightComponent.mode == LM_DYNAMIC_SHADOW)
			{
				lightComponent.node->setID(entity.getComponent<DescriptorComponent>().id);

				lightComponent.node->setVisible(render.isVisible && lightComponent.visible);

				if (lightComponent.update_component_data) {
					lightComponent.update_component_data = false;
					setLightData(lightComponent);
				}
			}

			if (entity.hasComponent<DebugSpriteComponent>()) {
				auto color = lightComponent.color_diffuse;
				entity.getComponent<DebugSpriteComponent>().node->setColor(SColor(255, static_cast<irr::u32>(color.r * 255.0f), static_cast<irr::u32>(color.g * 255.0f), static_cast<irr::u32>(color.b * 255.0f)));
			}
        }

        if (entity.hasComponent<BillboardSpriteComponent>()) {
            entity.getComponent<BillboardSpriteComponent>().node->setID(entity.getComponent<DescriptorComponent>().id);
            entity.getComponent<BillboardSpriteComponent>().selectorNode->setID(entity.getComponent<DescriptorComponent>().id);
            entity.getComponent<BillboardSpriteComponent>().selectorNode->setPosition(entity.getComponent<BillboardSpriteComponent>().node->getAbsolutePosition());
        }

        if (entity.hasComponent<DebugSpriteComponent>()) {
            entity.getComponent<DebugSpriteComponent>().node->setID(entity.getComponent<DescriptorComponent>().id);
            entity.getComponent<DebugSpriteComponent>().selectorNode->setID(entity.getComponent<DescriptorComponent>().id);
            entity.getComponent<DebugSpriteComponent>().selectorNode->setPosition(entity.getComponent<DebugSpriteComponent>().node->getAbsolutePosition());
        }

        if (entity.hasComponent<DebugMeshComponent>()) {
            auto& node = entity.getComponent<DebugMeshComponent>().node;

            node->setID(entity.getComponent<DescriptorComponent>().id);

			if (g_sceneInteractor.getConfiguration().drawPointLightBounds)
			{
				if (entity.hasComponent<LightComponent>())
				{
					auto lscale = entity.getComponent<LightComponent>().radius * DEBUG_MESH_LIGHT_SPHERE_SCALE;

					node->setScale(irr::core::vector3df(lscale, lscale, lscale));

					node->setVisible(false);
				}
			}
        }
    }

	if (m_applyDebugSpriteVisSetting) {
		for (auto& entity : entities) {
			if (entity.hasComponent<DebugSpriteComponent>()) {
				entity.getComponent<DebugSpriteComponent>().node->setVisible(m_drawDebugSprites);
				entity.getComponent<DebugSpriteComponent>().selectorNode->setVisible(m_drawDebugSprites);
			}
			if (entity.hasComponent<DebugMeshComponent>()) {
				auto& dmc = entity.getComponent<DebugMeshComponent>();
				if (entity.hasComponent<LightComponent>() && dmc.mesh == "_primitive_sphere")
				{
					dmc.node->setVisible(false);
				}
				else
				{
					dmc.node->setVisible(m_drawDebugSprites);
				}
			}
		}
		m_applyDebugSpriteVisSetting = false;
	}
}

void RenderSystem::remove(std::string name)
{
    auto& entities = getEntities();

    for (auto& entity : entities) {

        if (entity.getComponent<DescriptorComponent>().name == name) {

            entity.getComponent<MeshComponent>().node->remove();
            return;
        }
    }
}

void RenderSystem::setVisible(std::string name, bool visible)
{
    auto& entities = getEntities();

    for (auto& entity : entities) {

        if (entity.getComponent<DescriptorComponent>().name == name) {
			if (entity.hasComponent<MeshComponent>()) {
				entity.getComponent<MeshComponent>().node->setVisible(visible);
			}
            return;
        }
    }
}

void RenderSystem::setFrame(entityid id, irr::s32 frame)
{
	auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(id);

	if (entity.hasComponent<MeshComponent>()) {

		auto& render = entity.getComponent<MeshComponent>();

		if (render.isAnimated) {
		    render.node->setCurrentFrame(static_cast<irr::f32>(frame));
		}

	    if (render.isAnimated && render.trimesh->getFrameCount() < 2) {
			spdlog::warn("RenderSystem::setFrame failed to play animation, no animation frames exist in node");
	    }

	}
}

void RenderSystem::setFrameLoop(entityid id, irr::s32 begin, irr::s32 end)
{
	auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(id);

	if (entity.hasComponent<MeshComponent>()) {

		auto& render = entity.getComponent<MeshComponent>();

		if (render.isAnimated) { render.node->setFrameLoop(begin, end); }

	}
}

void RenderSystem::setFrame(std::string name, irr::s32 frame)
{
    auto& entity = WorldManager::Get()->managerSystem()->getEntityByName(name);

    if (entity.hasComponent<MeshComponent>()) {

        auto& render = entity.getComponent<MeshComponent>();
		
		if (render.isAnimated) { render.node->setCurrentFrame(static_cast<irr::f32>(frame)); }
    }
}

void RenderSystem::setFrameLoop(std::string name, irr::s32 begin, irr::s32 end)
{
    auto& entity = WorldManager::Get()->managerSystem()->getEntityByName(name);

    if (entity.hasComponent<MeshComponent>()) {

        auto& render = entity.getComponent<MeshComponent>();

        if (render.isAnimated) { render.node->setFrameLoop(begin, end); }

    }
}

void RenderSystem::playAnimation(entityid id, std::string animation)
{
	auto& entities = getEntities();

	for (auto& entity : entities) {
		if (entity.hasComponent<MeshComponent>()) {
			if (entity.getComponent<DescriptorComponent>().id == id) { // Don't 'for loop' for an ID its stupid and slow

				auto start = 0U, end = 0U;
				auto& render = entity.getComponent<MeshComponent>();

				for (sAnimationData s : render.animationList) {
					if (s.name == animation) {
						start = s.frames.X;
						end = s.frames.Y;

                        render.lastPlayedAnimation = sAnimationData(s.name, s.frames.X, s.frames.Y, s.loop);
					}
				}

                render.node->setLoopMode(false);
				render.node->setFrameLoop(start, end);

				return;
			}
		}
	}
}

void RenderSystem::loopAnimation(entityid id, std::string animation)
{
    auto& entities = getEntities();

    for (auto& entity : entities) {
        if (entity.hasComponent<MeshComponent>()) {
            if (entity.getComponent<DescriptorComponent>().id == id) { // Don't 'for loop' for an ID its stupid and slow

                auto start = 0U, end = 0U;
                auto& render = entity.getComponent<MeshComponent>();

                for (sAnimationData s : render.animationList) {
                    if (s.name == animation) {
                        start = s.frames.X;
                        end = s.frames.Y;
                    }
                }

                render.node->setLoopMode(true);
                render.node->setFrameLoop(start, end);

                return;
            }
        }
    }
}

void RenderSystem::playAnimation(const anax::Entity& entity, std::string animation)
{
	if (entity.hasComponent<MeshComponent>()) {
		auto start = 0U, end = 0U;
		auto& render = entity.getComponent<MeshComponent>();

		for (sAnimationData s : render.animationList) {
			if (s.name == animation) {
				start = s.frames.X;
				end = s.frames.Y;
			}
		}

		render.node->setFrameLoop(start, end);
	}
}

bool RenderSystem::getEntityAABBIntersection(entityid e1, entityid e2)
{
	auto& entity1 = WorldManager::Get()->managerSystem()->getEntityByID(e1);
	auto& entity2 = WorldManager::Get()->managerSystem()->getEntityByID(e2);

	if (entity1.isValid() && entity2.isValid()) {
		if (entity1.hasComponent<MeshComponent>() && entity2.hasComponent<MeshComponent>()) {
			return RenderManager::getAABBIntersection(entity1.getComponent<MeshComponent>().node, entity2.getComponent<MeshComponent>().node);
		}
	}

	spdlog::warn("Entity invalid/missing required component in RenderSystem::getEntityAABBIntersection");

	return false;
}

bool RenderSystem::swapMesh(entityid id, std::string file)
{
	auto& entities = getEntities();

	for (auto& entity : entities) {
		if (entity.hasComponent<MeshComponent>()) {
			if (entity.getComponent<DescriptorComponent>().id == id) {
				if (entity.getComponent<MeshComponent>().trimesh && entity.getComponent<MeshComponent>().node) {
					auto mesh = RenderManager::Get()->sceneManager()->getMesh(file.c_str());
				    
					if (mesh) {
						entity.getComponent<MeshComponent>().trimesh = mesh;
						entity.getComponent<MeshComponent>().node->setMesh(entity.getComponent<MeshComponent>().trimesh);
						return true;
					}
				}
            }
		}
	}

	spdlog::warn("No ID match in RenderSystem::swapMesh");

	return false;
}

// CRITICAL BUG: If 'texture_id' is greater than '... textures().size()' fatal program termination will occur
void RenderSystem::swapTexture(entityid id, unsigned int texture_id)
{
    auto& entities = getEntities();

    for (auto& entity : entities) {
        if (entity.hasComponent<MeshComponent>()) {
            if (entity.getComponent<DescriptorComponent>().id == id) {
                if (entity.getComponent<MeshComponent>().node) {
                    if (texture_id > entity.getComponent<MeshComponent>().node->getMaterialCount()) {
                        return;
                    }

                    entity.getComponent<MeshComponent>().node->setMaterialTexture(0, 
						RenderManager::Get()->driver()->getTexture(
							entity.getComponent<MeshComponent>().textures.at(texture_id).c_str()));
                    return;
                }
            }
        }
    }

    spdlog::warn("No ID match in RenderSystem::swapTexture");
}

void RenderSystem::setNodeMaterialType(entityid id, irr::video::E_MATERIAL_TYPE material_type)
{
    auto& entities = getEntities();

    for (auto& entity : entities) {
        if (entity.hasComponent<MeshComponent>()) {
            if (entity.getComponent<DescriptorComponent>().id == id) {
                if (entity.getComponent<MeshComponent>().node) {
                    entity.getComponent<MeshComponent>().node->setMaterialType(material_type);
                    //RenderManager::Get()->renderer()->getMaterialSwapper()->swapMaterials(entity.getComponent<MeshComponent>().node);
                    return;
                }
            }
        }
    }

    spdlog::warn("No ID match in RenderSystem::swapTexture");
}
