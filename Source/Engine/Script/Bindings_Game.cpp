#include "Bindings.h"

#include "Engine/Engine.h"
#include "Engine/Navigation/NavigationManager.h"

#include "ScriptExceptions.h"
#include "Game/Components.h"
#include "Game/Item/ItemDatabase.h"
#include "Game/Player/InventoryController.h"
#include "Game/Player/PlayerController.h"

#include <boost/filesystem.hpp>
#include <boost/range/iterator_range.hpp>
#include <algorithm>
#include <iomanip>
#include <set>
#include <sstream>

bool AS_GetCVarExists(std::string name)
{
	return WorldManager::Get()->getCVarExists(name);
}
std::string AS_GetCVarValue(std::string name)
{
	return WorldManager::Get()->getCVarValue(name);
}
void AS_SetCVar(std::string name, std::string value)
{
	WorldManager::Get()->setCVar(name, value);
}
void AS_RemoveCVar(std::string name)
{
	WorldManager::Get()->removeCVar(name);
}
void AS_ClearCVars()
{
	WorldManager::Get()->clearCVars();
}

static void AS_ExecuteCommand(std::string cmd)
{
	// NOIMP
}

static void AS_SaveScene(std::string file)
{
    WorldManager::Get()->exportScene(file);
}

static void AS_LoadScene(std::string file)
{
	WorldManager::Get()->loadScene(file);
}

static std::string AS_SaveCheckpoint(int maxSlots)
{
	namespace fs = boost::filesystem;
	boost::system::error_code ec;

	if (maxSlots <= 0) maxSlots = 1;

	const std::string saveDir = "saves";
	fs::create_directories(saveDir, ec);

	struct Slot { fs::path path; std::time_t writeTime; int index; };
	std::vector<Slot> slots;
	std::set<int>     usedIndices;

	for (auto& entry : boost::make_iterator_range(fs::directory_iterator(saveDir, ec), {}))
	{
		const std::string name = entry.path().filename().string();
		if (name.size() > 15 &&
			name.substr(0, 11) == "checkpoint_" &&
			name.substr(name.size() - 4) == ".pak")
		{
			try {
				int idx = std::stoi(name.substr(11, name.size() - 15));
				slots.push_back({ entry.path(), fs::last_write_time(entry.path(), ec), idx });
				usedIndices.insert(idx);
			} catch (...) {}
		}
	}

	std::string targetPath;

	if (static_cast<int>(slots.size()) < maxSlots)
	{
		int next = 0;
		while (usedIndices.count(next)) ++next;

		std::ostringstream oss;
		oss << saveDir << "/checkpoint_"
		    << std::setw(3) << std::setfill('0') << next << ".pak";
		targetPath = oss.str();
	}
	else
	{
		auto oldest = std::min_element(slots.begin(), slots.end(),
			[](const Slot& a, const Slot& b){ return a.writeTime < b.writeTime; });
		targetPath = oldest->path.string();
	}

	WorldManager::Get()->queueSceneSave(targetPath);
	return targetPath;
}

void AS_ActivateLogicComponent(int e, bool b)
{
	auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);

	if (!entity.isValid())
	{
		return;
	}

	if (entity.hasComponent<LogicComponent>()) 
	{
		entity.getComponent<LogicComponent>().isActivated = b;
	}
}

bool AS_IsLogicComponentActivated(int e)
{
	auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);

	if (!entity.isValid())
	{
		return false;
	}

	if (entity.hasComponent<LogicComponent>()) 
	{
		return entity.getComponent<LogicComponent>().isActivated;
	}

	return false;
}

static void AS_DamageEntity(int e, int d)
{
	auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);

	if (!entity.isValid())
	{
		return;
	}

	WorldManager::Get()->gameplaySystem()->damageEntity(e, d);

}

static void AS_HealEntity(int e, int d)
{
	auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);

	if (!entity.isValid())
	{
		return;
	}

	WorldManager::Get()->gameplaySystem()->healEntity(e, d);
}

void as_interact(entityid e)
{
	WorldManager::Get()->gameplaySystem()->interact(e);
}

bool as_getPlayerInventoryDisplayState()
{
	return g_PlayerInventoryIsDisplaying;
}

static void DamagePlayer(int d)
{
	WorldManager::Get()->gameplaySystem()->damageEntity(WorldManager::Get()->managerSystem()->getIDByName("player"), d);
}

static void HealPlayer(int d)
{
	WorldManager::Get()->gameplaySystem()->healEntity(WorldManager::Get()->managerSystem()->getIDByName("player"), d);
}

// ---- Inventory ----
// String-keyed throughout. The old int-id overloads are gone with the numeric
// ids themselves: those came from directory iteration order, so a script saying
// give(4) meant a different item the moment a .item file was added.

static void player_giveitem(std::string item)
{
	g_PlayerController->inventoryController()->giveItem(item, 1);
}

static void player_giveitemcount(std::string item, int count)
{
	g_PlayerController->inventoryController()->giveItem(item, count);
}

bool as_isPlayerMoving()
{
	return g_PlayerController->isMoving();
}

static void player_removeitem(std::string item)
{
	g_PlayerController->inventoryController()->removeItem(item, 1);
}

static bool player_useitem(std::string item)
{
	return g_PlayerController->inventoryController()->useItem(item);
}

int GetPlayerHealth()
{
	return g_PlayerController->getCurrentHealth();
}
int GetPlayerMaxHealth()
{
	return g_PlayerController->getMaxHealth();
}

static bool player_hasitem(std::string item)
{
	return g_PlayerController->inventoryController()->hasItem(item);
}

static int player_itemcount(std::string item)
{
	return g_PlayerController->inventoryController()->itemCount(item);
}

bool player_isswimming()
{
	return g_PlayerController->isSwimming();
}
bool player_isheadunderwater()
{
	return g_PlayerController->isHeadUnderWater();
}

bool player_isblocking()
{
	return g_PlayerController->isBlocking();
}
void player_setblocking(bool blocking)
{
	g_PlayerController->setIsBlocking(blocking);
}

void as_lockplayerforinput(bool lock)
{
	g_LockPlayerForInput = lock;
}
bool as_isplayerlocked()
{
	return g_LockPlayerForInput;
}

// ---- AutoKillComponent ----

static unsigned int AS_AutoKill_GetLifetime(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<AutoKillComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<AutoKillComponent>().lifetime_ms;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_AutoKill_GetLifetime", ex.what(), e); }
    return 0;
}

static void AS_AutoKill_SetLifetime(entityid e, unsigned int v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<AutoKillComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<AutoKillComponent>().lifetime_ms = v;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_AutoKill_SetLifetime", ex.what(), e); }
}

// ---- InteractionComponent ----

static bool AS_Interaction_Get(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<InteractionComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<InteractionComponent>().interact;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Interaction_Get", ex.what(), e); }
    return false;
}

static void AS_Interaction_Set(entityid e, bool v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<InteractionComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<InteractionComponent>().interact = v;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Interaction_Set", ex.what(), e); }
}

// ---- LogicComponent ----

static bool AS_Logic_GetActive(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<LogicComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<LogicComponent>().isActivated;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Logic_GetActive", ex.what(), e); }
    return false;
}

static void AS_Logic_SetActive(entityid e, bool v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<LogicComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<LogicComponent>().isActivated = v;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Logic_SetActive", ex.what(), e); }
}

static std::string AS_Logic_GetReceiver(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<LogicComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<LogicComponent>().receiver;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Logic_GetReceiver", ex.what(), e); }
    return std::string();
}

static void AS_Logic_SetReceiver(entityid e, std::string v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<LogicComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<LogicComponent>().receiver = v;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Logic_SetReceiver", ex.what(), e); }
}

// ---- TriggerZoneComponent ----

static bool AS_Trigger_GetTriggered(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<TriggerZoneComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<TriggerZoneComponent>().triggered;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Trigger_GetTriggered", ex.what(), e); }
    return false;
}

static bool AS_Trigger_GetReset(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<TriggerZoneComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<TriggerZoneComponent>().reset;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Trigger_GetReset", ex.what(), e); }
    return false;
}

static void AS_Trigger_SetReset(entityid e, bool v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<TriggerZoneComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<TriggerZoneComponent>().reset = v;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Trigger_SetReset", ex.what(), e); }
}

static bool AS_Trigger_GetSingleUse(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<TriggerZoneComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<TriggerZoneComponent>().single_use;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Trigger_GetSingleUse", ex.what(), e); }
    return false;
}

static void AS_Trigger_SetSingleUse(entityid e, bool v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<TriggerZoneComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<TriggerZoneComponent>().single_use = v;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Trigger_SetSingleUse", ex.what(), e); }
}

static bool AS_Trigger_GetToggle(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<TriggerZoneComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<TriggerZoneComponent>().toggle;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Trigger_GetToggle", ex.what(), e); }
    return false;
}

static void AS_Trigger_SetToggle(entityid e, bool v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<TriggerZoneComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<TriggerZoneComponent>().toggle = v;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Trigger_SetToggle", ex.what(), e); }
}

static bool AS_Trigger_GetInvert(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<TriggerZoneComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<TriggerZoneComponent>().invert;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Trigger_GetInvert", ex.what(), e); }
    return false;
}

static void AS_Trigger_SetInvert(entityid e, bool v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<TriggerZoneComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<TriggerZoneComponent>().invert = v;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Trigger_SetInvert", ex.what(), e); }
}

static int AS_Trigger_GetMask(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<TriggerZoneComponent>()) throw ex_ent_invalid_comp;
        return static_cast<int>(entity.getComponent<TriggerZoneComponent>().mask);
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Trigger_GetMask", ex.what(), e); }
    return 0;
}

static void AS_Trigger_SetMask(entityid e, int v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<TriggerZoneComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<TriggerZoneComponent>().mask = static_cast<TRIGGER_ZONE_MASK>(v);
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Trigger_SetMask", ex.what(), e); }
}

static std::string AS_Trigger_GetEntity(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<TriggerZoneComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<TriggerZoneComponent>().entity;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Trigger_GetEntity", ex.what(), e); }
    return std::string();
}

static void AS_Trigger_SetEntity(entityid e, std::string v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<TriggerZoneComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<TriggerZoneComponent>().entity = v;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Trigger_SetEntity", ex.what(), e); }
}

static std::string AS_Trigger_GetTriggeredEntity(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<TriggerZoneComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<TriggerZoneComponent>().triggered_entity;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Trigger_GetTriggeredEntity", ex.what(), e); }
    return std::string();
}

// ---- MarkerComponent ----

static int AS_Marker_GetType(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<MarkerComponent>()) throw ex_ent_invalid_comp;
        return static_cast<int>(entity.getComponent<MarkerComponent>().type);
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Marker_GetType", ex.what(), e); }
    return 0;
}

static void AS_Marker_SetType(entityid e, int v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<MarkerComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<MarkerComponent>().type = static_cast<MARKER_TYPE>(v);
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Marker_SetType", ex.what(), e); }
}

static bool AS_Marker_GetHasUpdated(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<MarkerComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<MarkerComponent>().hasUpdated;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Marker_GetHasUpdated", ex.what(), e); }
    return false;
}

static void AS_Marker_SetHasUpdated(entityid e, bool v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<MarkerComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<MarkerComponent>().hasUpdated = v;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Marker_SetHasUpdated", ex.what(), e); }
}

// ---- DamageReceiverComponent ----

static int AS_HP_GetCurrent(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<DamageReceiverComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<DamageReceiverComponent>().health;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_HP_GetCurrent", ex.what(), e); }
    return 0;
}

static void AS_HP_SetCurrent(entityid e, int v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<DamageReceiverComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<DamageReceiverComponent>().health = v;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_HP_SetCurrent", ex.what(), e); }
}

static int AS_HP_GetThreshold(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<DamageReceiverComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<DamageReceiverComponent>().threshold;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_HP_GetThreshold", ex.what(), e); }
    return 0;
}

static void AS_HP_SetThreshold(entityid e, int v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<DamageReceiverComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<DamageReceiverComponent>().threshold = v;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_HP_SetThreshold", ex.what(), e); }
}

static bool AS_HP_GetInvulnerable(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<DamageReceiverComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<DamageReceiverComponent>().invulnerable;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_HP_GetInvulnerable", ex.what(), e); }
    return false;
}

static void AS_HP_SetInvulnerable(entityid e, bool v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<DamageReceiverComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<DamageReceiverComponent>().invulnerable = v;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_HP_SetInvulnerable", ex.what(), e); }
}

static bool AS_HP_GetBuddha(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<DamageReceiverComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<DamageReceiverComponent>().buddha;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_HP_GetBuddha", ex.what(), e); }
    return false;
}

static void AS_HP_SetBuddha(entityid e, bool v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<DamageReceiverComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<DamageReceiverComponent>().buddha = v;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_HP_SetBuddha", ex.what(), e); }
}

static bool AS_HP_DidReceiveDamage(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<DamageReceiverComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<DamageReceiverComponent>().didReceiveDamage();
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_HP_DidReceiveDamage", ex.what(), e); }
    return false;
}

// ---- DataComponent ----

static int AS_Data_Size(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<DataComponent>()) throw ex_ent_invalid_comp;
        return static_cast<int>(entity.getComponent<DataComponent>().data.size());
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Data_Size", ex.what(), e); }
    return 0;
}

static std::string AS_Data_Get(entityid e, int index)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<DataComponent>()) throw ex_ent_invalid_comp;
        auto& d = entity.getComponent<DataComponent>().data;
        if (index >= 0 && index < static_cast<int>(d.size()))
            return d[index];
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Data_Get", ex.what(), e); }
    return std::string();
}

static void AS_Data_Set(entityid e, int index, std::string v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<DataComponent>()) throw ex_ent_invalid_comp;
        auto& d = entity.getComponent<DataComponent>().data;
        if (index >= 0 && index < static_cast<int>(d.size()))
            d[index] = v;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Data_Set", ex.what(), e); }
}

static void AS_Data_Add(entityid e, std::string v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<DataComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<DataComponent>().data.push_back(v);
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Data_Add", ex.what(), e); }
}

static void AS_Data_Clear(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<DataComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<DataComponent>().data.clear();
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Data_Clear", ex.what(), e); }
}

// ---- ItemComponent ----

static std::string AS_Item_GetName(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<ItemComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<ItemComponent>().item;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Item_GetName", ex.what(), e); }
    return std::string();
}

static void AS_Item_SetName(entityid e, std::string v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<ItemComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<ItemComponent>().item = v;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Item_SetName", ex.what(), e); }
}

static std::string AS_Item_GetData(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<ItemComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<ItemComponent>().data;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Item_GetData", ex.what(), e); }
    return std::string();
}

static void AS_Item_SetData(entityid e, std::string v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<ItemComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<ItemComponent>().data = v;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Item_SetData", ex.what(), e); }
}

// ---- DialogComponent ----

static bool AS_Dialog_GetActive(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<DialogComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<DialogComponent>().active;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Dialog_GetActive", ex.what(), e); }
    return false;
}

static void AS_Dialog_SetActive(entityid e, bool v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<DialogComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<DialogComponent>().active = v;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Dialog_SetActive", ex.what(), e); }
}

static int AS_Dialog_Size(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<DialogComponent>()) throw ex_ent_invalid_comp;
        return static_cast<int>(entity.getComponent<DialogComponent>().data.size());
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Dialog_Size", ex.what(), e); }
    return 0;
}

static std::string AS_Dialog_Get(entityid e, int index)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<DialogComponent>()) throw ex_ent_invalid_comp;
        auto& d = entity.getComponent<DialogComponent>().data;
        if (index >= 0 && index < static_cast<int>(d.size()))
            return d[index];
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Dialog_Get", ex.what(), e); }
    return std::string();
}

static void AS_Dialog_Set(entityid e, int index, std::string v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<DialogComponent>()) throw ex_ent_invalid_comp;
        auto& d = entity.getComponent<DialogComponent>().data;
        if (index >= 0 && index < static_cast<int>(d.size()))
            d[index] = v;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Dialog_Set", ex.what(), e); }
}

static void AS_Dialog_Add(entityid e, std::string v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<DialogComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<DialogComponent>().data.push_back(v);
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Dialog_Add", ex.what(), e); }
}

static void AS_Dialog_Clear(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<DialogComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<DialogComponent>().data.clear();
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Dialog_Clear", ex.what(), e); }
}

// ---- WaterComponent ----

static irr::core::vector3df AS_Water_GetShallowColor(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<WaterComponent>()) throw ex_ent_invalid_comp;
        auto& c = entity.getComponent<WaterComponent>().shallowColor;
        return irr::core::vector3df(c[0], c[1], c[2]);
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Water_GetShallowColor", ex.what(), e); }
    return irr::core::vector3df();
}

static void AS_Water_SetShallowColor(entityid e, irr::core::vector3df v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<WaterComponent>()) throw ex_ent_invalid_comp;
        auto& c = entity.getComponent<WaterComponent>().shallowColor;
        c[0] = v.X; c[1] = v.Y; c[2] = v.Z;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Water_SetShallowColor", ex.what(), e); }
}

static irr::core::vector3df AS_Water_GetDeepColor(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<WaterComponent>()) throw ex_ent_invalid_comp;
        auto& c = entity.getComponent<WaterComponent>().deepColor;
        return irr::core::vector3df(c[0], c[1], c[2]);
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Water_GetDeepColor", ex.what(), e); }
    return irr::core::vector3df();
}

static void AS_Water_SetDeepColor(entityid e, irr::core::vector3df v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<WaterComponent>()) throw ex_ent_invalid_comp;
        auto& c = entity.getComponent<WaterComponent>().deepColor;
        c[0] = v.X; c[1] = v.Y; c[2] = v.Z;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_Water_SetDeepColor", ex.what(), e); }
}

// ---- NPCComponent ----

static bool AS_NPC_GetAlive(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<DescriptorComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<DescriptorComponent>().isAlive;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_NPC_GetAlive", ex.what(), e); }
    return false;
}

static void AS_NPC_SetAlive(entityid e, bool v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<DescriptorComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<DescriptorComponent>().isAlive = v;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_NPC_SetAlive", ex.what(), e); }
}

static std::string AS_NPC_GetDisplayName(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NPCComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<NPCComponent>().displayName;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_NPC_GetDisplayName", ex.what(), e); }
    return std::string();
}

static void AS_NPC_SetDisplayName(entityid e, std::string v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NPCComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<NPCComponent>().displayName = v;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_NPC_SetDisplayName", ex.what(), e); }
}

// Reads through factionOf(), so it answers correctly for the PLAYER too -- the
// player carries no NPCComponent and resolves off its ET_PLAYER descriptor.
static int AS_NPC_GetFaction(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        return static_cast<int>(factionOf(entity));
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_NPC_GetFaction", ex.what(), e); }
    return static_cast<int>(FACTION::NEUTRAL);
}

static void AS_NPC_SetFaction(entityid e, int f)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NPCComponent>()) throw ex_ent_invalid_comp;
        if (f < 0 || f >= static_cast<int>(FACTION::FACTION_COUNT))
            throw std::runtime_error("faction out of range");
        entity.getComponent<NPCComponent>().faction = static_cast<FACTION>(f);
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_NPC_SetFaction", ex.what(), e); }
}

static bool AS_NPC_IsHostile(entityid attacker, entityid target)
{
    try {
        auto& a = WorldManager::Get()->managerSystem()->getEntityByID(attacker);
        auto& b = WorldManager::Get()->managerSystem()->getEntityByID(target);
        if (!a.isValid() || !b.isValid()) throw ex_ent_invalid_name;
        return isHostile(a, b);
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_NPC_IsHostile", ex.what(), attacker); }
    return false;
}

// Collapses this NPC to NEUTRAL in factionOf(), which makes it work in BOTH
// directions at once: a pacified NPC neither attacks nor is attacked.
static void AS_NPC_SetPacified(entityid e, bool v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NPCComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<NPCComponent>().pacified = v;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_NPC_SetPacified", ex.what(), e); }
}

static bool AS_NPC_GetPacified(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NPCComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<NPCComponent>().pacified;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_NPC_GetPacified", ex.what(), e); }
    return false;
}

// Faction constants (match the FACTION enum order, which is APPEND-ONLY --
// see NPCComponent.h).
static const int NPC_FACTION_NEUTRAL  = 0;
static const int NPC_FACTION_PLAYER   = 1;
static const int NPC_FACTION_UNDEAD   = 2;
static const int NPC_FACTION_CULT     = 3;
static const int NPC_FACTION_CIVILIAN = 4;

void ScriptBindings::RegisterGame(asIScriptEngine* engine)
{
	engine->SetDefaultNamespace("game");
	{
		engine->RegisterGlobalFunction("void damage(int entityId, int amount)", asFUNCTION(AS_DamageEntity), asCALL_CDECL);
		engine->RegisterGlobalFunction("void heal(int entityId, int amount)", asFUNCTION(AS_HealEntity), asCALL_CDECL);

		engine->RegisterGlobalFunction("void activateLogicEvent(int entityId, bool active)", asFUNCTION(AS_ActivateLogicComponent), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool isLogicEventActive(int entityId)", asFUNCTION(AS_IsLogicComponentActivated), asCALL_CDECL);

		engine->RegisterGlobalFunction("void interact(int entityId)", asFUNCTION(as_interact), asCALL_CDECL);

		engine->RegisterGlobalFunction("void save(string filename)", asFUNCTION(AS_SaveScene), asCALL_CDECL);
		engine->RegisterGlobalFunction("void load(string filename)", asFUNCTION(AS_LoadScene), asCALL_CDECL);
		engine->RegisterGlobalFunction("string saveCheckpoint(int maxSlots)", asFUNCTION(AS_SaveCheckpoint), asCALL_CDECL);
	}

	engine->SetDefaultNamespace("player");
	{
		engine->RegisterGlobalFunction("bool isInventoryDisplayed()", asFUNCTION(as_getPlayerInventoryDisplayState), asCALL_CDECL);

		engine->RegisterGlobalFunction("bool isLocked()", asFUNCTION(as_isplayerlocked), asCALL_CDECL);
		engine->RegisterGlobalFunction("void lock(bool locked)", asFUNCTION(as_lockplayerforinput), asCALL_CDECL);

		engine->RegisterGlobalFunction("bool isMoving()", asFUNCTION(as_isPlayerMoving), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool isSwimming()", asFUNCTION(player_isswimming), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool isHeadUnderWater()", asFUNCTION(player_isheadunderwater), asCALL_CDECL);

		engine->RegisterGlobalFunction("bool isBlocking()", asFUNCTION(player_isblocking), asCALL_CDECL);
		engine->RegisterGlobalFunction("void blocking(bool blocking)", asFUNCTION(player_setblocking), asCALL_CDECL);

		engine->RegisterGlobalFunction("int health()", asFUNCTION(GetPlayerHealth), asCALL_CDECL);
		engine->RegisterGlobalFunction("int max_health()", asFUNCTION(GetPlayerMaxHealth), asCALL_CDECL);

		engine->RegisterGlobalFunction("void damage(int amount)", asFUNCTION(DamagePlayer), asCALL_CDECL);
		engine->RegisterGlobalFunction("void heal(int amount)", asFUNCTION(HealPlayer), asCALL_CDECL);

		// Items are addressed by their .item filename stem. Dropping is gone
		// along with the capacity limit that made it meaningful — without one it
		// was only a way to lose the key you needed.
		engine->RegisterGlobalFunction("void give(string item)", asFUNCTION(player_giveitem), asCALL_CDECL);
		engine->RegisterGlobalFunction("void give(string item, int count)", asFUNCTION(player_giveitemcount), asCALL_CDECL);

		engine->RegisterGlobalFunction("void remove(string item)", asFUNCTION(player_removeitem), asCALL_CDECL);

		engine->RegisterGlobalFunction("bool has(string item)", asFUNCTION(player_hasitem), asCALL_CDECL);
		engine->RegisterGlobalFunction("int count(string item)", asFUNCTION(player_itemcount), asCALL_CDECL);

		engine->RegisterGlobalFunction("bool use(string item)", asFUNCTION(player_useitem), asCALL_CDECL);
	}

	engine->SetDefaultNamespace("cvar");
	{
		engine->RegisterGlobalFunction("bool exists(string name)", asFUNCTION(AS_GetCVarExists), asCALL_CDECL);
		engine->RegisterGlobalFunction("string get(string name)", asFUNCTION(AS_GetCVarValue), asCALL_CDECL);
		engine->RegisterGlobalFunction("void set(string name, string value)", asFUNCTION(AS_SetCVar), asCALL_CDECL);
		engine->RegisterGlobalFunction("void remove(string name)", asFUNCTION(AS_RemoveCVar), asCALL_CDECL);
		engine->RegisterGlobalFunction("void remove_all()", asFUNCTION(AS_ClearCVars), asCALL_CDECL);
	}

	engine->SetDefaultNamespace("console");
	{
		engine->RegisterGlobalFunction("void execute(string command)", asFUNCTION(AS_ExecuteCommand), asCALL_CDECL);
	}

	engine->SetDefaultNamespace("autokill");
	{
		engine->RegisterGlobalFunction("uint lifetime(int entityId)",         asFUNCTION(AS_AutoKill_GetLifetime), asCALL_CDECL);
		engine->RegisterGlobalFunction("void lifetime(int entityId, uint ms)", asFUNCTION(AS_AutoKill_SetLifetime), asCALL_CDECL);
	}

	engine->SetDefaultNamespace("interaction");
	{
		engine->RegisterGlobalFunction("bool interact(int entityId)",              asFUNCTION(AS_Interaction_Get), asCALL_CDECL);
		engine->RegisterGlobalFunction("void interact(int entityId, bool state)", asFUNCTION(AS_Interaction_Set), asCALL_CDECL);
	}

	engine->SetDefaultNamespace("logic");
	{
		engine->RegisterGlobalFunction("bool active(int entityId)",               asFUNCTION(AS_Logic_GetActive),   asCALL_CDECL);
		engine->RegisterGlobalFunction("void active(int entityId, bool active)", asFUNCTION(AS_Logic_SetActive),   asCALL_CDECL);
		engine->RegisterGlobalFunction("string receiver(int entityId)",          asFUNCTION(AS_Logic_GetReceiver), asCALL_CDECL);
		engine->RegisterGlobalFunction("void receiver(int entityId, string name)",asFUNCTION(AS_Logic_SetReceiver), asCALL_CDECL);
	}

	engine->SetDefaultNamespace("trigger");
	{
		engine->RegisterGlobalFunction("bool triggered(int entityId)",                    asFUNCTION(AS_Trigger_GetTriggered),       asCALL_CDECL);
		engine->RegisterGlobalFunction("bool reset(int entityId)",                      asFUNCTION(AS_Trigger_GetReset),           asCALL_CDECL);
		engine->RegisterGlobalFunction("void reset(int entityId, bool reset)",          asFUNCTION(AS_Trigger_SetReset),           asCALL_CDECL);
		engine->RegisterGlobalFunction("bool singleUse(int entityId)",                  asFUNCTION(AS_Trigger_GetSingleUse),       asCALL_CDECL);
		engine->RegisterGlobalFunction("void singleUse(int entityId, bool singleUse)",  asFUNCTION(AS_Trigger_SetSingleUse),       asCALL_CDECL);
		engine->RegisterGlobalFunction("bool toggle(int entityId)",                     asFUNCTION(AS_Trigger_GetToggle),          asCALL_CDECL);
		engine->RegisterGlobalFunction("void toggle(int entityId, bool toggle)",        asFUNCTION(AS_Trigger_SetToggle),          asCALL_CDECL);
		engine->RegisterGlobalFunction("bool invert(int entityId)",                     asFUNCTION(AS_Trigger_GetInvert),          asCALL_CDECL);
		engine->RegisterGlobalFunction("void invert(int entityId, bool invert)",        asFUNCTION(AS_Trigger_SetInvert),          asCALL_CDECL);
		engine->RegisterGlobalFunction("int mask(int entityId)",                        asFUNCTION(AS_Trigger_GetMask),            asCALL_CDECL);
		engine->RegisterGlobalFunction("void mask(int entityId, int mask)",             asFUNCTION(AS_Trigger_SetMask),            asCALL_CDECL);
		engine->RegisterGlobalFunction("string entity(int entityId)",                   asFUNCTION(AS_Trigger_GetEntity),          asCALL_CDECL);
		engine->RegisterGlobalFunction("void entity(int entityId, string name)",        asFUNCTION(AS_Trigger_SetEntity),          asCALL_CDECL);
		engine->RegisterGlobalFunction("string triggeredEntity(int entityId)",          asFUNCTION(AS_Trigger_GetTriggeredEntity),  asCALL_CDECL);
	}

	engine->SetDefaultNamespace("marker");
	{
		engine->RegisterGlobalFunction("int type(int entityId)",                  asFUNCTION(AS_Marker_GetType),       asCALL_CDECL);
		engine->RegisterGlobalFunction("void type(int entityId, int type)",      asFUNCTION(AS_Marker_SetType),       asCALL_CDECL);
		engine->RegisterGlobalFunction("bool hasUpdated(int entityId)",          asFUNCTION(AS_Marker_GetHasUpdated), asCALL_CDECL);
		engine->RegisterGlobalFunction("void hasUpdated(int entityId, bool updated)", asFUNCTION(AS_Marker_SetHasUpdated), asCALL_CDECL);
	}

	engine->SetDefaultNamespace("hp");
	{
		engine->RegisterGlobalFunction("int current(int entityId)",                    asFUNCTION(AS_HP_GetCurrent),       asCALL_CDECL);
		engine->RegisterGlobalFunction("void current(int entityId, int hp)",         asFUNCTION(AS_HP_SetCurrent),       asCALL_CDECL);
		engine->RegisterGlobalFunction("int threshold(int entityId)",                asFUNCTION(AS_HP_GetThreshold),     asCALL_CDECL);
		engine->RegisterGlobalFunction("void threshold(int entityId, int hp)",       asFUNCTION(AS_HP_SetThreshold),     asCALL_CDECL);
		engine->RegisterGlobalFunction("bool invulnerable(int entityId)",            asFUNCTION(AS_HP_GetInvulnerable),  asCALL_CDECL);
		engine->RegisterGlobalFunction("void invulnerable(int entityId, bool state)",asFUNCTION(AS_HP_SetInvulnerable),  asCALL_CDECL);
		engine->RegisterGlobalFunction("bool buddha(int entityId)",                  asFUNCTION(AS_HP_GetBuddha),        asCALL_CDECL);
		engine->RegisterGlobalFunction("void buddha(int entityId, bool state)",      asFUNCTION(AS_HP_SetBuddha),        asCALL_CDECL);
		engine->RegisterGlobalFunction("bool didReceiveDamage(int entityId)",        asFUNCTION(AS_HP_DidReceiveDamage), asCALL_CDECL);
	}

	engine->SetDefaultNamespace("data");
	{
		engine->RegisterGlobalFunction("int size(int entityId)",                       asFUNCTION(AS_Data_Size),  asCALL_CDECL);
		engine->RegisterGlobalFunction("string get(int entityId, int index)",         asFUNCTION(AS_Data_Get),   asCALL_CDECL);
		engine->RegisterGlobalFunction("void set(int entityId, int index, string value)", asFUNCTION(AS_Data_Set), asCALL_CDECL);
		engine->RegisterGlobalFunction("void add(int entityId, string value)",        asFUNCTION(AS_Data_Add),   asCALL_CDECL);
		engine->RegisterGlobalFunction("void clear(int entityId)",                    asFUNCTION(AS_Data_Clear), asCALL_CDECL);
	}

	engine->SetDefaultNamespace("item");
	{
		engine->RegisterGlobalFunction("string name(int entityId)",              asFUNCTION(AS_Item_GetName), asCALL_CDECL);
		engine->RegisterGlobalFunction("void name(int entityId, string item)", asFUNCTION(AS_Item_SetName), asCALL_CDECL);
		engine->RegisterGlobalFunction("string data(int entityId)",            asFUNCTION(AS_Item_GetData), asCALL_CDECL);
		engine->RegisterGlobalFunction("void data(int entityId, string data)", asFUNCTION(AS_Item_SetData), asCALL_CDECL);
	}

	engine->SetDefaultNamespace("dialog");
	{
		engine->RegisterGlobalFunction("bool active(int entityId)",                asFUNCTION(AS_Dialog_GetActive), asCALL_CDECL);
		engine->RegisterGlobalFunction("void active(int entityId, bool active)", asFUNCTION(AS_Dialog_SetActive), asCALL_CDECL);
		engine->RegisterGlobalFunction("int size(int entityId)",                 asFUNCTION(AS_Dialog_Size),      asCALL_CDECL);
		engine->RegisterGlobalFunction("string get(int entityId, int index)",    asFUNCTION(AS_Dialog_Get),       asCALL_CDECL);
		engine->RegisterGlobalFunction("void set(int entityId, int index, string line)", asFUNCTION(AS_Dialog_Set), asCALL_CDECL);
		engine->RegisterGlobalFunction("void add(int entityId, string line)",    asFUNCTION(AS_Dialog_Add),       asCALL_CDECL);
		engine->RegisterGlobalFunction("void clear(int entityId)",               asFUNCTION(AS_Dialog_Clear),     asCALL_CDECL);
	}

	engine->SetDefaultNamespace("water");
	{
		engine->RegisterGlobalFunction("vector3d shallowColor(int entityId)",             asFUNCTION(AS_Water_GetShallowColor), asCALL_CDECL);
		engine->RegisterGlobalFunction("void shallowColor(int entityId, vector3d rgb)", asFUNCTION(AS_Water_SetShallowColor), asCALL_CDECL);
		engine->RegisterGlobalFunction("vector3d deepColor(int entityId)",              asFUNCTION(AS_Water_GetDeepColor),    asCALL_CDECL);
		engine->RegisterGlobalFunction("void deepColor(int entityId, vector3d rgb)",    asFUNCTION(AS_Water_SetDeepColor),    asCALL_CDECL);
	}

	engine->SetDefaultNamespace("npc");
	{
		engine->RegisterGlobalFunction("bool alive(int entityId)",                        asFUNCTION(AS_NPC_GetAlive),           asCALL_CDECL);
		engine->RegisterGlobalFunction("void alive(int entityId, bool alive)",          asFUNCTION(AS_NPC_SetAlive),           asCALL_CDECL);
		engine->RegisterGlobalFunction("string displayName(int entityId)",              asFUNCTION(AS_NPC_GetDisplayName),     asCALL_CDECL);
		engine->RegisterGlobalFunction("void displayName(int entityId, string name)",   asFUNCTION(AS_NPC_SetDisplayName),     asCALL_CDECL);

		engine->RegisterGlobalFunction("int faction(int entityId)",                     asFUNCTION(AS_NPC_GetFaction),         asCALL_CDECL);
		engine->RegisterGlobalFunction("void faction(int entityId, int faction)",       asFUNCTION(AS_NPC_SetFaction),         asCALL_CDECL);
		engine->RegisterGlobalFunction("bool isHostile(int attackerId, int targetId)",  asFUNCTION(AS_NPC_IsHostile),          asCALL_CDECL);
		engine->RegisterGlobalFunction("bool pacified(int entityId)",                   asFUNCTION(AS_NPC_GetPacified),        asCALL_CDECL);
		engine->RegisterGlobalFunction("void pacified(int entityId, bool pacified)",    asFUNCTION(AS_NPC_SetPacified),        asCALL_CDECL);

		engine->RegisterGlobalProperty("const int FACTION_NEUTRAL",  const_cast<int*>(&NPC_FACTION_NEUTRAL));
		engine->RegisterGlobalProperty("const int FACTION_PLAYER",   const_cast<int*>(&NPC_FACTION_PLAYER));
		engine->RegisterGlobalProperty("const int FACTION_UNDEAD",   const_cast<int*>(&NPC_FACTION_UNDEAD));
		engine->RegisterGlobalProperty("const int FACTION_CULT",     const_cast<int*>(&NPC_FACTION_CULT));
		engine->RegisterGlobalProperty("const int FACTION_CIVILIAN", const_cast<int*>(&NPC_FACTION_CIVILIAN));
	}

	engine->SetDefaultNamespace("");
}
