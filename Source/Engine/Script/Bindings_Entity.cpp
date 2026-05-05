#include "Bindings.h"

#include <anax/anax.hpp>

#include "Engine/Engine.h"
#include "ScriptExceptions.h"
#include "Engine/Resource/FilePaths.h"

static irr::core::vector3df AS_Get_Entity_Position(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);

        if (!entity.isValid())
            throw ex_ent_invalid_name;

        if (entity.hasComponent<TransformComponent>())
            return entity.getComponent<TransformComponent>().getPosition();

        throw ex_ent_invalid_comp;
    }
    catch (std::exception& ex) {
        spdlog::error(std::string(ex.what()) + "\'" + std::to_string(e) + "\'" + std::string(" Function: ScriptBindings::AS_Get_Entity_Position"));
    }

    return irr::core::vector3df();
}

static irr::core::vector3df AS_Get_Entity_Rotation(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);

        if (!entity.isValid())
            throw ex_ent_invalid_name;

        if (entity.hasComponent<TransformComponent>())
            return entity.getComponent<TransformComponent>().getRotation();

        throw ex_ent_invalid_comp;
    }
    catch (std::exception& ex) {
        spdlog::error(std::string(ex.what()) + "\'" + std::to_string(e) + "\'" + std::string(" Function: ScriptBindings::AS_Get_Entity_Rotation"));
    }

    return irr::core::vector3df();
}

static irr::core::vector3df AS_Get_Entity_Scale(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);

        if (!entity.isValid())
            throw ex_ent_invalid_name;

        if (entity.hasComponent<TransformComponent>())
            return entity.getComponent<TransformComponent>().getScale();

        throw ex_ent_invalid_comp;
    }
    catch (std::exception& ex) {
        spdlog::error(std::string(ex.what()) + "\'" + std::to_string(e) + "\'" + std::string(" Function: ScriptBindings::AS_Get_Entity_Scale"));
    }

    return irr::core::vector3df();
}

static void AS_Set_Entity_Position(entityid e, irr::core::vector3df v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);

        if (!entity.isValid())
            throw ex_ent_invalid_name;

        if (entity.hasComponent<TransformComponent>())
            entity.getComponent<TransformComponent>().setPosition(v);
        else
            throw ex_ent_invalid_comp;
    }
    catch (std::exception& ex) {
        spdlog::error(std::string(ex.what()) + "\'" + std::to_string(e) + "\'" + std::string(" Function: ScriptBindings::AS_Set_Entity_Position"));
    }
}

static void AS_Set_Entity_Rotation(entityid e, irr::core::vector3df v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);

        if (!entity.isValid())
            throw ex_ent_invalid_name;

        if (entity.hasComponent<TransformComponent>())
            entity.getComponent<TransformComponent>().setRotation(v);
        else
            throw ex_ent_invalid_comp;
    }
    catch (std::exception& ex) {
        spdlog::error(std::string(ex.what()) + "\'" + std::to_string(e) + "\'" + std::string(" Function: ScriptBindings::AS_Set_Entity_Rotation"));
    }
}

static void AS_Set_Entity_Scale(entityid e, irr::core::vector3df v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);

        if (!entity.isValid())
            throw ex_ent_invalid_name;

        if (entity.hasComponent<TransformComponent>())
            entity.getComponent<TransformComponent>().setScale(v);
        else
            throw ex_ent_invalid_comp;
    }
    catch (std::exception& ex) {
        spdlog::error(std::string(ex.what()) + "\'" + std::to_string(e) + "\'" + std::string(" Function: ScriptBindings::AS_Set_Entity_Scale"));
    }
}

static void AS_Move_Entity(entityid e, irr::core::vector3df v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);

        if (!entity.isValid())
            throw ex_ent_invalid_name;

        if (entity.hasComponent<TransformComponent>()) {
            entity.getComponent<TransformComponent>().setPosition(entity.getComponent<TransformComponent>().getPosition() + v);
        }
        else
            throw ex_ent_invalid_comp;
    }
    catch (std::exception& ex) 
    {
        spdlog::error(std::string(ex.what()) + "\'" + std::to_string(e) + "\'" + std::string(" Function: ScriptBindings::AS_Move_Entity"));
    }
}

static void AS_Rotate_Entity(entityid e, irr::core::vector3df v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);

        if (!entity.isValid())
            throw ex_ent_invalid_name;

        if (entity.hasComponent<TransformComponent>()) {
            entity.getComponent<TransformComponent>().setRotation(entity.getComponent<TransformComponent>().getRotation() + v);
        }
        else
            throw ex_ent_invalid_comp;
    }
    catch (std::exception& ex) {
        spdlog::error(std::string(ex.what()) + "\'" + std::to_string(e) + "\'" + std::string(" Function: ScriptBindings::AS_Rotate_Entity"));
    }
}

static std::string AS_Get_Entity_Name_By_ID(entityid id) { return WorldManager::Get()->managerSystem()->getNameByID(id); }
static int AS_Get_Entity_ID_By_Name(std::string& name) { return WorldManager::Get()->managerSystem()->getIDByName(name); }

static void AS_Set_Entity_Parent(entityid child, entityid parent)
{
    try {
        auto& child_entity = WorldManager::Get()->managerSystem()->getEntityByID(child);
        if (!child_entity.isValid())
            throw ex_ent_invalid_name;

        auto& parent_entity = WorldManager::Get()->managerSystem()->getEntityByID(parent);
        if (!parent_entity.isValid())
            throw ex_ent_invalid_name;

        if (child_entity.hasComponent<TransformComponent>() &&
            parent_entity.hasComponent<TransformComponent>()) {
			parent_entity.getComponent<TransformComponent>().addChild(child_entity.getComponent<TransformComponent>().node);
        }
        else {
            throw ex_ent_invalid_comp;
        }
    }
    catch (std::exception& ex)
    {
        spdlog::error(std::string(ex.what()) + "\'" + std::to_string(child) + ", " + std::to_string(parent) + "\'" + " Failed to set entity parent in: ScriptBindings::AS_Set_Entity_Parent");
    }
}

static void AS_Set_Camera_To_Entity(entityid child)
{
	try {
		auto& child_entity = WorldManager::Get()->managerSystem()->getEntityByID(child);
		if (!child_entity.isValid())
		{
			throw ex_ent_invalid_name;
		}

		if (child_entity.hasComponent<TransformComponent>())
		{
			auto& player = WorldManager::Get()->managerSystem()->getEntityByName("player");

			if (!player.isValid())
			{
				player = WorldManager::Get()->managerSystem()->getEntityByName("freecamera");
				if (!player.isValid())
				{
					throw ex_ent_invalid_name;
				}
			}

			//player.getComponent<TransformComponent>().addChild(child_entity.getComponent<TransformComponent>().node);

			// BUG: Doens't fully work entity lags behind camera rotation
			player.getComponent<CameraComponent>().camera->addChild(child_entity.getComponent<TransformComponent>().node);
			child_entity.getComponent<TransformComponent>().isChild = true;
			
		}
		else {
			throw ex_ent_invalid_comp;
		}
	}
	catch (std::exception& ex)
	{
		spdlog::error(std::string(ex.what()) + "\'" + std::to_string(child) + "\'" + " Failed to set camera to parent in: ScriptBindings::AS_Set_Camera_To_Entity");
	}
}

static entityid AS_Spawn_Entity_0(std::string e)
{
	// DEBUG: Do not use '_asset_ent' macro here as it converts the string 'e' to a string, causing AS to not call the function
	return WorldManager::Get()->spawnEntity(g_entity_path + e + std::string(".ent"));
}

static entityid AS_Spawn_Entity_1(std::string e, irr::core::vector3df p)
{
	// DEBUG: Do not use '_asset_ent' macro here as it converts the string 'e' to a string, causing AS to not call the function
    return WorldManager::Get()->spawnEntity(g_entity_path + e + std::string(".ent"), "", false, p);
}

static entityid AS_Spawn_Entity_2(std::string e, irr::core::vector3df p, irr::core::vector3df r)
{
	// DEBUG: Do not use '_asset_ent' macro here as it converts the string 'e' to a string, causing AS to not call the function
    return WorldManager::Get()->spawnEntity(g_entity_path + e + std::string(".ent"), "", false, p, r);
}

static entityid AS_Spawn_Entity_3(std::string e, irr::core::vector3df p, irr::core::vector3df r, irr::core::vector3df s)
{
	// DEBUG: Do not use '_asset_ent' macro here as it converts the string 'e' to a string, causing AS to not call the function
    return WorldManager::Get()->spawnEntity(g_entity_path + e + std::string(".ent"), "", false, p, r, s);
}

static void AS_Kill_Entity_By_ID(entityid e)
{
    WorldManager::Get()->killEntityByID(e);
}

static void AS_Kill_Entity_By_Name(std::string& name)
{
    WorldManager::Get()->killEntityByName(name);
}

static void AS_Camera_SwitchTo(entityid id)
{
	WorldManager::Get()->cameraSystem()->switchToCamera(id);
}

static void AS_Camera_SwitchTo_Name(std::string name)
{
	WorldManager::Get()->cameraSystem()->switchToCamera(WorldManager::Get()->managerSystem()->getIDByName(name));
}

static void AS_Camera_SwitchToPlayer()
{
	WorldManager::Get()->cameraSystem()->switchToPlayerCamera();
}

static float AS_Light_GetRadius(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (entity.hasComponent<LightComponent>())
            return entity.getComponent<LightComponent>().radius;
        throw ex_ent_invalid_comp;
    }
    catch (std::exception& ex) {
        spdlog::error(std::string(ex.what()) + "\'" + std::to_string(e) + "\'" + " Function: ScriptBindings::AS_Light_GetRadius");
    }
    return 0.0f;
}

static void AS_Light_SetRadius(entityid e, float v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (entity.hasComponent<LightComponent>()) {
            auto& light = entity.getComponent<LightComponent>();
            light.radius = v;
            light.update();
        }
        else
            throw ex_ent_invalid_comp;
    }
    catch (std::exception& ex) {
        spdlog::error(std::string(ex.what()) + "\'" + std::to_string(e) + "\'" + " Function: ScriptBindings::AS_Light_SetRadius");
    }
}

static float AS_Light_GetOuterCone(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (entity.hasComponent<LightComponent>())
            return entity.getComponent<LightComponent>().outerCone;
        throw ex_ent_invalid_comp;
    }
    catch (std::exception& ex) {
        spdlog::error(std::string(ex.what()) + "\'" + std::to_string(e) + "\'" + " Function: ScriptBindings::AS_Light_GetOuterCone");
    }
    return 0.0f;
}

static void AS_Light_SetOuterCone(entityid e, float v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (entity.hasComponent<LightComponent>()) {
            auto& light = entity.getComponent<LightComponent>();
            light.outerCone = v;
            light.update();
        }
        else
            throw ex_ent_invalid_comp;
    }
    catch (std::exception& ex) {
        spdlog::error(std::string(ex.what()) + "\'" + std::to_string(e) + "\'" + " Function: ScriptBindings::AS_Light_SetOuterCone");
    }
}

static float AS_Light_GetInnerCone(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (entity.hasComponent<LightComponent>())
            return entity.getComponent<LightComponent>().innerCone;
        throw ex_ent_invalid_comp;
    }
    catch (std::exception& ex) {
        spdlog::error(std::string(ex.what()) + "\'" + std::to_string(e) + "\'" + " Function: ScriptBindings::AS_Light_GetInnerCone");
    }
    return 0.0f;
}

static void AS_Light_SetInnerCone(entityid e, float v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (entity.hasComponent<LightComponent>()) {
            auto& light = entity.getComponent<LightComponent>();
            light.innerCone = v;
            light.update();
        }
        else
            throw ex_ent_invalid_comp;
    }
    catch (std::exception& ex) {
        spdlog::error(std::string(ex.what()) + "\'" + std::to_string(e) + "\'" + " Function: ScriptBindings::AS_Light_SetInnerCone");
    }
}

static float AS_Light_GetFalloff(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (entity.hasComponent<LightComponent>())
            return entity.getComponent<LightComponent>().falloff;
        throw ex_ent_invalid_comp;
    }
    catch (std::exception& ex) {
        spdlog::error(std::string(ex.what()) + "\'" + std::to_string(e) + "\'" + " Function: ScriptBindings::AS_Light_GetFalloff");
    }
    return 0.0f;
}

static void AS_Light_SetFalloff(entityid e, float v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (entity.hasComponent<LightComponent>()) {
            auto& light = entity.getComponent<LightComponent>();
            light.falloff = v;
            light.update();
        }
        else
            throw ex_ent_invalid_comp;
    }
    catch (std::exception& ex) {
        spdlog::error(std::string(ex.what()) + "\'" + std::to_string(e) + "\'" + " Function: ScriptBindings::AS_Light_SetFalloff");
    }
}

static irr::core::vector3df AS_Light_GetColor(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (entity.hasComponent<LightComponent>()) {
            auto& c = entity.getComponent<LightComponent>().color_diffuse;
            return irr::core::vector3df(c.r, c.g, c.b);
        }
        throw ex_ent_invalid_comp;
    }
    catch (std::exception& ex) {
        spdlog::error(std::string(ex.what()) + "\'" + std::to_string(e) + "\'" + " Function: ScriptBindings::AS_Light_GetColor");
    }
    return irr::core::vector3df();
}

static void AS_Light_SetColor(entityid e, irr::core::vector3df v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (entity.hasComponent<LightComponent>()) {
            auto& light = entity.getComponent<LightComponent>();
            light.color_diffuse.r = v.X; light.color_diffuse.g = v.Y; light.color_diffuse.b = v.Z;
            light.update();
        }
        else
            throw ex_ent_invalid_comp;
    }
    catch (std::exception& ex) {
        spdlog::error(std::string(ex.what()) + "\'" + std::to_string(e) + "\'" + " Function: ScriptBindings::AS_Light_SetColor");
    }
}

static irr::core::vector3df AS_Light_GetOffset(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (entity.hasComponent<LightComponent>())
            return entity.getComponent<LightComponent>().offset;
        throw ex_ent_invalid_comp;
    }
    catch (std::exception& ex) {
        spdlog::error(std::string(ex.what()) + "\'" + std::to_string(e) + "\'" + " Function: ScriptBindings::AS_Light_GetOffset");
    }
    return irr::core::vector3df();
}

static void AS_Light_SetOffset(entityid e, irr::core::vector3df v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (entity.hasComponent<LightComponent>()) {
            auto& light = entity.getComponent<LightComponent>();
            light.offset = v;
            light.update();
        }
        else
            throw ex_ent_invalid_comp;
    }
    catch (std::exception& ex) {
        spdlog::error(std::string(ex.what()) + "\'" + std::to_string(e) + "\'" + " Function: ScriptBindings::AS_Light_SetOffset");
    }
}

static bool AS_Light_GetVisible(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (entity.hasComponent<LightComponent>())
            return entity.getComponent<LightComponent>().visible;
        throw ex_ent_invalid_comp;
    }
    catch (std::exception& ex) {
        spdlog::error(std::string(ex.what()) + "\'" + std::to_string(e) + "\'" + " Function: ScriptBindings::AS_Light_GetVisible");
    }
    return false;
}

static void AS_Light_SetVisible(entityid e, bool v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (entity.hasComponent<LightComponent>()) {
            auto& light = entity.getComponent<LightComponent>();
            light.visible = v;
            light.update();
        }
        else
            throw ex_ent_invalid_comp;
    }
    catch (std::exception& ex) {
        spdlog::error(std::string(ex.what()) + "\'" + std::to_string(e) + "\'" + " Function: ScriptBindings::AS_Light_SetVisible");
    }
}

static int AS_Light_GetType(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (entity.hasComponent<LightComponent>())
            return static_cast<int>(entity.getComponent<LightComponent>().type);
        throw ex_ent_invalid_comp;
    }
    catch (std::exception& ex) {
        spdlog::error(std::string(ex.what()) + "\'" + std::to_string(e) + "\'" + " Function: ScriptBindings::AS_Light_GetType");
    }
    return 0;
}

static void AS_Light_SetType(entityid e, int v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (entity.hasComponent<LightComponent>()) {
            auto& light = entity.getComponent<LightComponent>();
            light.type = static_cast<LIGHT_TYPE>(v);
            light.update();
        }
        else
            throw ex_ent_invalid_comp;
    }
    catch (std::exception& ex) {
        spdlog::error(std::string(ex.what()) + "\'" + std::to_string(e) + "\'" + " Function: ScriptBindings::AS_Light_SetType");
    }
}

static int AS_Light_GetMode(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (entity.hasComponent<LightComponent>())
            return static_cast<int>(entity.getComponent<LightComponent>().mode);
        throw ex_ent_invalid_comp;
    }
    catch (std::exception& ex) {
        spdlog::error(std::string(ex.what()) + "\'" + std::to_string(e) + "\'" + " Function: ScriptBindings::AS_Light_GetMode");
    }
    return 0;
}

static void AS_Light_SetMode(entityid e, int v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (entity.hasComponent<LightComponent>()) {
            auto& light = entity.getComponent<LightComponent>();
            light.mode = static_cast<LIGHT_MODE>(v);
            light.update();
        }
        else
            throw ex_ent_invalid_comp;
    }
    catch (std::exception& ex) {
        spdlog::error(std::string(ex.what()) + "\'" + std::to_string(e) + "\'" + " Function: ScriptBindings::AS_Light_SetMode");
    }
}

void ScriptBindings::RegisterEntity(asIScriptEngine* engine)
{
    engine->SetDefaultNamespace("transform");
    {
        engine->RegisterGlobalFunction("vector3d position(int entityId)", asFUNCTION(AS_Get_Entity_Position), asCALL_CDECL);
        engine->RegisterGlobalFunction("void position(int entityId, vector3d pos)", asFUNCTION(AS_Set_Entity_Position), asCALL_CDECL);
        engine->RegisterGlobalFunction("vector3d rotation(int entityId)", asFUNCTION(AS_Get_Entity_Rotation), asCALL_CDECL);
        engine->RegisterGlobalFunction("void rotation(int entityId, vector3d rot)", asFUNCTION(AS_Set_Entity_Rotation), asCALL_CDECL);
        engine->RegisterGlobalFunction("vector3d scale(int entityId)", asFUNCTION(AS_Get_Entity_Scale), asCALL_CDECL);
        engine->RegisterGlobalFunction("void scale(int entityId, vector3d scale)", asFUNCTION(AS_Set_Entity_Scale), asCALL_CDECL);

        engine->RegisterGlobalFunction("void move(int entityId, vector3d delta)", asFUNCTION(AS_Move_Entity), asCALL_CDECL);
        engine->RegisterGlobalFunction("void rotate(int entityId, vector3d delta)", asFUNCTION(AS_Rotate_Entity), asCALL_CDECL);
    }

    engine->SetDefaultNamespace("entity");
    {
        engine->RegisterGlobalFunction("int spawn(string name)", asFUNCTION(AS_Spawn_Entity_0), asCALL_CDECL);
        engine->RegisterGlobalFunction("int spawn(string name, vector3d position)", asFUNCTION(AS_Spawn_Entity_1), asCALL_CDECL);
        engine->RegisterGlobalFunction("int spawn(string name, vector3d position, vector3d rotation)", asFUNCTION(AS_Spawn_Entity_2), asCALL_CDECL);
        engine->RegisterGlobalFunction("int spawn(string name, vector3d position, vector3d rotation, vector3d scale)", asFUNCTION(AS_Spawn_Entity_3), asCALL_CDECL);

        engine->RegisterGlobalFunction("void kill(int entityId)", asFUNCTION(AS_Kill_Entity_By_ID), asCALL_CDECL);
        engine->RegisterGlobalFunction("void kill(string name)", asFUNCTION(AS_Kill_Entity_By_Name), asCALL_CDECL);

        engine->RegisterGlobalFunction("string get(int entityId)", asFUNCTION(AS_Get_Entity_Name_By_ID), asCALL_CDECL);
        engine->RegisterGlobalFunction("int get(string name)", asFUNCTION(AS_Get_Entity_ID_By_Name), asCALL_CDECL);

        engine->RegisterGlobalFunction("void setParent(int childId, int parentId)", asFUNCTION(AS_Set_Entity_Parent), asCALL_CDECL);
        engine->RegisterGlobalFunction("void setParentCamera(int entityId)", asFUNCTION(AS_Set_Camera_To_Entity), asCALL_CDECL);
    }

    engine->SetDefaultNamespace("camera");
    {
        engine->RegisterGlobalFunction("void switchTo(int entityId)",    asFUNCTION(AS_Camera_SwitchTo),       asCALL_CDECL);
		engine->RegisterGlobalFunction("void switchTo(string name)",    asFUNCTION(AS_Camera_SwitchTo_Name), asCALL_CDECL);
        engine->RegisterGlobalFunction("void switchToPlayer()", asFUNCTION(AS_Camera_SwitchToPlayer), asCALL_CDECL);
    }

    engine->SetDefaultNamespace("light");
    {
        engine->RegisterGlobalFunction("float radius(int entityId)",                asFUNCTION(AS_Light_GetRadius),    asCALL_CDECL);
        engine->RegisterGlobalFunction("void radius(int entityId, float radius)",  asFUNCTION(AS_Light_SetRadius),    asCALL_CDECL);
        engine->RegisterGlobalFunction("float outerCone(int entityId)",            asFUNCTION(AS_Light_GetOuterCone), asCALL_CDECL);
        engine->RegisterGlobalFunction("void outerCone(int entityId, float deg)",  asFUNCTION(AS_Light_SetOuterCone), asCALL_CDECL);
        engine->RegisterGlobalFunction("float innerCone(int entityId)",            asFUNCTION(AS_Light_GetInnerCone), asCALL_CDECL);
        engine->RegisterGlobalFunction("void innerCone(int entityId, float deg)",  asFUNCTION(AS_Light_SetInnerCone), asCALL_CDECL);
        engine->RegisterGlobalFunction("float falloff(int entityId)",              asFUNCTION(AS_Light_GetFalloff),   asCALL_CDECL);
        engine->RegisterGlobalFunction("void falloff(int entityId, float falloff)",asFUNCTION(AS_Light_SetFalloff),   asCALL_CDECL);
        engine->RegisterGlobalFunction("vector3d color(int entityId)",             asFUNCTION(AS_Light_GetColor),     asCALL_CDECL);
        engine->RegisterGlobalFunction("void color(int entityId, vector3d rgb)",   asFUNCTION(AS_Light_SetColor),     asCALL_CDECL);
        engine->RegisterGlobalFunction("vector3d offset(int entityId)",            asFUNCTION(AS_Light_GetOffset),    asCALL_CDECL);
        engine->RegisterGlobalFunction("void offset(int entityId, vector3d offset)",asFUNCTION(AS_Light_SetOffset),   asCALL_CDECL);
        engine->RegisterGlobalFunction("bool visible(int entityId)",               asFUNCTION(AS_Light_GetVisible),   asCALL_CDECL);
        engine->RegisterGlobalFunction("void visible(int entityId, bool visible)", asFUNCTION(AS_Light_SetVisible),   asCALL_CDECL);
        engine->RegisterGlobalFunction("int type(int entityId)",                   asFUNCTION(AS_Light_GetType),      asCALL_CDECL);
        engine->RegisterGlobalFunction("void type(int entityId, int type)",        asFUNCTION(AS_Light_SetType),      asCALL_CDECL);
        engine->RegisterGlobalFunction("int mode(int entityId)",                   asFUNCTION(AS_Light_GetMode),      asCALL_CDECL);
        engine->RegisterGlobalFunction("void mode(int entityId, int mode)",        asFUNCTION(AS_Light_SetMode),      asCALL_CDECL);
    }

    engine->SetDefaultNamespace("");
}