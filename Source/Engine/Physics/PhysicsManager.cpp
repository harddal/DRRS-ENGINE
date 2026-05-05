#include "Engine/Physics/PhysicsManager.h"

#include <fstream>
#include <thread>

#include <spdlog/spdlog.h>

#include "Application.h"
#include "Utility/Utility.h"

#include <scene.h>
#include <Importer.hpp>
#include <postprocess.h>
#include <cereal/archives/xml.hpp>

using namespace physx;

PhysicsManager* PhysicsManager::s_Instance = nullptr;

PhysicsManager::PhysicsManager() :
    m_substepCount(2),
    m_accumulator(0.0f),
    m_stepSize(1.0f / 60.f),
    m_sceneDesc(nullptr),
    m_foundation(nullptr),
    m_physics(nullptr),
    m_dispatcher(nullptr),
    m_scene(nullptr),
    m_cctManager(nullptr),
    m_cooking(nullptr),
    m_material(nullptr),
    m_gravity(PxVec3(0, -9.81f, 0))
{
    if (s_Instance)
    {
        Utility::Error("Pointer to class \'PhysicsManager\' is invalid");
    }
    s_Instance = this;

    try
    {
        std::ifstream ifs_physics("config/physics.xml");
        cereal::XMLInputArchive physics_config(ifs_physics);

        physics_config(m_configuration);
    }
    catch (cereal::Exception& ex)
    {
        spdlog::get("core")->warn("Failed to load physics configuration: {}, default values used", ex.what());

        m_configuration = PhysicsConfiguration();

        std::ofstream ofs_physics("config/physics.xml");
        cereal::XMLOutputArchive physics_config(ofs_physics);

        physics_config(m_configuration);
    }

    m_scale.length = PHYSX_DEFAULT_SCALE;
    m_scale.speed  = PHYSX_DEFAULT_SPEED;
	
    m_foundation = PxCreateFoundation(PX_PHYSICS_VERSION, m_allocator, m_errorCallback);
    if (!m_foundation)
    {
        Utility::Error("PhysX::PxCreateFoundation Failed");
    }

#ifdef ENABLE_DEBUG
    bool recordMemoryAllocations = true;
#else
    bool recordMemoryAllocations = false;
#endif

	m_pvd = PxCreatePvd(*m_foundation);
	m_transport = PxDefaultPvdSocketTransportCreate("127.0.0.1", 5425, 10);
	auto pvd_connect = m_pvd->connect(*m_transport, PxPvdInstrumentationFlag::eALL);

    m_physics = PxCreatePhysics(PX_PHYSICS_VERSION, *m_foundation, m_scale, /*recordMemoryAllocations*/false, m_pvd);
    if (!m_physics)
    {
        Utility::Error("PhysX::PxCreatePhysics failed!");
    }

	PxCookingParams cookingParams(m_scale);
	// IMPORTANT: GPU cooking disabled - causes crashes in nvgpucomp32.dll with convex meshes
	cookingParams.buildGPUData = false;  // Was: m_configuration.gpu
	
	// Enable mesh welding to fix stacked TrenchBroom box issues
	cookingParams.meshWeldTolerance = 0.01f;  // Merge vertices within 0.01 units
	cookingParams.meshPreprocessParams = PxMeshPreprocessingFlag::eWELD_VERTICES;
	
    m_cooking = PxCreateCooking(PX_PHYSICS_VERSION, *m_foundation, cookingParams);
    if (!m_cooking)
    {
        Utility::Error("PhysX::PxCreateCooking failed!");
    }

    int nthreads = m_configuration.threads;
    if (static_cast<unsigned int>(nthreads) > std::thread::hardware_concurrency()) { nthreads = std::thread::hardware_concurrency(); }
#ifdef PHYSX_LIMIT_MAX_THREADS
    if (static_cast<unsigned int>(nthreads) > PHYSX_MAX_THREADS) { nthreads = PHYSX_MAX_THREADS; }
#endif
    if (nthreads < 0) { nthreads = 0; }

	static bool using_gpu = false;
	// DISABLED: CUDA context manager creation causes nvgpucomp32.dll crashes with convex meshes
	// Even creating the context manager loads GPU libs, so we skip it entirely
	/*
	if (m_configuration.gpu)
	{
		PxCudaContextManagerDesc cudaContextManagerDesc;
		cudaContextManagerDesc.interopMode = PxCudaInteropMode::NO_INTEROP;
		
		m_cuda = PxCreateCudaContextManager(*m_foundation, cudaContextManagerDesc, PxGetProfilerCallback());

		if (m_cuda)
		{
			if (!m_cuda->contextIsValid())
			{
				m_cuda->release();
				m_cuda = nullptr;

				m_configuration.gpu = false;
				using_gpu = false;

				spdlog::error("PxCudaContext is invalid, PhysX GPU acceleration is not supported on this system");
			}
			else
			{
				using_gpu = true;
				spdlog::info("PxCudaContext created successfully, using GPU acceleration");
			}
		}
	}
	*/
	spdlog::info("PhysicsManager: GPU acceleration disabled (prevents convex mesh crashes)");
	//		}
	//	}
	//	else
	//	{
	//		spdlog::error("Failed to initialize PxCudaContextManager, PhysX GPU acceleration is not supported on this system");

	//		m_configuration.gpu = false;
	//		using_gpu = false;
	//	}
	//}

	m_dispatcher = PxDefaultCpuDispatcherCreate(nthreads);
	if (!m_dispatcher) { Utility::Error("PhysX::CreateCpuDispatcher failed! Attempted to allocate " + std::to_string(nthreads) + " threads"); }

	spdlog::info(std::string("PhysX Version {0}.{1}.{2} utilizing {3} CPU threads") + std::string(using_gpu ? " with GPU Acceleration" : " "), 
		PX_PHYSICS_VERSION_MAJOR, PX_PHYSICS_VERSION_MINOR, PX_PHYSICS_VERSION_BUGFIX, nthreads);

	if (m_cuda)
	{
		spdlog::info("CUDA Driver Version {0} on device {1}", m_cuda->getDriverVersion(), m_cuda->getDeviceName());
	}

	if (!pvd_connect)
	{
		spdlog::warn("PhysX - Failed to connect to PVD at 127.0.0.1:5425");
	}
	else
	{
		spdlog::debug("PhysX - Connected to PVD at 127.0.0.1:5425");
	}

    m_material = m_physics->createMaterial(PHYSX_DEFAULT_MATERIAL);
}
PhysicsManager::~PhysicsManager()
{
    destroyScene();

	m_cuda->release();
    m_dispatcher->release();
    m_physics->release();
	m_pvd->release();
	m_transport->release();
    m_foundation->release();

    delete s_Instance;
}

void PhysicsManager::update(irr::f32 dt)
{
    for (auto i = 0U; i < m_substepCount; i++) 
    {
        m_scene->simulate(m_stepSize);
        m_scene->fetchResults(true);
    }
}

void PhysicsManager::createScene()
{
    if (!m_scene) 
    {
        if (!m_sceneDesc)
        {
            m_sceneDesc = new PxSceneDesc(m_physics->getTolerancesScale());

            m_sceneDesc->gravity = PxVec3(PHYSX_DEFAULT_GRAVITY);
            m_sceneDesc->cpuDispatcher = m_dispatcher;
            m_sceneDesc->filterShader = PxDefaultSimulationFilterShader;

			if (m_configuration.gpu)
			{
				// DISABLED: GPU dynamics cause crashes with convex meshes (nvgpucomp32.dll)
				// m_sceneDesc->cudaContextManager = m_cuda;
				// m_sceneDesc->flags |= PxSceneFlag::eENABLE_GPU_DYNAMICS;
				// m_sceneDesc->broadPhaseType = PxBroadPhaseType::eGPU;
				
				spdlog::warn("PhysicsManager: GPU acceleration disabled (incompatible with convex meshes)");
			
				m_sceneDesc->gpuMaxNumPartitions = 8;
			}
        }

        m_scene = m_physics->createScene(*m_sceneDesc);
    }
    if (!m_cctManager) 
    {
       m_cctManager = PxCreateControllerManager(*m_scene);
    }
}
void PhysicsManager::destroyScene()
{
    if (m_cctManager) 
    {
        if (m_cctManager->getNbControllers() > 0) 
        {
            m_cctManager->purgeControllers();
        }
    }

    if (m_cctManager) 
    {
        m_cctManager->release();
        m_cctManager = nullptr;
    }

    if (m_scene) 
    {
        m_scene->release();
        m_scene = nullptr;
    }

    if (m_sceneDesc)
    {
        delete m_sceneDesc;
        m_sceneDesc = nullptr;
    }
}

//void PhysicsManager::cookTriangleMeshFromFile(std::string file, PxRigidDynamic*& actor, PxVec3 pos, PxQuat rot, PxVec3 scale)
//{
//	Assimp::Importer importer;
//	auto scene = const_cast<aiScene*>(importer.ReadFile(file, aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_MakeLeftHanded | aiProcess_FlipWindingOrder));
//
//	if (!scene || scene->mFlags == AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
//		spdlog::error("PhysicsManager::cookTriangleMeshFromFile() - Failed to import physics mesh error: {0}", std::string(importer.GetErrorString()));
//		return;
//	}
//
//	std::vector<aiMesh*> meshes;
//	for (auto i = 0U; i < scene->mRootNode->mNumMeshes; i++) {
//		meshes.push_back(scene->mMeshes[scene->mRootNode->mMeshes[i]]);
//	}
//
//	for (aiMesh* m : meshes) {
//		std::vector<PxVec3> vertices;
//		std::vector<PxU32> indices;
//
//		for (auto i = 0U; i < m->mNumVertices; i++) {
//			PxVec3 v;
//
//			v.x = m->mVertices[i].x;
//			v.y = m->mVertices[i].y;
//			v.z = m->mVertices[i].z;
//
//			vertices.push_back(v);
//		}
//
//		for (auto i = 0U; i < m->mNumFaces; i++) {
//			aiFace face = m->mFaces[i];
//			for (auto j = 0U; j < face.mNumIndices; j++) {
//				indices.push_back(face.mIndices[j]);
//			}
//		}
//
//		this->cookTriangleMesh(actor, vertices.size(), indices.size(), &vertices[0], &indices[0], pos, rot, scale);
//
//		vertices.clear();
//		indices.clear();
//	}
//
//	importer.FreeScene();
//}
//
//void PhysicsManager::cookTriangleMesh(PxRigidDynamic*& actor, PxU32 vertexCount, PxU32 indexCount, void* vertices, void* indices, PxVec3 pos, PxQuat rot, PxVec3 scale)
//{
//    PxTriangleMeshDesc desc;
//
//    desc.points.count = vertexCount;
//    desc.points.stride = sizeof(PxVec3);
//    desc.points.data = vertices;
//    
//    desc.triangles.count = indexCount / 3;
//    desc.triangles.stride = 3 * sizeof(PxU32);
//    desc.triangles.data = indices;
//
//	PxDefaultMemoryOutputStream writeBuffer;
//	bool status = m_cooking->cookTriangleMesh(desc, writeBuffer);
//	if (!status)
//	{
//		return;
//	}
//
//	PxDefaultMemoryInputData readBuffer(writeBuffer.getData(), writeBuffer.getSize());
//
//	PxTriangleMesh* triMesh = m_physics->createTriangleMesh(readBuffer);
//	actor = m_physics->createRigidDynamic(PxTransform(pos, rot));
//
//	actor->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
//
//	PxTriangleMeshGeometry triGeom;
//	triGeom.triangleMesh = triMesh;
//
//	// DEBUG: Not sure if the right way to do this in PhysX 4.4
//	PxShape* TriMeshShape = m_physics->createShape(triGeom, *m_material);
//
//	PxFilterData filterData;
//	filterData.word0 = RHG_STATIC;
//	TriMeshShape->setQueryFilterData(filterData);
//
//	// DEBUG: Not sure if the right way to do this in PhysX 4.4
//	actor->attachShape(*TriMeshShape);
//
//	m_scene->addActor(*actor);
//}

static irr::core::vector3df getVertexPos(void* verts, irr::video::E_VERTEX_TYPE type, irr::u32 i)
{
    switch (type)
    {
    case irr::video::EVT_2TCOORDS:  return static_cast<irr::video::S3DVertex2TCoords*>(verts)[i].Pos;
    case irr::video::EVT_TANGENTS:  return static_cast<irr::video::S3DVertexTangents*>(verts)[i].Pos;
    default:                        return static_cast<irr::video::S3DVertex*>(verts)[i].Pos;
    }
}

void PhysicsManager::cookTriangleMeshFromMemory(irr::scene::IMesh* trimesh, PxRigidDynamic*& actor, PxVec3 pos, PxQuat rot, PxVec3 scale)
{
	// Collect ALL vertices and indices from ALL mesh buffers into single arrays
	// This merges fragmented geometry (e.g., TrenchBroom brushes) into one continuous mesh
	std::vector<PxVec3> allVertices;
	std::vector<PxU32> allIndices;

	spdlog::debug("PhysicsManager: Merging {} mesh buffers into single collision mesh", trimesh->getMeshBufferCount());

	for (auto bufferIdx = 0U; bufferIdx < trimesh->getMeshBufferCount(); bufferIdx++)
	{
		auto* mesh = trimesh->getMeshBuffer(bufferIdx);

		irr::u16* ilist = mesh->getIndices();
		void* verts = mesh->getVertices();
		irr::video::E_VERTEX_TYPE vtype = mesh->getVertexType();

		// Track the vertex offset for this buffer (for index adjustment)
		PxU32 vertexOffset = static_cast<PxU32>(allVertices.size());

		for (auto i = 0U; i < mesh->getVertexCount(); i++)
		{
			irr::core::vector3df p = getVertexPos(verts, vtype, i);
			allVertices.push_back(PxVec3(p.X * scale.x, p.Y * scale.y, p.Z * scale.z));
		}

		// Detect quads vs triangles based on index count
		irr::u32 indexCount = mesh->getIndexCount();
		bool isQuadMesh = false;

		// Conservative heuristic: Only treat as quads if there's strong evidence
		// - Divisible by 4 (could be quads)
		// - NOT divisible by 3 (can't be triangles)
		// Examples: 8, 16, 20, 28, 32, 40, 44... are quads
		// Examples: 12, 24, 36, 48... are triangles (divisible by both, assume triangles)
		if (indexCount % 4 == 0 && indexCount % 3 != 0)
		{
			isQuadMesh = true;
			spdlog::debug("PhysicsManager: Buffer {} - Detected QUAD mesh ({} indices)", bufferIdx, indexCount);
		}
		else if (indexCount % 3 == 0)
		{
			spdlog::debug("PhysicsManager: Buffer {} - Detected TRIANGLE mesh ({} indices)", bufferIdx, indexCount);
		}
		else
		{
			spdlog::warn("PhysicsManager: Buffer {} - Unusual index count {} (not cleanly divisible by 3 or 4)", 
				bufferIdx, indexCount);
		}

		if (isQuadMesh)
		{
			// Convert quads to triangles (split each quad into 2 triangles)
			spdlog::debug("PhysicsManager: Buffer {} - Triangulating {} quads -> {} triangles", 
				bufferIdx, indexCount / 4, (indexCount / 4) * 2);

			for (auto i = 0U; i < indexCount; i += 4)
			{
				// First triangle: 0, 1, 2 (adjusted by vertex offset)
				allIndices.push_back(vertexOffset + ilist[i + 0]);
				allIndices.push_back(vertexOffset + ilist[i + 1]);
				allIndices.push_back(vertexOffset + ilist[i + 2]);

				// Second triangle: 0, 2, 3 (adjusted by vertex offset)
				allIndices.push_back(vertexOffset + ilist[i + 0]);
				allIndices.push_back(vertexOffset + ilist[i + 2]);
				allIndices.push_back(vertexOffset + ilist[i + 3]);
			}
		}
		else
		{
			// Assume triangles, copy indices with vertex offset adjustment
			for (auto i = 0U; i < indexCount; i++)
			{
				allIndices.push_back(vertexOffset + ilist[i]);
			}
		}
	}

	spdlog::debug("PhysicsManager: Created merged mesh with {} vertices, {} triangles", 
		allVertices.size(), allIndices.size() / 3);

	// Now cook the SINGLE merged mesh
	PxTriangleMeshDesc desc;

	desc.points.count = static_cast<PxU32>(allVertices.size());
	desc.points.stride = sizeof(PxVec3);
	desc.points.data = allVertices.data();

	desc.triangles.count = static_cast<PxU32>(allIndices.size() / 3);
	desc.triangles.stride = 3 * sizeof(PxU32);
	desc.triangles.data = allIndices.data();

	PxDefaultMemoryOutputStream writeBuffer;
	bool status = m_cooking->cookTriangleMesh(desc, writeBuffer);
	if (!status)
	{
		spdlog::error("PhysicsManager: Failed to cook triangle mesh");
		return;
	}

	PxDefaultMemoryInputData readBuffer(writeBuffer.getData(), writeBuffer.getSize());

	PxTriangleMesh* triMesh = m_physics->createTriangleMesh(readBuffer);
	
	// Log triangle count after cooking (will be less if duplicates were removed)
	spdlog::debug("PhysicsManager: Cooked mesh has {} triangles (removed {} duplicates)", 
		triMesh->getNbTriangles(), 
		(allIndices.size() / 3) - triMesh->getNbTriangles());
	
	actor = m_physics->createRigidDynamic(PxTransform(pos, rot));

	actor->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);

	PxTriangleMeshGeometry triGeom;
	triGeom.triangleMesh = triMesh;

	PxShape* TriMeshShape = m_physics->createShape(triGeom, *m_material);

	PxFilterData filterData;
	filterData.word0 = RHG_STATIC;
	TriMeshShape->setQueryFilterData(filterData);

	actor->attachShape(*TriMeshShape);

	m_scene->addActor(*actor);
}

void PhysicsManager::cookStaticTriangleMeshFromMemory(irr::scene::IMesh* trimesh, PxRigidStatic*& actor, PxVec3 pos, PxQuat rot, PxVec3 scale)
{
	std::vector<PxVec3> allVertices;
	std::vector<PxU32> allIndices;

	for (auto bufferIdx = 0U; bufferIdx < trimesh->getMeshBufferCount(); bufferIdx++)
	{
		auto* mesh = trimesh->getMeshBuffer(bufferIdx);

		irr::u16* ilist = mesh->getIndices();
		void* verts = mesh->getVertices();
		irr::video::E_VERTEX_TYPE vtype = mesh->getVertexType();

		PxU32 vertexOffset = static_cast<PxU32>(allVertices.size());

		for (auto i = 0U; i < mesh->getVertexCount(); i++)
		{
			irr::core::vector3df p = getVertexPos(verts, vtype, i);
			allVertices.push_back(PxVec3(p.X * scale.x, p.Y * scale.y, p.Z * scale.z));
		}

		irr::u32 indexCount = mesh->getIndexCount();
		bool isQuadMesh = (indexCount % 4 == 0 && indexCount % 3 != 0);

		if (isQuadMesh)
		{
			for (auto i = 0U; i < indexCount; i += 4)
			{
				allIndices.push_back(vertexOffset + ilist[i + 0]);
				allIndices.push_back(vertexOffset + ilist[i + 1]);
				allIndices.push_back(vertexOffset + ilist[i + 2]);
				allIndices.push_back(vertexOffset + ilist[i + 0]);
				allIndices.push_back(vertexOffset + ilist[i + 2]);
				allIndices.push_back(vertexOffset + ilist[i + 3]);
			}
		}
		else
		{
			for (auto i = 0U; i < indexCount; i++)
				allIndices.push_back(vertexOffset + ilist[i]);
		}
	}

	PxTriangleMeshDesc desc;
	desc.points.count    = static_cast<PxU32>(allVertices.size());
	desc.points.stride   = sizeof(PxVec3);
	desc.points.data     = allVertices.data();
	desc.triangles.count = static_cast<PxU32>(allIndices.size() / 3);
	desc.triangles.stride = 3 * sizeof(PxU32);
	desc.triangles.data  = allIndices.data();

	PxDefaultMemoryOutputStream writeBuffer;
	if (!m_cooking->cookTriangleMesh(desc, writeBuffer))
	{
		spdlog::error("PhysicsManager::cookStaticTriangleMeshFromMemory: failed to cook mesh");
		return;
	}

	PxDefaultMemoryInputData readBuffer(writeBuffer.getData(), writeBuffer.getSize());
	PxTriangleMesh* triMesh = m_physics->createTriangleMesh(readBuffer);

	actor = m_physics->createRigidStatic(PxTransform(pos, rot));

	PxTriangleMeshGeometry triGeom;
	triGeom.triangleMesh = triMesh;

	PxShape* shape = m_physics->createShape(triGeom, *m_material);
	PxFilterData filterData;
	filterData.word0 = RHG_STATIC;
	shape->setQueryFilterData(filterData);

	actor->attachShape(*shape);
	shape->release();

	m_scene->addActor(*actor);
}

void PhysicsManager::cookConvexMeshFromMemory(irr::scene::IMesh* trimesh, PxRigidStatic*& actor, PxVec3 pos, PxQuat rot, PxVec3 scale)
{
	spdlog::debug("PhysicsManager: Cooking {} mesh buffers as convex compound", trimesh->getMeshBufferCount());

	// Create a single static actor to hold all convex shapes
	actor = m_physics->createRigidStatic(PxTransform(pos, rot));

	for (auto bufferIdx = 0U; bufferIdx < trimesh->getMeshBufferCount(); bufferIdx++)
	{
		auto* mesh = trimesh->getMeshBuffer(bufferIdx);

		void* verts = mesh->getVertices();
		irr::video::E_VERTEX_TYPE vtype = mesh->getVertexType();

		std::vector<PxVec3> vertices;
		for (auto i = 0U; i < mesh->getVertexCount(); i++)
		{
			irr::core::vector3df p = getVertexPos(verts, vtype, i);
			vertices.push_back(PxVec3(p.X * scale.x, p.Y * scale.y, p.Z * scale.z));
		}

		// Validate vertex count
		if (vertices.size() < 4)
		{
			spdlog::warn("PhysicsManager: Buffer {} has only {} vertices (need at least 4 for convex hull), skipping", 
				bufferIdx, vertices.size());
			continue;
		}

		// Cook convex hull for this buffer using absolute vertex positions
		PxConvexMeshDesc convexDesc;
		convexDesc.points.count = static_cast<PxU32>(vertices.size());
		convexDesc.points.stride = sizeof(PxVec3);
		convexDesc.points.data = vertices.data();
		convexDesc.flags = PxConvexFlag::eCOMPUTE_CONVEX | PxConvexFlag::eCHECK_ZERO_AREA_TRIANGLES;
		
		// Validate descriptor before cooking
		if (!convexDesc.isValid())
		{
			spdlog::error("PhysicsManager: Invalid convex mesh descriptor for buffer {}", bufferIdx);
			continue;
		}

		PxDefaultMemoryOutputStream writeBuffer;
		PxConvexMeshCookingResult::Enum result;
		bool status = m_cooking->cookConvexMesh(convexDesc, writeBuffer, &result);
		
		if (!status || result != PxConvexMeshCookingResult::eSUCCESS)
		{
			spdlog::warn("PhysicsManager: Failed to cook convex hull for buffer {} (result: {})", bufferIdx, result);
			continue;
		}

		PxDefaultMemoryInputData readBuffer(writeBuffer.getData(), writeBuffer.getSize());
		PxConvexMesh* convexMesh = m_physics->createConvexMesh(readBuffer);

		// Create shape with identity transform (hull already in correct position)
		PxConvexMeshGeometry convexGeom(convexMesh);
		PxShape* shape = m_physics->createShape(convexGeom, *m_material);

		PxFilterData filterData;
		filterData.word0 = RHG_STATIC;
		shape->setQueryFilterData(filterData);

		actor->attachShape(*shape);
		shape->release();

		spdlog::debug("PhysicsManager: Buffer {} - Created convex hull with {} vertices", bufferIdx, vertices.size());
	}

	m_scene->addActor(*actor);

	spdlog::debug("PhysicsManager: Created static compound actor with {} convex shapes", actor->getNbShapes());
}


RaycastData PhysicsManager::raycast(irr::core::vector3df origin, irr::core::vector3df direction, double maxDistance, int group)
{
    RaycastData raycastData;

    // Define what parts of PxRaycastHit we're interested in
	const PxHitFlags outputFlags = PxHitFlag::ePOSITION | PxHitFlag::eNORMAL;

    PxQueryFilterData filterData = PxQueryFilterData();

	// BUG: PhysX doesn't care what the filter flags are ???
	/*switch (group)
	{
	case RHG_ANY_HIT:
		filterData.flags.set(physx::PxQueryFlag::eANY_HIT);
		break;
	case RHG_DYNAMIC:
		filterData.flags.set(physx::PxQueryFlag::eDYNAMIC);
		break;
	case RHG_NO_BLOCK:
		filterData.flags.set(physx::PxQueryFlag::eNO_BLOCK);
		break;
	case RHG_POSTFILTER:
		filterData.flags.set(physx::PxQueryFlag::ePOSTFILTER);
		break;
	case RHG_PREFILTER:
		filterData.flags.set(physx::PxQueryFlag::ePREFILTER);
		break;
	case RHG_STATIC:
		filterData.flags.set(physx::PxQueryFlag::eSTATIC);
		break;
	default:
		filterData.flags.set(physx::PxQueryFlag::eANY_HIT);
		break;
	}*/
	
    raycastData.hit = m_scene->raycast(
        PxVec3(origin.X, origin.Y, origin.Z),
        PxVec3(direction.X, direction.Y, direction.Z),
        PxReal(maxDistance), raycastData.data, outputFlags, filterData);

    return raycastData;
}

PxController* PhysicsManager::createCCT(PxCapsuleControllerDesc desc)
{
    return m_cctManager->createController(desc);
}

void PhysicsManager::saveConfiguration(PhysicsConfiguration configuration)
{
	std::ofstream ofs_physics("config/physics.xml");
	cereal::XMLOutputArchive physics_config(ofs_physics);

	m_configuration = configuration;
	physics_config(configuration);
}
