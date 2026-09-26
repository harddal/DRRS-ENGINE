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

	// Breath readout for the HUD, written by PlayerController after its
	// PlayerBreath tick. Display copies only, same rule as currentHealth.
	float breathFraction = 1.0f;   // 0..1 air remaining
	float breathHudAlpha = 0.0f;   // 0..1 faded visibility of the bar
	bool  isDrowning     = false;  // out of air and still airless

	// Every field must be swapped here by hand: one left out silently keeps
	// its default through any assignment.
	PlayerData& operator=(PlayerData data)
	{
		std::swap(isWeaponEquipped, data.isWeaponEquipped);
		std::swap(currentHealth, data.currentHealth);
		std::swap(breathFraction, data.breathFraction);
		std::swap(breathHudAlpha, data.breathHudAlpha);
		std::swap(isDrowning, data.isDrowning);

		return *this;
	}
};

extern PlayerData g_PlayerData;