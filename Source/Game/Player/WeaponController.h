#pragma once

#include <memory>
#include <vector>

#include "WeaponData.h"

#include "Weapon/Weapon_None.h"

#include "Weapon/Weapon_Melee.h"
#include "Weapon/Weapon_Revolver.h"
#include "Weapon/Weapon_Shotgun.h"
#include "Weapon/Weapon_HeavyRifle.h"
#include "Weapon/Weapon_Crossbow.h"
#include "Weapon/Weapon_LMG.h"
#include "Weapon/Weapon_Sniper.h"
#include "Weapon/Weapon_Launcher.h"
#include "Weapon/Weapon_DualSMG.h"
#include "Weapon/Weapon_SkullStaff.h"
#include "Weapon/Weapon_Sawnoffs.h"
#include "Weapon/Weapon_Pitchfork.h"
#include "Weapon/Weapon_Rifle.h"
#include "Weapon/Weapon_SMG.h"


#include "Weapon/Weapon_Pistol.h"
#include "Weapon/Weapon_BoltDriver.h"
#include "Weapon/Weapon_Minigun.h"
#include "Weapon/Weapon_RocketLauncher.h"
#include "Weapon/Weapon_MiningLaser.h"
#include "Weapon/Weapon_BioRifle.h"
#include "Weapon/Weapon_GrenadeLauncher.h"
#include "Weapon/Weapon_FuelRodCannon.h"

class WeaponController
{
public:
	void init();
	void update();
	void destroy();

	// --- Reserve ammunition --------------------------------------------------
	// Returns the amount ACTUALLY accepted after clamping to the pool's cap, so
	// a pickup can tell "you took 30" from "you were already full" and decline to
	// consume itself in the second case.
	unsigned int addAmmo(AMMO_TYPE type, unsigned int amount);

	void setAmmo(AMMO_TYPE type, unsigned int amount);

	unsigned int reserveAmmo(AMMO_TYPE type) const;

	// Removes up to 'amount' from the pool and returns what was actually there to
	// take. This is the reload path — PlayerWeapon::drawFromReserve() wraps it.
	unsigned int takeAmmo(AMMO_TYPE type, unsigned int amount);

	void giveAllAmmo();

	void switchNextWeapon();
	void switchPreviousWeapon();
	void switchWeapon(PLAYER_WEAPON type);
	void unequipWeapon();

	// --- Selection (Half-Life 2 buckets) -------------------------------------
	// Pressing a bucket key does NOT switch immediately. It opens the selection
	// bar on the first owned weapon in that bucket; pressing the same bucket again
	// advances within it, wrapping — so a player can tap 4 twice to reach the
	// second automatic without ever equipping the first one on the way past.
	//
	// The bar has NO timeout. It stays open until the player commits with attack,
	// dismisses it with alt-fire, or leaves via 0 / Q. Nothing is equipped while
	// it is open, so there is no cost to leaving it up and deciding.
	void selectCategory(int category);
	void commitSelection();
	void cancelSelection();
	bool isSelectionOpen() const { return m_selectionCategory > 0; }

	// The mouse wheel (and the bracket keys) walk the selection in the order it is
	// DRAWN — down a bucket, then right to the next one — rather than in enum
	// order, which interleaves the buckets and would make the highlight jump
	// around the bar. direction +1 is down/right, -1 is up/left.
	void selectionStep(int direction);

	// Q, as in HL2's lastinv. The single most-used switch in a fight, and the one
	// thing buckets alone do not give you.
	void switchToLastWeapon();

	// --- Ownership -----------------------------------------------------------
	// Every weapon is CONSTRUCTED and init()ed at startup regardless — they need
	// their scene nodes and effect pools built, and tearing that down per pickup
	// would be a lot of churn for nothing. Ownership gates only what the player
	// can switch TO, which is the part that actually matters.
	//
	// The player starts owning WEAP_NONE alone. Everything else arrives through
	// giveWeapon(), which is what a pickup calls.
	//
	// Returns true when the weapon was NOT already owned — so a caller can tell
	// "you found a new gun" from "you topped up one you had" and react differently.
	bool giveWeapon(PLAYER_WEAPON type, bool autoEquip = true);
	bool hasWeapon(PLAYER_WEAPON type) const;

	// For a debug command or a test map that wants the old behaviour back
	void giveAllWeapons();

	// --- Save sidecar --------------------------------------------------------
	// The weapon in hand, as a PLAYER_WEAPON rather than as a slot index — the
	// two match today only because registration order happens to follow the enum.
	PLAYER_WEAPON currentWeaponType() const;

	// --- HUD -----------------------------------------------------------------
	// Rounds in the gun, and rounds left in the pool behind it. Both return -1
	// when there is nothing meaningful to draw — no weapon in hand, or a weapon
	// with no ammunition — so the HUD has a single test for "skip the readout".
	int currentDisplayAmmo() const;
	int currentReserveAmmo() const;

	// Restores one weapon's magazine. Addressed by enum for the same reason, and
	// silently ignores a weapon this build never registered, so an old save that
	// mentions a removed gun loads rather than throwing.
	void loadWeaponMagState(PLAYER_WEAPON type, const WeaponMagState& state);

	std::vector<unsigned int> m_player_ammo;
	std::vector<std::unique_ptr<PlayerWeapon>> m_player_weapon;

private:
	bool m_firstUpdate;
	bool m_showViewmodelDebug = false;

	void setViewmodelDebug(bool open);
	void drawViewmodelDebugUI();

	// Highest valid index into m_player_weapon (registered weapons only).
	// NOTE this is a BOUND, not a history — the weapon previously held is
	// m_previousWeapon. The two names are close; they mean unrelated things.
	unsigned int lastWeaponSlot() const;

	// --- Selection state -----------------------------------------------------
	// -1 / closed when no bucket is open. m_selectionSlot is a CANDIDATE: nothing
	// is equipped until it commits, so scrolling through a bucket costs no draw
	// animations.
	int m_selectionCategory = -1;
	int m_selectionSlot     = -1;

	// While the bar is open the weapons must not see the mouse, or the click that
	// commits would also fire, and the alt-fire that dismisses would zoom the
	// sniper or lob a bounce grenade. InputManager::blockMouseInput() hides the
	// BUTTONS from anything reading them without ignore_process_flag; the wheel is
	// unaffected because getMouseWheelDelta() does not consult the flag, and mouse
	// LOOK is unaffected because only button queries are gated.
	//
	// The block is held past the commit until the button comes back up, so the
	// click that chose a weapon cannot also be read as the trigger pull after the
	// bar has closed.
	bool m_swallowMouseUntilRelease = false;

	// The weapon held before the current one, for the Q toggle. Updated where a
	// switch actually COMPLETES, not where one is requested — a switch that is
	// superseded mid-unequip never became the weapon you were holding.
	unsigned int m_previousWeapon = 0;

	// First owned weapon in a bucket at or after 'fromSlot', wrapping within the
	// bucket. Returns -1 when the player owns nothing in it.
	int firstOwnedInCategory(int category, int fromSlot) const;

	// Owned weapons in DRAW order: bucket 1 top-to-bottom, then bucket 2, and so
	// on. This is the order the wheel follows, and it is built from the same two
	// rules the bar is drawn with, so the two can never disagree.
	int buildSelectionOrder(int* out, int max) const;

	void drawWeaponSelection();

	// One flag per registered slot. Sized in init() alongside m_player_weapon, so
	// it cannot drift out of step with what was actually registered.
	std::vector<bool> m_owned;

	// The next owned slot walking in 'step' direction from 'from', wrapping.
	// Returns 'from' when nothing else is owned, which is what makes the cycling
	// safe with a single weapon: the caller sees no change and does nothing.
	unsigned int nextOwnedSlot(unsigned int from, int step) const;

	unsigned int m_current_weapon;
	int m_pendingWeapon = -1; // >= 0 while waiting for unequip anim to finish

	Weapon_None m_weapon_none;
	Weapon_Melee m_weapon_melee;
	Weapon_Pistol m_weapon_pistol;
	Weapon_Revolver m_weapon_revolver;
	Weapon_BoltDriver m_weapon_boltdriver;
	Weapon_HeavyRifle m_weapon_heavyrifle;
	Weapon_Crossbow m_weapon_crossbow;
	Weapon_LMG m_weapon_lmg;
	Weapon_Sniper m_weapon_sniper;
	Weapon_Launcher m_weapon_launcher;
	Weapon_DualSMG m_weapon_dualsmg;
	Weapon_SkullStaff m_weapon_skullstaff;
	Weapon_Sawnoffs m_weapon_sawnoffs;
	Weapon_Pitchfork m_weapon_pitchfork;
	Weapon_Rifle m_weapon_rifle;
	Weapon_SMG m_weapon_smg;
	Weapon_Minigun m_weapon_minigun;
	Weapon_RocketLauncher m_weapon_rocketlauncher;
	Weapon_Shotgun m_weapon_shotgun;
	Weapon_MiningLaser m_weapon_mininglaser;
	Weapon_BioRifle m_weapon_biorifle;
	Weapon_GrenadeLauncher m_weapon_grenadelauncher;
	Weapon_FuelRodCannon m_weapon_fuelrodcannon;

};