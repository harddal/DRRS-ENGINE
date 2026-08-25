#pragma once

#include "../WeaponData.h"
#include "WeaponEffects.h"

#include <anax/anax.hpp>

#include "Engine/Engine.h"
#include "Engine/Renderer/RenderManager.h"

class Weapon_HeavyRifle : public PlayerWeapon
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
	float m_recoil = 0.01f;
	float m_lastFireTime = 0.0f;
	bool m_isEquipping = false;
	bool m_isUnequipping = false;
	bool m_isPlayingFireAnim = false;
	bool m_isReloadingAnim = false;

	// Box magazine. The asset carries two reload clips and the count is what
	// picks between them: "reload_empty" (10-93) runs the bolt as well as the
	// magazine, "reload" (94-152) is the shorter tactical swap that leaves a
	// round chambered.
	static const int m_magSize = 10;
	int  m_rounds = m_magSize;
	bool m_reloadWasEmpty = false; // which clip the running reload chose

	// Both reload clips run quicker than authored. Restored to 1x when the
	// reload ends, on every exit path.
	static constexpr float m_reloadSpeed = 1.3f;

	// Reload cue frames, as offsets from whichever clip is running: the two share
	// identical internal timing and simply sit 84 frames apart, so one set of
	// offsets covers both. Measured off the mag and bolt motion in the .glb.
	static const int m_reloadEmptyStart = 10;
	static const int m_reloadStart      = 94;
	static const int m_magDetachOffset  = 15; // magazine starts to drop
	static const int m_magSwapOffset    = 27; // magazine at full extension, furthest off screen
	static const int m_magSeatOffset    = 39; // fresh magazine is home
	static const int m_boltPullOffset   = 55; // charging handle starts back — empty reload only

	// The clip runs the swap straight through, reaching a sharp single-frame apex
	// at +27 before coming straight back. Holding there gives the beat where the
	// old magazine is gone and the new one is not up yet room to read.
	static constexpr float m_reloadPauseMs = 750.0f;

	bool  m_reloadPaused         = false; // this reload has already taken its hold
	float m_reloadPauseRemaining = 0.0f;  // ms left of it

	// Seconds from the start of each .wav to the transient that should land on
	// the visual event. SECONDS, not frames: the reload runs at m_reloadSpeed, so
	// the frame count these span differs from 1x. Convert with soundLeadFrames().
	static constexpr float m_removeMagLeadSec = 0.166f; // onset — the release click
	static constexpr float m_insertMagLeadSec = 0.400f; // peak  — the magazine seating
	static constexpr float m_cockLeadSec      = 0.042f; // onset — the handle starting back

	// The draw racks the bolt too — same 3-frame pull, 6-frame hold, 3-frame
	// release as the empty reload, just at an absolute frame of its own since the
	// equip clip shares its timing with nothing.
	static const int m_equipBoltPullFrame = 181;

	bool m_removeMagPlayed = false;
	bool m_insertMagPlayed = false;
	bool m_cockPlayed      = false;
	bool m_equipCockPlayed = false;

	void updateReloadSounds(float frame);
	void playCockSound();

	// Seconds until the clip reaches targetFrame, counting the swap pause if it
	// falls in between. Sound cues compare against this rather than a frame
	// count, so the hold cannot desync them.
	float secondsUntilFrame(int targetFrame, float currentFrame) const;

	// The bolt carries the ejection port, so brass is spawned from it. There is
	// no BRASS bone on this model and no animated case to hand off from — unlike
	// the shotgun's slug — so the casing is synthesised entirely here.
	irr::scene::IBoneSceneNode* m_bolt = nullptr;

	// Ejection port in the bolt's own local space: the centroid of the 50 verts on
	// its -X face. NOT the +X face, which juts 2.45 units further out and is the
	// charging handle. The viewmodel carries a 180 degree yaw, so model -X is the
	// player's right, which is where brass belongs. Model units, so unaffected by
	// the viewmodel scale.
	const irr::core::vector3df m_ejectPortOffset =
		irr::core::vector3df(-1.41f, 0.20f, -0.55f);

	// The magazine round, used only as a size reference so the ejected casing
	// matches the calibre the model is drawn holding.
	MeshPart m_caseSizeRef;

	// Shared muzzle flash / tracer / shell / impact VFX. Also owns the muzzle
	// attachment — fire() reads muzzleWorldPosition() rather than caching a node.
	WeaponEffects m_effects;

	irr::video::ITexture *m_crosshair;

	void ejectSpentCase();

public:
	WeaponEffects* debugEffects() override { return &m_effects; }
};
