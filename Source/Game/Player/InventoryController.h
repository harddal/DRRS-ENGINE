#pragma once

#include "PlayerData.h"
#include "InventoryData.h"

class InventoryController
{
public:
    InventoryController() = default;

    void init();
    void update(PlayerData &data);
    void destroy();

    void draw(irr::core::vector2di mouse_position);

    bool isInventoryDisplaying() { return m_displayInventory; }

    // Returns false if no space to pickup item
    bool pickupItem(itemid item, entityid ent = ITEM_NULL_ID);
    bool pickupItem(std::string item, entityid ent = ITEM_NULL_ID);
    bool pickupItem(const Item& item);

    // Returns false if given invalid/empty slot
    bool dropItem(unsigned int slot);
    bool removeItem(unsigned int slot);

    // Returns -1 if no item found
    int getFirstItemSlotOfType(itemid id);
    int getFirstItemSlotOfType(std::string name);

    // Returns an item with ITEM_ID_NULL if no item found
    Item& getFirstItemCopyOfType(itemid id);
    Item& getFirstItemCopyOfType(std::string name);

    // Equiping
    bool equipLeftHand (const Item& item);
    bool equipRightHand(const Item& item);
    bool equipTwoHand  (const Item& item);

    bool unequipLeftHand ();
    bool unequipRightHand();
    bool unequipTwoHand  ();

    bool isEquipedLeftHand()  { return !m_leftHandEquipedItem.empty(); }
    bool isEquipedRightHand() { return !m_rightHandEquipedItem.empty(); }
    bool isEquipedTwoHand()   { return !m_twoHandEquipedItem.empty(); }

	bool isEquippedSpell(unsigned int slot)
	{
		switch (slot)
		{
		case 1:
			return m_spellSlot1Equip.id < ITEM_NULL_ID ? true : false;
		case 2:
			return m_spellSlot2Equip.id < ITEM_NULL_ID ? true : false;
		case 3:
			return m_spellSlot3Equip.id < ITEM_NULL_ID ? true : false;
		case 4:
			return m_spellSlot4Equip.id < ITEM_NULL_ID ? true : false;
		default:
			return false;
			break;
		}
	}

	Item getEquippedSpell(unsigned int slot)
	{
		switch (slot)
		{
		case 1:
			return m_spellSlot1Equip;
		case 2:
			return m_spellSlot2Equip;
		case 3:
			return m_spellSlot3Equip;
		case 4:
			return m_spellSlot4Equip;
		default:
			return Item();
			break;
		}
	}

    Item& getItemRightHand() { return m_leftHandEquipSlot; }
    Item& getItemLeftHand () { return m_rightHandEquipSlot; }
    Item& getItemTwoHand  () { return m_twoHandEquipSlot; }

    Item getItemRightHandCopy() { return m_leftHandEquipSlot; }
    Item getItemLeftHandCopy () { return m_rightHandEquipSlot; }
    Item getItemTwoHandCopy  () { return m_twoHandEquipSlot; }

    // Returns -1 if weapon doesn't use ammo, or no weapon is equipped
    int getCurrentWeaponAmmoCount();

    void removeCurrentActivatedItem();

private:
    bool 
        m_displayInventory, 
        m_draggingItem;

    bool
        m_display_ui_info,
        m_display_ui_stats,
        m_display_ui_misc,
        m_display_ui_container;

    unsigned int m_draggedItemPrevSlot, m_currentActivatedItemSlot;

    irr::video::ITexture 
        *m_slotImage, 
        *m_slotImage2H, 
        *m_slotImage1H;

    Item
        m_currentActivatedItem,
        m_currentClickedItem,
        m_currentDraggedItem,
        m_leftHandEquipSlot,
        m_rightHandEquipSlot,
        m_twoHandEquipSlot,
		m_spellSlot1Equip,
		m_spellSlot2Equip,
		m_spellSlot3Equip,
		m_spellSlot4Equip;

    std::string
        m_leftHandEquipedItem,
        m_rightHandEquipedItem,
        m_twoHandEquipedItem,
		m_spellSlot1EquippedItem,
		m_spellSlot2EquippedItem, 
		m_spellSlot3EquippedItem, 
		m_spellSlot4EquippedItem;
};

extern bool g_PlayerInventoryIsDisplaying, g_LockPlayerForInput;
