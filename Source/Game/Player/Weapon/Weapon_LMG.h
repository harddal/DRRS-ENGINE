#pragma once

#include "../WeaponData.h"
#include "WeaponEffects.h"

// ---------------------------------------------------------------------------
// Weapon_LMG — belt-fed light machine gun.
//
// The feature that defines it is the BELT: lmg_animated.glb carries ten
// separate round meshes (bullet0..bullet9) strung along the arc that runs from
// the ammo box up into the feed tray, and their visibility is driven off the
// live ammo count every frame. The belt is therefore the ammo counter — the HUD
// readout is commented out in HUDController, so a player judges what is left by
// looking at how much belt is still hanging out of the box.
// ---------------------------------------------------------------------------
class Weapon_LMG : public PlayerWeapon
{
public:
	void precache();
	void init();
	void update();
	void persist();
	void destroy();
	void equip();
	void unequip();
	void startUnequip() override;
	bool isUnequipping() const override { return m_isUnequipping; }
	void idle();
	void move();
	void fire();
	void reload();

private:
	// --- State ---------------------------------------------------------------
	// Flags rather than the shotgun's explicit enum: this gun only ever fires,
	// reloads, draws or holsters, and none of those chain into one another the
	// way the shotgun's fire -> pump -> reload -> rack does.
	bool m_isFireAnim    = false; // fire clip running; does NOT block firing
	bool m_isReloading   = false; // belt change; blocks everything
	bool m_isEquipping   = false;
	bool m_isUnequipping = false;

	// --- Cadence -------------------------------------------------------------
	// Full auto, so the fire button is level-triggered and there is no
	// one-shot-per-press latch like the revolver's. 100 ms is 600 rpm, between
	// a PKM and an M249.
	float m_lastFireTime = 0.0f;
	static constexpr float m_fireInterval = 100.0f; // ms between rounds

	// One dry click per press. The ONLY thing this weapon latches per press: an
	// open-bolt gun run dry does nothing at all when the trigger is squeezed
	// again, so clicking once per cadence tick would be both wrong and maddening.
	bool m_dryFiredThisPress = false;

	// The fire clip is 9 frames of travel (see init()). Played at 3x it spans
	// exactly m_fireInterval, so held fire reads as one continuous bolt cycle
	// rather than as a clip that restarts before or after it has finished.
	static constexpr float m_fireSpeed = 3.0f;

	int m_damage = 16; // 600 rpm x 16 = 160 dps sustained, before the bloom bites

	// --- Belt ----------------------------------------------------------------
	// Ten rounds on the arc, and one link is one ROUND — not a fraction of the
	// magazine. The arc stays full while the box still holds more than ten, and
	// only starts to thin once the belt showing IS the whole remaining supply.
	// See beltLinksForAmmo().
	//
	// THE ORDER MATTERS AND IT IS NOT THE OBVIOUS ONE. bullet9 sits at the feed
	// tray — it is the mesh the fire clip throws forward into the chamber — and
	// bullet0 is the tail, deepest in the box. A real belt slides THROUGH the
	// feed, so every arc position stays occupied until the tail of the belt
	// passes it, which means the arc empties from the BOX end upward and the
	// feed tray is the last position to go bare. Hence: hide bullet0 first,
	// bullet9 last.
	static const int m_beltLinks = 10;

	MeshPart m_belt[m_beltLinks];
	int      m_beltShown = -1; // links currently visible; -1 forces the first pass

	int m_ammo = 0;
	static const int m_ammoCapacity = 100;

	// --- Reload --------------------------------------------------------------
	// Frames within the reload clip, measured off the .glb (see init()). The
	// whole belt change is one 171-frame take, so every event below is an
	// absolute frame within that single range rather than a clip of its own.
	static const int m_latchOpenFrame  = 24;  // 'loader' has slid fully back
	static const int m_boxOffFrame     = 68;  // 'mag' breaks free of the receiver
	static const int m_boxOnFrame      = 92;  // fresh box seated — ammo lands HERE
	static const int m_coverCloseFrame = 128; // 'lid_2' slams shut over the belt
	static const int m_latchCloseFrame = 165; // 'loader' returns; the gun is charged

	// --- Keeping a PARTIAL belt from breaking during the swap ----------------
	//
	// The clip was authored for a full belt and keeps the strand strung out along
	// the arc while the box is carried away. A partial belt is the FAR end of
	// that strand, so the moment the box moves the visible rounds become a
	// fragment flying through the air with the links that joined them to the box
	// hidden. Nothing about the ammo count can describe that, so the belt has to
	// be taken off screen for as long as the box is off the gun.
	//
	// This is driven off the box's MEASURED DISPLACEMENT, not off frame numbers.
	// A first attempt hardcoded the two frames the box leaves and returns, read
	// off the .glb, and it did not work — so the frame the clip reports at
	// runtime is not the frame this analysis says it is. Asking the joint where
	// the box actually is cannot be wrong about that, needs no constant kept in
	// step with the asset, and covers the return trip as well as the throw.
	//
	// The payoff at the far end is exact: the box reaches its seat on the same
	// frame the ammo is credited, and the artist has the belt folded down INSIDE
	// the box for the whole return — so the fresh belt reappears within the box
	// mesh, where there is nothing to see, and then rides up the arc as authored.
	irr::scene::IBoneSceneNode* m_magBone = nullptr;
	irr::core::vector3df        m_magRest;          // joint-local, sampled at reload start
	bool                        m_magRestValid = false;
	bool                        m_boxOffSeat   = false;

	// Model units. The box travels ~47 units at the peak of the throw and sits
	// dead still otherwise, so anything clear of animation noise works; this
	// trips on the first frame the box actually moves.
	static constexpr float m_boxOffSeatEpsilon = 1.0f;

	// Seconds from the start of each .wav to its transient, measured off the
	// files, so a cue can be triggered early enough for that transient to land
	// ON the visual event. Held in SECONDS, not frames: the reload runs at
	// m_reloadSpeed, so the number of animation frames a fixed lead-in spans is
	// not the same as at 1x. Convert with soundLeadFrames() at the point of use,
	// never with a constant.
	static constexpr float m_cockLeadSec      = 0.047f; // cock_rifle.wav peak
	static constexpr float m_removeMagLeadSec = 0.276f; // remove_mag.wav peak
	static constexpr float m_insertMagLeadSec = 0.400f; // insert_mag.wav peak

	// The authored belt change is 5.7 s. Trimmed rather than re-authored, and
	// deliberately still the longest reload of any weapon here: being caught
	// mid-swap is what an LMG trades its sustained fire for.
	static constexpr float m_reloadSpeed = 1.6f; // -> 3.56 s

	// Ammo is credited once, at m_boxOnFrame, so a belt change abandoned by a
	// weapon switch before the box is seated yields nothing. Because belt
	// visibility is derived from the ammo count every frame, the fresh belt
	// appears in the same instant and the two can never disagree.
	bool m_ammoCredited = false;

	// One shot per cue. Five separate flags rather than a counter, because these
	// are five distinct events and not five repeats of one.
	bool m_latchOpenPlayed  = false;
	bool m_boxOffPlayed     = false;
	bool m_boxOnPlayed      = false;
	bool m_coverClosePlayed = false;
	bool m_latchClosePlayed = false;

	// How much of the belt change's screen-space swing to cancel. Below 1.0 on
	// purpose: hauling a fresh box up into a gun SHOULD move the weapon about,
	// it just should not wander off the edge of the screen doing it.
	static constexpr float m_reloadStabilize = 0.45f;

	// --- Spread bloom --------------------------------------------------------
	// The reason to burst-fire rather than hold the trigger down. Bloom climbs
	// per round and decays in real time, so the cone is a function of how long
	// the trigger has been held — not of how much ammo is left, which would
	// punish a full belt and reward an empty one.
	float m_bloom = 0.0f;                           // 0..1
	static constexpr float m_spreadMin    = 0.006f; // first round out of a cold gun
	static constexpr float m_spreadMax    = 0.032f; // fully bloomed
	static constexpr float m_bloomPerShot = 0.055f; // ~18 rounds to full
	static constexpr float m_bloomDecay   = 1.1f;   // per second, once the trigger is off

	// --- Brass ---------------------------------------------------------------
	// Spawned from the 'ejector' joint, which the fire clip drives through the
	// extraction stroke, rather than from a port offset off the weapon node — so
	// the case leaves from where the animation says it does.
	irr::scene::IBoneSceneNode* m_ejector = nullptr;

	// Case owed by the shot just fired, thrown once the clip reaches the top of
	// the extraction stroke. Deferred rather than spawned inside fire() because
	// fire() restarts the clip at frame 0, where the ejector is still home.
	bool m_caseOwed = false;
	static const int m_caseEjectFrame = 2; // 'ejector' at full rearward travel

	// Port on the -X face of the receiver. The viewmodel carries a 180 degree
	// yaw, so model -X reads as the player's right, which is where brass belongs;
	// the +X face juts 1.2 units further out and is the charging handle. Held in
	// ejector-local model units, so a viewmodel-scale change does not move it.
	const irr::core::vector3df m_ejectPortOffset =
		irr::core::vector3df(-1.2f, 0.0f, 0.0f);

	WeaponEffects m_effects;

	irr::video::ITexture* m_crosshair = nullptr;

	// Belt visibility as a pure function of the ammo count — one place for the
	// mapping, so the belt cannot drift out of sync with what the gun will fire.
	int  beltLinksForAmmo(int ammo) const;
	int  beltLinksThisFrame() const;
	void updateBelt();
	void updateBoxOffSeat();
	void resolveBelt();

	void updateReloadSounds(float frame);
	void ejectSpentCase();
	void endReload();

public:
	WeaponEffects* debugEffects() override { return &m_effects; }
};
