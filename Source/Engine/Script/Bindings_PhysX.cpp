#include "Bindings.h"

#include "Engine/Engine.h"

#include "irrlicht.h"

#include "PxPhysics.h"
#include "ScriptExceptions.h"

using namespace anax;
using namespace std;
using namespace physx;
using namespace irr::core;

inline PxVec3 pxvec2irrvec3(vector3df v)
{
    return PxVec3(v.X, v.Y, v.Z);
}

static void AS_Displace_CCT(int e, vector3df d)
{
    try 
    {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);

        if (!entity.isValid())
            throw ex_ent_invalid_name;

        if (entity.hasComponent<CCTComponent>())
            entity.getComponent<CCTComponent>().displacement = pxvec2irrvec3(d);
        else
            throw ex_ent_invalid_comp;
    }
    catch (exception& ex)
    {
        spdlog::error(std::string(ex.what()) + "\'" + std::to_string(e) + "\'" + std::string(" Function: ScriptBindings::AS_Displace_CCT"));
    }
}

static void AS_Resize_CCT(int e, float y)
{
    try 
    {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);

        if (!entity.isValid())
            throw ex_ent_invalid_name;

        if (entity.hasComponent<CCTComponent>())
            entity.getComponent<CCTComponent>().controller->resize(y);
        else
            throw ex_ent_invalid_comp;
    }
    catch (exception& ex)
    {
        spdlog::error(std::string(ex.what()) + "\'" + std::to_string(e) + "\'" + std::string(" Function: ScriptBindings::AS_Resize_CCT"));
    }
}

// Gross, please fix
static vector3df g_lastRayHit = vector3df(0, 0, 0);
static bool AS_Raycast(vector3df p, vector3df d, float md = 1000)
{
    auto hit = PhysicsManager::Get()->raycast(p, d, md);

    if (hit.data.getNbAnyHits()) 
    {
        g_lastRayHit.X = hit.data.getAnyHit(0).position.x;
        g_lastRayHit.Y = hit.data.getAnyHit(0).position.y;
        g_lastRayHit.Z = hit.data.getAnyHit(0).position.z;
    }

    return hit.hit;
}

static vector3df AS_Get_last_Raycast_Hit()
{
    return g_lastRayHit;
}

static vector3df AS_Get_Kinematic_Position(int e)
{
    try 
    {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);

        if (!entity.isValid())
            throw ex_ent_invalid_name;

        if (entity.hasComponent<PhysicsComponent>()) {
            if (entity.getComponent<PhysicsComponent>().kinematic) {
                return WorldManager::Get()->physicsSystem()->getKinematicActorPosition(WorldManager::Get()->managerSystem()->getNameByID(e));
            }
        }
        else
            throw ex_ent_invalid_comp;
    }
    catch (exception& ex)
    {
        spdlog::error(std::string(ex.what()) + "\'" + std::to_string(e) + "\'" + std::string(" Function: ScriptBindings::AS_Get_Kinematic_Position"));
    }

    return vector3df();
}

static void AS_Displace_Kinemat(int e, vector3df d)
{
    try 
    {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);

        if (!entity.isValid())
            throw ex_ent_invalid_name;

        if (entity.hasComponent<PhysicsComponent>()) {
            if (entity.getComponent<PhysicsComponent>().kinematic) {
                WorldManager::Get()->physicsSystem()->setKinematicActorPosition(WorldManager::Get()->managerSystem()->getNameByID(e), d);
            }
        }
        else
            throw ex_ent_invalid_comp;
    }
    catch (exception& ex)
    {
        spdlog::error(std::string(ex.what()) + "\'" + std::to_string(e) + "\'" + std::string(" Function: ScriptBindings::AS_Displace_Kinemat"));
    }
}

void ScriptBindings::RegisterPhysX(asIScriptEngine* engine)
{
    engine->SetDefaultNamespace("physx");

    engine->RegisterGlobalFunction("void displaceCharacterController(int entityId, vector3d displacement)", asFUNCTION(AS_Displace_CCT), asCALL_CDECL);
    engine->RegisterGlobalFunction("void setCharacterControllerHeight(int entityId, float height)", asFUNCTION(AS_Resize_CCT), asCALL_CDECL);

    engine->RegisterGlobalFunction("bool raycastHit(vector3d origin, vector3d direction)", asFUNCTION(AS_Raycast), asCALL_CDECL);
    engine->RegisterGlobalFunction("bool raycastHit(vector3d origin, vector3d direction, float maxDist)", asFUNCTION(AS_Raycast), asCALL_CDECL);

    engine->RegisterGlobalFunction("vector3d getLastRaycastHitPos()", asFUNCTION(AS_Get_last_Raycast_Hit), asCALL_CDECL);

    engine->RegisterGlobalFunction("vector3d getKinematicPosition(int entityId)", asFUNCTION(AS_Get_Kinematic_Position), asCALL_CDECL);
    engine->RegisterGlobalFunction("void setKinematicPosition(int entityId, vector3d position)", asFUNCTION(AS_Displace_Kinemat), asCALL_CDECL);

    engine->SetDefaultNamespace("");
}