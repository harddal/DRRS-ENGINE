#include "Bindings.h"

#include "Engine/World/WorldManager.h"
#include "Engine/World/Components.h"
#include "ScriptExceptions.h"

#include <spdlog/spdlog.h>

static void AS_Tween_SetActive(entityid e, bool state)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<TweenComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<TweenComponent>().active = state;
    }
    catch (std::exception& ex) {
        spdlog::error("{} entity:{} Function: AS_Tween_SetActive", ex.what(), e);
    }
}

static bool AS_Tween_GetActive(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<TweenComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<TweenComponent>().active;
    }
    catch (std::exception& ex) {
        spdlog::error("{} entity:{} Function: AS_Tween_GetActive", ex.what(), e);
    }
    return false;
}

static float AS_Tween_GetSpeed(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<TweenComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<TweenComponent>().speed;
    }
    catch (std::exception& ex) {
        spdlog::error("{} entity:{} Function: AS_Tween_GetSpeed", ex.what(), e);
    }
    return 0.0f;
}

static void AS_Tween_SetSpeed(entityid e, float v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<TweenComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<TweenComponent>().speed = v;
    }
    catch (std::exception& ex) {
        spdlog::error("{} entity:{} Function: AS_Tween_SetSpeed", ex.what(), e);
    }
}

static float AS_Tween_GetT(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<TweenComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<TweenComponent>().t;
    }
    catch (std::exception& ex) {
        spdlog::error("{} entity:{} Function: AS_Tween_GetT", ex.what(), e);
    }
    return 0.0f;
}

static void AS_Tween_SetT(entityid e, float v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<TweenComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<TweenComponent>().t = v;
    }
    catch (std::exception& ex) {
        spdlog::error("{} entity:{} Function: AS_Tween_SetT", ex.what(), e);
    }
}

static irr::core::vector3df AS_Tween_GetStartPosition(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<TweenComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<TweenComponent>().startPosition;
    }
    catch (std::exception& ex) {
        spdlog::error("{} entity:{} Function: AS_Tween_GetStartPosition", ex.what(), e);
    }
    return irr::core::vector3df();
}

static void AS_Tween_SetStartPosition(entityid e, irr::core::vector3df v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<TweenComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<TweenComponent>().startPosition = v;
    }
    catch (std::exception& ex) {
        spdlog::error("{} entity:{} Function: AS_Tween_SetStartPosition", ex.what(), e);
    }
}

static irr::core::vector3df AS_Tween_GetStartRotation(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<TweenComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<TweenComponent>().startRotation;
    }
    catch (std::exception& ex) {
        spdlog::error("{} entity:{} Function: AS_Tween_GetStartRotation", ex.what(), e);
    }
    return irr::core::vector3df();
}

static void AS_Tween_SetStartRotation(entityid e, irr::core::vector3df v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<TweenComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<TweenComponent>().startRotation = v;
    }
    catch (std::exception& ex) {
        spdlog::error("{} entity:{} Function: AS_Tween_SetStartRotation", ex.what(), e);
    }
}

static irr::core::vector3df AS_Tween_GetTargetPosition(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<TweenComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<TweenComponent>().targetPosition;
    }
    catch (std::exception& ex) {
        spdlog::error("{} entity:{} Function: AS_Tween_GetTargetPosition", ex.what(), e);
    }
    return irr::core::vector3df();
}

static void AS_Tween_SetTargetPosition(entityid e, irr::core::vector3df v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<TweenComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<TweenComponent>().targetPosition = v;
    }
    catch (std::exception& ex) {
        spdlog::error("{} entity:{} Function: AS_Tween_SetTargetPosition", ex.what(), e);
    }
}

static irr::core::vector3df AS_Tween_GetTargetRotation(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<TweenComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<TweenComponent>().targetRotation;
    }
    catch (std::exception& ex) {
        spdlog::error("{} entity:{} Function: AS_Tween_GetTargetRotation", ex.what(), e);
    }
    return irr::core::vector3df();
}

static void AS_Tween_SetTargetRotation(entityid e, irr::core::vector3df v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<TweenComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<TweenComponent>().targetRotation = v;
    }
    catch (std::exception& ex) {
        spdlog::error("{} entity:{} Function: AS_Tween_SetTargetRotation", ex.what(), e);
    }
}

static int AS_Tween_GetMovingBufferIndex(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<TweenComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<TweenComponent>().movingBufferIndex;
    }
    catch (std::exception& ex) {
        spdlog::error("{} entity:{} Function: AS_Tween_GetMovingBufferIndex", ex.what(), e);
    }
    return 0;
}

static void AS_Tween_SetMovingBufferIndex(entityid e, int v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<TweenComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<TweenComponent>().movingBufferIndex = v;
    }
    catch (std::exception& ex) {
        spdlog::error("{} entity:{} Function: AS_Tween_SetMovingBufferIndex", ex.what(), e);
    }
}

static void AS_Tween_Toggle(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<TweenComponent>()) throw ex_ent_invalid_comp;
        auto& tw = entity.getComponent<TweenComponent>();
        tw.active = !tw.active;
    }
    catch (std::exception& ex) {
        spdlog::error("{} entity:{} Function: AS_Tween_Toggle", ex.what(), e);
    }
}

void ScriptBindings::RegisterTween(asIScriptEngine* engine)
{
    engine->SetDefaultNamespace("tween");
    {
        engine->RegisterGlobalFunction("void setActive(int entityId, bool active)",
            asFUNCTION(AS_Tween_SetActive), asCALL_CDECL);
        engine->RegisterGlobalFunction("bool getActive(int entityId)",
            asFUNCTION(AS_Tween_GetActive), asCALL_CDECL);
        engine->RegisterGlobalFunction("void toggle(int entityId)",
            asFUNCTION(AS_Tween_Toggle), asCALL_CDECL);

        engine->RegisterGlobalFunction("float speed(int entityId)",                       asFUNCTION(AS_Tween_GetSpeed),            asCALL_CDECL);
        engine->RegisterGlobalFunction("void speed(int entityId, float speed)",           asFUNCTION(AS_Tween_SetSpeed),            asCALL_CDECL);
        engine->RegisterGlobalFunction("float t(int entityId)",                           asFUNCTION(AS_Tween_GetT),                asCALL_CDECL);
        engine->RegisterGlobalFunction("void t(int entityId, float t)",                   asFUNCTION(AS_Tween_SetT),                asCALL_CDECL);
        engine->RegisterGlobalFunction("vector3d startPosition(int entityId)",            asFUNCTION(AS_Tween_GetStartPosition),    asCALL_CDECL);
        engine->RegisterGlobalFunction("void startPosition(int entityId, vector3d pos)",  asFUNCTION(AS_Tween_SetStartPosition),    asCALL_CDECL);
        engine->RegisterGlobalFunction("vector3d startRotation(int entityId)",            asFUNCTION(AS_Tween_GetStartRotation),    asCALL_CDECL);
        engine->RegisterGlobalFunction("void startRotation(int entityId, vector3d rot)",  asFUNCTION(AS_Tween_SetStartRotation),    asCALL_CDECL);
        engine->RegisterGlobalFunction("vector3d targetPosition(int entityId)",           asFUNCTION(AS_Tween_GetTargetPosition),   asCALL_CDECL);
        engine->RegisterGlobalFunction("void targetPosition(int entityId, vector3d pos)", asFUNCTION(AS_Tween_SetTargetPosition),   asCALL_CDECL);
        engine->RegisterGlobalFunction("vector3d targetRotation(int entityId)",           asFUNCTION(AS_Tween_GetTargetRotation),   asCALL_CDECL);
        engine->RegisterGlobalFunction("void targetRotation(int entityId, vector3d rot)", asFUNCTION(AS_Tween_SetTargetRotation),   asCALL_CDECL);
        engine->RegisterGlobalFunction("int movingBufferIndex(int entityId)",             asFUNCTION(AS_Tween_GetMovingBufferIndex), asCALL_CDECL);
        engine->RegisterGlobalFunction("void movingBufferIndex(int entityId, int index)", asFUNCTION(AS_Tween_SetMovingBufferIndex), asCALL_CDECL);
    }
    engine->SetDefaultNamespace("");
}
