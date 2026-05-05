#include "Bindings.h"

#include "Engine/World/WorldManager.h"
#include "Engine/World/Components.h"
#include "ScriptExceptions.h"

#include <spdlog/spdlog.h>

static float AS_NavAgent_GetMoveSpeed(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NavAgentComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<NavAgentComponent>().moveSpeed;
    }
    catch (std::exception& ex) {
        spdlog::error("{} entity:{} Function: AS_NavAgent_GetMoveSpeed", ex.what(), e);
    }
    return 0.0f;
}

static void AS_NavAgent_SetMoveSpeed(entityid e, float v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NavAgentComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<NavAgentComponent>().moveSpeed = v;
    }
    catch (std::exception& ex) {
        spdlog::error("{} entity:{} Function: AS_NavAgent_SetMoveSpeed", ex.what(), e);
    }
}

static float AS_NavAgent_GetArrivalRadius(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NavAgentComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<NavAgentComponent>().arrivalRadius;
    }
    catch (std::exception& ex) {
        spdlog::error("{} entity:{} Function: AS_NavAgent_GetArrivalRadius", ex.what(), e);
    }
    return 0.0f;
}

static void AS_NavAgent_SetArrivalRadius(entityid e, float v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NavAgentComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<NavAgentComponent>().arrivalRadius = v;
    }
    catch (std::exception& ex) {
        spdlog::error("{} entity:{} Function: AS_NavAgent_SetArrivalRadius", ex.what(), e);
    }
}

static bool AS_NavAgent_GetHasPath(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NavAgentComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<NavAgentComponent>().hasPath;
    }
    catch (std::exception& ex) {
        spdlog::error("{} entity:{} Function: AS_NavAgent_GetHasPath", ex.what(), e);
    }
    return false;
}

static int AS_NavAgent_GetPathIndex(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NavAgentComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<NavAgentComponent>().pathIndex;
    }
    catch (std::exception& ex) {
        spdlog::error("{} entity:{} Function: AS_NavAgent_GetPathIndex", ex.what(), e);
    }
    return 0;
}

static int AS_NavAgent_GetPathSize(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NavAgentComponent>()) throw ex_ent_invalid_comp;
        return static_cast<int>(entity.getComponent<NavAgentComponent>().path.size());
    }
    catch (std::exception& ex) {
        spdlog::error("{} entity:{} Function: AS_NavAgent_GetPathSize", ex.what(), e);
    }
    return 0;
}

static irr::core::vector3df AS_NavAgent_GetWaypoint(entityid e, int index)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NavAgentComponent>()) throw ex_ent_invalid_comp;
        auto& agent = entity.getComponent<NavAgentComponent>();
        if (index >= 0 && index < static_cast<int>(agent.path.size()))
            return agent.path[index];
    }
    catch (std::exception& ex) {
        spdlog::error("{} entity:{} Function: AS_NavAgent_GetWaypoint", ex.what(), e);
    }
    return irr::core::vector3df();
}

static void AS_NavAgent_AddWaypoint(entityid e, irr::core::vector3df v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NavAgentComponent>()) throw ex_ent_invalid_comp;
        auto& agent = entity.getComponent<NavAgentComponent>();
        agent.path.push_back(v);
        agent.hasPath = true;
    }
    catch (std::exception& ex) {
        spdlog::error("{} entity:{} Function: AS_NavAgent_AddWaypoint", ex.what(), e);
    }
}

static void AS_NavAgent_SetDestination(entityid e, irr::core::vector3df v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NavAgentComponent>()) throw ex_ent_invalid_comp;
        auto& agent = entity.getComponent<NavAgentComponent>();
        agent.path.clear();
        agent.path.push_back(v);
        agent.pathIndex = 0;
        agent.hasPath = true;
    }
    catch (std::exception& ex) {
        spdlog::error("{} entity:{} Function: AS_NavAgent_SetDestination", ex.what(), e);
    }
}

static void AS_NavAgent_ClearPath(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NavAgentComponent>()) throw ex_ent_invalid_comp;
        auto& agent = entity.getComponent<NavAgentComponent>();
        agent.path.clear();
        agent.pathIndex = 0;
        agent.hasPath = false;
    }
    catch (std::exception& ex) {
        spdlog::error("{} entity:{} Function: AS_NavAgent_ClearPath", ex.what(), e);
    }
}

void ScriptBindings::RegisterNavigation(asIScriptEngine* engine)
{
    engine->SetDefaultNamespace("nav");
    {
        engine->RegisterGlobalFunction("float moveSpeed(int entityId)",                      asFUNCTION(AS_NavAgent_GetMoveSpeed),     asCALL_CDECL);
        engine->RegisterGlobalFunction("void moveSpeed(int entityId, float speed)",        asFUNCTION(AS_NavAgent_SetMoveSpeed),     asCALL_CDECL);
        engine->RegisterGlobalFunction("float arrivalRadius(int entityId)",                asFUNCTION(AS_NavAgent_GetArrivalRadius), asCALL_CDECL);
        engine->RegisterGlobalFunction("void arrivalRadius(int entityId, float radius)",   asFUNCTION(AS_NavAgent_SetArrivalRadius), asCALL_CDECL);
        engine->RegisterGlobalFunction("bool hasPath(int entityId)",                       asFUNCTION(AS_NavAgent_GetHasPath),       asCALL_CDECL);
        engine->RegisterGlobalFunction("int pathIndex(int entityId)",                      asFUNCTION(AS_NavAgent_GetPathIndex),     asCALL_CDECL);
        engine->RegisterGlobalFunction("int pathSize(int entityId)",                       asFUNCTION(AS_NavAgent_GetPathSize),      asCALL_CDECL);
        engine->RegisterGlobalFunction("vector3d waypoint(int entityId, int index)",       asFUNCTION(AS_NavAgent_GetWaypoint),      asCALL_CDECL);
        engine->RegisterGlobalFunction("void addWaypoint(int entityId, vector3d position)",asFUNCTION(AS_NavAgent_AddWaypoint),      asCALL_CDECL);
        engine->RegisterGlobalFunction("void setDestination(int entityId, vector3d position)", asFUNCTION(AS_NavAgent_SetDestination), asCALL_CDECL);
        engine->RegisterGlobalFunction("void clearPath(int entityId)",                     asFUNCTION(AS_NavAgent_ClearPath),        asCALL_CDECL);
    }
    engine->SetDefaultNamespace("");
}
