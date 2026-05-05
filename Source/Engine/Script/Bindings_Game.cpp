#include "Bindings.h"

#include "Engine/Engine.h"
#include "Engine/Navigation/NavigationManager.h"

#include "ScriptExceptions.h"
#include "Game/Components.h"
#include "Game/Item/ItemDatabase.h"
#include "Game/Player/InventoryController.h"
#include "Game/Player/PlayerController.h"

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

void player_additemtoinv(int id)
{
	g_PlayerController->inventoryController()->pickupItem(id);
}
void player_additemtoinvs(std::string item)
{
	g_PlayerController->inventoryController()->pickupItem(ItemDatabase::GetItemByName(item).id);
}

bool as_isPlayerMoving()
{
	return g_PlayerController->isMoving();
}

void player_additemtoinvEnt(int id, int entid)
{
	g_PlayerController->inventoryController()->pickupItem(id, entid);
}
void player_additemtoinvsEnt(std::string item, int entid)
{
	g_PlayerController->inventoryController()->pickupItem(ItemDatabase::GetItemByName(item).id, entid);
}

void player_dropiteminv(int id)
{
	g_PlayerController->inventoryController()->dropItem(id);
}
void player_removeitemtoinv(int id)
{
	g_PlayerController->inventoryController()->removeItem(id);
}

int GetPlayerHealth()
{
	return g_PlayerController->getCurrentHealth();
}
int GetPlayerMaxHealth()
{
	return g_PlayerController->getMaxHealth();
}

void player_removeCurrentActivatedItem()
{
	g_PlayerController->inventoryController()->removeCurrentActivatedItem();
}

bool player_hasitem(int id)
{
	return g_PlayerController->inventoryController()->getFirstItemCopyOfType(id).id == id;
}
bool player_hasitemstr(std::string name)
{
	return g_PlayerController->inventoryController()->getFirstItemCopyOfType(name).name == name;
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

std::string player_getItemDataEquipped2H(std::string key)
{
	/*if (g_PlayerController->inventoryController()->getItemTwoHand().id < ITEM_NULL_ID) {
		switch (data_slot) {
		case 1:
			return g_PlayerController->inventoryController()->getItemTwoHand().data1;
		case 2:
			return g_PlayerController->inventoryController()->getItemTwoHand().data2;
		case 3:
			return g_PlayerController->inventoryController()->getItemTwoHand().data3;
		case 4:
			return g_PlayerController->inventoryController()->getItemTwoHand().data4;
		default:
			return std::string();
		}
	}*/

	if (g_PlayerController->inventoryController()->getItemTwoHand().id < ITEM_NULL_ID) 
	{
		return g_PlayerController->inventoryController()->getItemTwoHand().data;
	}

	return std::string();
}

void player_setItemDataEquipped2H(std::string key, std::string value)
{
	/*if (g_PlayerController->inventoryController()->getItemTwoHand().id < ITEM_NULL_ID) {
		switch (data_slot) {
		case 1:
			g_PlayerController->inventoryController()->getItemTwoHand().data1 = value;
			break;
		case 2:
			g_PlayerController->inventoryController()->getItemTwoHand().data2 = value;
			break;
		case 3:
			g_PlayerController->inventoryController()->getItemTwoHand().data3 = value;
			break;
		case 4:
			g_PlayerController->inventoryController()->getItemTwoHand().data4 = value;
			break;
		default:
			break;
		}
	}*/

	if (g_PlayerController->inventoryController()->getItemTwoHand().id < ITEM_NULL_ID)
	{
		g_PlayerController->inventoryController()->getItemTwoHand().data = value;
	}
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
        if (!entity.hasComponent<NPCComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<NPCComponent>().isAlive;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_NPC_GetAlive", ex.what(), e); }
    return false;
}

static void AS_NPC_SetAlive(entityid e, bool v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NPCComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<NPCComponent>().isAlive = v;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_NPC_SetAlive", ex.what(), e); }
}

static float AS_NPC_GetVisionRange(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NPCComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<NPCComponent>().visionRange;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_NPC_GetVisionRange", ex.what(), e); }
    return 0.0f;
}

static void AS_NPC_SetVisionRange(entityid e, float v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NPCComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<NPCComponent>().visionRange = v;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_NPC_SetVisionRange", ex.what(), e); }
}

static float AS_NPC_GetChaseRange(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NPCComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<NPCComponent>().chaseRange;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_NPC_GetChaseRange", ex.what(), e); }
    return 0.0f;
}

static void AS_NPC_SetChaseRange(entityid e, float v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NPCComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<NPCComponent>().chaseRange = v;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_NPC_SetChaseRange", ex.what(), e); }
}

static float AS_NPC_GetAttackRange(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NPCComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<NPCComponent>().attackRange;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_NPC_GetAttackRange", ex.what(), e); }
    return 0.0f;
}

static void AS_NPC_SetAttackRange(entityid e, float v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NPCComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<NPCComponent>().attackRange = v;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_NPC_SetAttackRange", ex.what(), e); }
}

static float AS_NPC_GetAttackDelay(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NPCComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<NPCComponent>().attackDelay;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_NPC_GetAttackDelay", ex.what(), e); }
    return 0.0f;
}

static void AS_NPC_SetAttackDelay(entityid e, float v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NPCComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<NPCComponent>().attackDelay = v;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_NPC_SetAttackDelay", ex.what(), e); }
}

static std::string AS_NPC_GetName(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NPCComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<NPCComponent>().name;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_NPC_GetName", ex.what(), e); }
    return std::string();
}

static void AS_NPC_SetName(entityid e, std::string v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NPCComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<NPCComponent>().name = v;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_NPC_SetName", ex.what(), e); }
}

static int AS_NPC_GetState(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NPCComponent>()) throw ex_ent_invalid_comp;
        return static_cast<int>(entity.getComponent<NPCComponent>().state);
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_NPC_GetState", ex.what(), e); }
    return 0;
}

static void AS_NPC_SetState(entityid e, int v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NPCComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<NPCComponent>().state = static_cast<NPC_AI_STATE>(v);
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_NPC_SetState", ex.what(), e); }
}

static int AS_NPC_GetDisposition(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NPCComponent>()) throw ex_ent_invalid_comp;
        return static_cast<int>(entity.getComponent<NPCComponent>().disposition);
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_NPC_GetDisposition", ex.what(), e); }
    return 0;
}

static void AS_NPC_SetDisposition(entityid e, int v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NPCComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<NPCComponent>().disposition = static_cast<NPC_AI_DISPOSITION>(v);
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_NPC_SetDisposition", ex.what(), e); }
}

static std::string AS_NPC_GetStartWaypoint(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NPCComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<NPCComponent>().start_waypoint;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_NPC_GetStartWaypoint", ex.what(), e); }
    return std::string();
}

static void AS_NPC_SetStartWaypoint(entityid e, std::string v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NPCComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<NPCComponent>().start_waypoint = v;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_NPC_SetStartWaypoint", ex.what(), e); }
}

static std::string AS_NPC_GetCurrentWaypoint(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NPCComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<NPCComponent>().current_waypoint;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_NPC_GetCurrentWaypoint", ex.what(), e); }
    return std::string();
}

static void AS_NPC_SetCurrentWaypoint(entityid e, std::string v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NPCComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<NPCComponent>().current_waypoint = v;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_NPC_SetCurrentWaypoint", ex.what(), e); }
}

static bool AS_NPC_GetScriptControlled(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NPCComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<NPCComponent>().scriptControlled;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_NPC_GetScriptControlled", ex.what(), e); }
    return false;
}

static void AS_NPC_SetScriptControlled(entityid e, bool v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NPCComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<NPCComponent>().scriptControlled = v;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_NPC_SetScriptControlled", ex.what(), e); }
}

static irr::core::vector3df AS_NPC_GetMoveTarget(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NPCComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<NPCComponent>().move_target;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_NPC_GetMoveTarget", ex.what(), e); }
    return irr::core::vector3df();
}

static void AS_NPC_SetMoveTarget(entityid e, irr::core::vector3df v)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NPCComponent>()) throw ex_ent_invalid_comp;
        auto& npc = entity.getComponent<NPCComponent>();
        npc.move_target = v;
        npc.scriptWantsMovement = true;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_NPC_SetMoveTarget", ex.what(), e); }
}

static void AS_NPC_StopMovement(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NPCComponent>()) throw ex_ent_invalid_comp;
        entity.getComponent<NPCComponent>().scriptWantsMovement = false;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_NPC_StopMovement", ex.what(), e); }
}

static void AS_NPC_FindPath(entityid e, irr::core::vector3df dest)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NPCComponent>()) throw ex_ent_invalid_comp;
        if (!NavigationManager::Get() || !NavigationManager::Get()->isNavMeshBuilt()) return;
        auto& npc = entity.getComponent<NPCComponent>();
        auto& tf  = entity.getComponent<TransformComponent>();
        npc.navPath      = NavigationManager::Get()->findPath(tf.getPosition(), dest);
        npc.navPathIndex = 0;
        npc.navRepath    = static_cast<float>(Engine::Get()->getCurrentTime());
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_NPC_FindPath", ex.what(), e); }
}

static int AS_NPC_GetNavPathSize(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NPCComponent>()) throw ex_ent_invalid_comp;
        return static_cast<int>(entity.getComponent<NPCComponent>().navPath.size());
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_NPC_GetNavPathSize", ex.what(), e); }
    return 0;
}

static int AS_NPC_GetNavPathIndex(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NPCComponent>()) throw ex_ent_invalid_comp;
        return entity.getComponent<NPCComponent>().navPathIndex;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_NPC_GetNavPathIndex", ex.what(), e); }
    return 0;
}

static irr::core::vector3df AS_NPC_GetNavWaypoint(entityid e, int index)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NPCComponent>()) throw ex_ent_invalid_comp;
        auto& path = entity.getComponent<NPCComponent>().navPath;
        if (index >= 0 && index < static_cast<int>(path.size())) return path[index];
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_NPC_GetNavWaypoint", ex.what(), e); }
    return irr::core::vector3df();
}

static void AS_NPC_AdvanceNavPath(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NPCComponent>()) throw ex_ent_invalid_comp;
        auto& npc = entity.getComponent<NPCComponent>();
        if (npc.navPathIndex < static_cast<int>(npc.navPath.size()) - 1)
            ++npc.navPathIndex;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_NPC_AdvanceNavPath", ex.what(), e); }
}

static void AS_NPC_ClearNavPath(entityid e)
{
    try {
        auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);
        if (!entity.isValid()) throw ex_ent_invalid_name;
        if (!entity.hasComponent<NPCComponent>()) throw ex_ent_invalid_comp;
        auto& npc = entity.getComponent<NPCComponent>();
        npc.navPath.clear();
        npc.navPathIndex = 0;
    }
    catch (std::exception& ex) { spdlog::error("{} entity:{} Function: AS_NPC_ClearNavPath", ex.what(), e); }
}

// NPC state enum constants (match NPC_AI_STATE order)
static const int NPC_STATE_INACTIVE = 0;
static const int NPC_STATE_IDLE     = 1;
static const int NPC_STATE_PATROL   = 2;
static const int NPC_STATE_ALERT    = 3;
static const int NPC_STATE_ATTACK   = 4;
static const int NPC_STATE_CHASE    = 5;
static const int NPC_STATE_FLEE     = 6;
static const int NPC_STATE_DEAD     = 7;

void ScriptBindings::RegisterGame(asIScriptEngine* engine)
{
	engine->SetDefaultNamespace("game");
	{
		engine->RegisterGlobalFunction("void damage(int entityId, int amount)", asFUNCTION(AS_DamageEntity), asCALL_CDECL);
		engine->RegisterGlobalFunction("void heal(int entityId, int amount)", asFUNCTION(AS_HealEntity), asCALL_CDECL);

		engine->RegisterGlobalFunction("void activateLogicEvent(int entityId, bool active)", asFUNCTION(AS_ActivateLogicComponent), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool isLogicEventActive(int entityId)", asFUNCTION(AS_IsLogicComponentActivated), asCALL_CDECL);

		engine->RegisterGlobalFunction("void interact(int entityId)", asFUNCTION(as_interact), asCALL_CDECL);
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

		engine->RegisterGlobalFunction("void give(int itemId)", asFUNCTION(player_additemtoinv), asCALL_CDECL);
		engine->RegisterGlobalFunction("void give(string itemName)", asFUNCTION(player_additemtoinvs), asCALL_CDECL);

		engine->RegisterGlobalFunction("void give(int itemId, int entityId)", asFUNCTION(player_additemtoinvEnt), asCALL_CDECL);
		engine->RegisterGlobalFunction("void give(string itemName, int entityId)", asFUNCTION(player_additemtoinvsEnt), asCALL_CDECL);

		engine->RegisterGlobalFunction("void drop(int itemId)", asFUNCTION(player_dropiteminv), asCALL_CDECL);
		engine->RegisterGlobalFunction("void remove(int itemId)", asFUNCTION(player_removeitemtoinv), asCALL_CDECL);

		engine->RegisterGlobalFunction("bool has(int itemId)", asFUNCTION(player_hasitem), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool has(string itemName)", asFUNCTION(player_hasitemstr), asCALL_CDECL);

		engine->RegisterGlobalFunction("void removeActivatedItem()", asFUNCTION(player_removeCurrentActivatedItem), asCALL_CDECL);

		engine->RegisterGlobalFunction("string getEquippedItemData(string key)", asFUNCTION(player_getItemDataEquipped2H), asCALL_CDECL);
		engine->RegisterGlobalFunction("void setEquippedItemData(string key, string value)", asFUNCTION(player_setItemDataEquipped2H), asCALL_CDECL);
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
		engine->RegisterGlobalFunction("float visionRange(int entityId)",               asFUNCTION(AS_NPC_GetVisionRange),     asCALL_CDECL);
		engine->RegisterGlobalFunction("void visionRange(int entityId, float range)",   asFUNCTION(AS_NPC_SetVisionRange),     asCALL_CDECL);
		engine->RegisterGlobalFunction("float chaseRange(int entityId)",                asFUNCTION(AS_NPC_GetChaseRange),      asCALL_CDECL);
		engine->RegisterGlobalFunction("void chaseRange(int entityId, float range)",    asFUNCTION(AS_NPC_SetChaseRange),      asCALL_CDECL);
		engine->RegisterGlobalFunction("float attackRange(int entityId)",               asFUNCTION(AS_NPC_GetAttackRange),     asCALL_CDECL);
		engine->RegisterGlobalFunction("void attackRange(int entityId, float range)",   asFUNCTION(AS_NPC_SetAttackRange),     asCALL_CDECL);
		engine->RegisterGlobalFunction("float attackDelay(int entityId)",               asFUNCTION(AS_NPC_GetAttackDelay),     asCALL_CDECL);
		engine->RegisterGlobalFunction("void attackDelay(int entityId, float ms)",      asFUNCTION(AS_NPC_SetAttackDelay),     asCALL_CDECL);
		engine->RegisterGlobalFunction("string name(int entityId)",                     asFUNCTION(AS_NPC_GetName),            asCALL_CDECL);
		engine->RegisterGlobalFunction("void name(int entityId, string name)",          asFUNCTION(AS_NPC_SetName),            asCALL_CDECL);
		engine->RegisterGlobalFunction("int state(int entityId)",                       asFUNCTION(AS_NPC_GetState),           asCALL_CDECL);
		engine->RegisterGlobalFunction("void state(int entityId, int state)",           asFUNCTION(AS_NPC_SetState),           asCALL_CDECL);
		engine->RegisterGlobalFunction("int disposition(int entityId)",                 asFUNCTION(AS_NPC_GetDisposition),     asCALL_CDECL);
		engine->RegisterGlobalFunction("void disposition(int entityId, int disposition)",asFUNCTION(AS_NPC_SetDisposition),    asCALL_CDECL);
		engine->RegisterGlobalFunction("string startWaypoint(int entityId)",            asFUNCTION(AS_NPC_GetStartWaypoint),   asCALL_CDECL);
		engine->RegisterGlobalFunction("void startWaypoint(int entityId, string name)", asFUNCTION(AS_NPC_SetStartWaypoint),   asCALL_CDECL);
		engine->RegisterGlobalFunction("string currentWaypoint(int entityId)",          asFUNCTION(AS_NPC_GetCurrentWaypoint), asCALL_CDECL);
		engine->RegisterGlobalFunction("void currentWaypoint(int entityId, string name)",asFUNCTION(AS_NPC_SetCurrentWaypoint),asCALL_CDECL);

		engine->RegisterGlobalFunction("bool scriptControlled(int entityId)",                    asFUNCTION(AS_NPC_GetScriptControlled), asCALL_CDECL);
		engine->RegisterGlobalFunction("void scriptControlled(int entityId, bool controlled)",   asFUNCTION(AS_NPC_SetScriptControlled), asCALL_CDECL);
		engine->RegisterGlobalFunction("vector3d moveTarget(int entityId)",                      asFUNCTION(AS_NPC_GetMoveTarget),       asCALL_CDECL);
		engine->RegisterGlobalFunction("void moveTarget(int entityId, vector3d pos)",            asFUNCTION(AS_NPC_SetMoveTarget),       asCALL_CDECL);
		engine->RegisterGlobalFunction("void stopMovement(int entityId)",                        asFUNCTION(AS_NPC_StopMovement),        asCALL_CDECL);
		engine->RegisterGlobalFunction("void findPath(int entityId, vector3d destination)",      asFUNCTION(AS_NPC_FindPath),            asCALL_CDECL);
		engine->RegisterGlobalFunction("int navPathSize(int entityId)",                          asFUNCTION(AS_NPC_GetNavPathSize),      asCALL_CDECL);
		engine->RegisterGlobalFunction("int navPathIndex(int entityId)",                         asFUNCTION(AS_NPC_GetNavPathIndex),     asCALL_CDECL);
		engine->RegisterGlobalFunction("vector3d navWaypoint(int entityId, int index)",          asFUNCTION(AS_NPC_GetNavWaypoint),      asCALL_CDECL);
		engine->RegisterGlobalFunction("void advanceNavPath(int entityId)",                      asFUNCTION(AS_NPC_AdvanceNavPath),      asCALL_CDECL);
		engine->RegisterGlobalFunction("void clearNavPath(int entityId)",                        asFUNCTION(AS_NPC_ClearNavPath),        asCALL_CDECL);

		engine->RegisterGlobalProperty("const int STATE_INACTIVE", const_cast<int*>(&NPC_STATE_INACTIVE));
		engine->RegisterGlobalProperty("const int STATE_IDLE",     const_cast<int*>(&NPC_STATE_IDLE));
		engine->RegisterGlobalProperty("const int STATE_PATROL",   const_cast<int*>(&NPC_STATE_PATROL));
		engine->RegisterGlobalProperty("const int STATE_ALERT",    const_cast<int*>(&NPC_STATE_ALERT));
		engine->RegisterGlobalProperty("const int STATE_ATTACK",   const_cast<int*>(&NPC_STATE_ATTACK));
		engine->RegisterGlobalProperty("const int STATE_CHASE",    const_cast<int*>(&NPC_STATE_CHASE));
		engine->RegisterGlobalProperty("const int STATE_FLEE",     const_cast<int*>(&NPC_STATE_FLEE));
		engine->RegisterGlobalProperty("const int STATE_DEAD",     const_cast<int*>(&NPC_STATE_DEAD));
	}

	engine->SetDefaultNamespace("");
}
