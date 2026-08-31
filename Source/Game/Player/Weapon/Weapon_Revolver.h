#pragma once

#include <vector>

#include "../WeaponData.h"
#include "WeaponEffects.h"

class Weapon_Revolver : public PlayerWeapon
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

	int displayAmmo() const override { return m_cylinder; }
	// Magazine contents for the save sidecar. See WeaponMagState — slot 0 is this
	// weapon's only counter.
	void saveMagState(WeaponMagState& out) const override { out.slots[0] = m_cylinder; }
	void loadMagState(const WeaponMagState& in) override  { if (in.slots[0] >= 0) m_cylinder = (in.slots[0] < m_cylinderSize ? in.slots[0] : m_cylinderSize); }

private:
	// Single-action: one shot per click, hard cadence limit between shots
	bool m_firedThisPress = false;
	bool m_isFireAnim = false;    // fire animation in progress (does NOT block firing)
	bool m_isReloading = false;   // reload animation in progress (blocks firing)
	bool m_isEquipping = false;
	bool m_isUnequipping = false;
	float m_lastFireTime = 0.0f;
	const float m_minFireInterval = 420.0f; // ms — hand-cocked single action, much slower than the pistol
	int m_damage = 34;                      // three shots to drop what the pistol needs ten for

	// Six-round cylinder. Firing an empty one only clicks — reloading is always a
	// deliberate act by the player, never automatic.
	int m_cylinder = 0;
	static const int m_cylinderSize = 6;

	// Reload playback state. The clip loads all six rounds back to back, so a
	// partial reload plays a computed sub-range (open + eject + N loading swoops)
	// and then cuts to the closing section. See reload().
	int m_reloadRounds   = 0; // rounds this reload set out to load, 1..6
	int m_reloadCredited = 0; // of those, how many have reached their chamber
	int m_reloadPhase    = 0; // 0 = not reloading, 1 = open/eject/load, 2 = close

	// Set when the player presses fire mid-reload: loading stops, the cylinder
	// close plays out, and the shot goes off the instant it latches. Buffered
	// rather than immediate so the gun is never fired with the crane hanging open.
	bool m_fireAfterReload = false;

	// Frame-triggered audio. Tracked separately from m_reloadCredited because the
	// sounds fire EARLY (see the lead constants) while the ammo lands on the seat
	// frame itself, so the two counters legitimately diverge mid-reload.
	int  m_insertSoundsPlayed = 0;
	bool m_closeSoundPlayed   = false;
	bool m_equipSpinPlayed    = false;

	// Aim spread (tighter than the pistol — long barrel, deliberate shots)
	float m_spread = 0.004f;

	// --- Cylinder rounds -----------------------------------------------------
	// The reload clip reuses ONE set of six round meshes for both the ejection
	// and the loading: the cases ride the ejector out and then slide straight
	// back into the chambers, and every round is seated before the first loading
	// swoop plays. The animation only reads correctly if they are hidden and
	// shown per frame, so resolveCylinderRounds() maps each round to its mesh
	// buffers and updateReloadRounds() drives them off the clip's frame number.
	//
	// Rounds are consumed in animation order: with N fired, rounds [0, N) are the
	// spent ones that eject and get reloaded, and rounds [N, 6) are still live —
	// those stay visible throughout, riding the ejector out and back, which is
	// exactly what an ejector rod does to unfired rounds.
	static const int m_roundCount = 6;

	// Frames within the reload clip, measured off the .glb (see init()).
	static const int m_reloadStartFrame = 13;  // cylinder starts swinging open
	static const int m_ejectFrame       = 34;  // cases at full extension — hide here
	static const int m_firstInsertFrame = 51;  // round 1 starts its loading swoop
	static const int m_firstSeatFrame   = 63;  // round 1 is home in its chamber
	static const int m_insertStride     = 26;  // frames between successive rounds
	static const int m_reloadCloseStart = 193; // cylinder starts swinging shut
	static const int m_cylinderLatchFrame = 217; // crane reaches 0 deg — shut
	static const int m_reloadCloseEnd   = 227; // last frame of the reload
	static const int m_equipSpinFrame   = 252; // cylinder spin flourish begins

	// Seconds from the start of each .wav to its transient, measured off the
	// files, so a cue can be triggered early enough for that transient to land ON
	// the visual event. insert_shell.wav in particular carries 0.2s of
	// near-silence before the click — played on the seat frame it lands a third
	// of a second late.
	//
	// Held in SECONDS, not frames: the reload runs at m_reloadSpeed, so the
	// number of animation frames these span is not the same as at 1x. Convert
	// with soundLeadFrames() at the point of use, never with a constant.
	static constexpr float m_insertSoundLeadSec = 0.306f; // insert_shell.wav peak
	static constexpr float m_closeSoundLeadSec  = 0.088f; // revolver_close_cylinder.wav peak
	static constexpr float m_spinSoundLeadSec   = 0.059f; // revolver_cylinder_spin.wav onset

	// The authored reload is a deliberate 7.2s at 1x, which reads as a liability
	// in a firefight; this trims it without re-authoring the clip.
	static constexpr float m_reloadSpeed = 1.3f;

	// First frame of the ejector push. Spent-case world positions are sampled from
	// here to m_ejectFrame so the casing handed off at the hide can inherit the
	// animation's real velocity instead of a guessed direction.
	static const int m_ejectTrackFrame = 29;

	struct CylinderRound
	{
		std::vector<irr::u32>       buffers;                          // indices into the viewmodel's mesh buffers
		irr::video::E_MATERIAL_TYPE material = irr::video::EMT_SOLID;  // real material, restored on show
		bool                        visible  = true;

		// Bone node for the same glTF node the buffers hang off. It carries no
		// geometry, but its absolute transform tracks the animated round exactly —
		// which is what the physics casing is spawned from.
		irr::scene::IBoneSceneNode* bone = nullptr;

		irr::core::vector3df lastWorldPos;
		irr::core::vector3df velocity;          // world units/sec, measured over one frame
		float                peakSpeed = 0.0f;  // fastest the ejector threw it, for scale-free scatter
		bool                 hasLastWorldPos = false;
	};
	CylinderRound m_rounds[m_roundCount];

	void resolveCylinderRounds();
	void setRoundVisible(int index, bool visible);
	void setAllRoundsVisible(bool visible);
	void updateReloadRounds(float frame);
	void creditSeatedRounds(float frame);
	void updateReloadSounds(float frame);
	void interruptReloadToFire();
	void trackSpentCases(float frame);
	void ejectSpentCase(int index);
	irr::core::vector3df spentCaseScale(int index) const;
	void endReload();

	// Shared muzzle flash / tracer / impact VFX (no per-shot shell — a revolver
	// dumps its brass during the reload, not on firing). Also owns the muzzle
	// attachment: fire() takes its ray origin from m_effects.muzzleWorldPosition()
	// rather than caching a node here, so the shot and the flash cannot disagree.
	WeaponEffects m_effects;

public:
	WeaponEffects* debugEffects() override { return &m_effects; }

private:

	irr::video::ITexture *m_crosshair;
};
