#pragma once

#include "WeaponData.h"

#define _cct_impulse_scale 50
#define _cct_transform_scale 0.5f

#define _player_interact_distance 1.5f

struct PlayerData
{
    bool isWeaponEquipped = false;

	// Refreshed from the player's DamageReceiverComponent at the top of every
	// PlayerController::update(). A COPY — write to the component, never here.
	//
	// The ammoDisplayValue that sat alongside this was the same pattern without
	// the refresh, and the HUD now asks the weapon directly instead.
    int currentHealth = 0;

	PlayerData& operator=(PlayerData data)
	{
		std::swap(isWeaponEquipped, data.isWeaponEquipped);
		std::swap(currentHealth, data.currentHealth);

		return *this;
	}
};

extern PlayerData g_PlayerData;