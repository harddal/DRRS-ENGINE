#pragma once

#include "../WeaponData.h"
#include "WeaponEffects.h"

// ---------------------------------------------------------------------------
// Weapon_Sniper — bolt-action scoped rifle.
//
// Two things set it apart from everything else in the pack:
//
//   The BOLT. Every shot has to be followed by working the action, which
//   sniper_animated.glb authors as its own clip. Fire hands off to the cycle
//   the way the shotgun's fire hands off to the pump, and the gun cannot fire
//   again until it has finished. That is the whole cost of the damage.
//
//   The SCOPE. Right mouse raises the sight: the view narrows, the viewmodel
//   comes up to the eye, and at full magnification the model is swapped for a
//   full-screen scope overlay. This is the first aim-down-sights in the
//   project, so the sustained FOV offset it needs was added to CameraFX rather
//   than hidden in here — see Weapon_Sniper::updateScope().
// ---------------------------------------------------------------------------
class Weapon_Sniper : public PlayerWeapon
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
	bool isUnequipping() const override { return m_state == State::Unequipping; }
	void idle();
	void move();
	void fire();
	void reload();

private:
	// Explicit state machine rather than the LMG's flags: fire ALWAYS chains
	// into the bolt cycle, and that chain is the weapon's defining rule. A set
	// of independent booleans cannot express "this clip owes another clip".
	enum class State
	{
		Idle,
		Equipping,
		Unequipping,
		Firing,   // "fire"   -> Cycling, always
		Cycling,  // "cycle"  -> Idle
		Reloading // "reload" -> Idle
	};

	State m_state = State::Idle;

	// --- Cadence -------------------------------------------------------------
	// Bolt action, so there is no fire-rate constant: the cycle clip IS the rate
	// limiter, and the gun is unfireable until it returns to Idle. One shot per
	// press on top of that, so holding the button does not auto-fire the instant
	// the bolt closes.
	bool m_firedThisPress = false;

	int m_damage = 110; // a torso hit kills; the cost is ~1.9 s before the next one

	// Tight even unscoped, but not perfect — the scope is still worth raising.
	// Scoped, this is multiplied by m_scopedSpreadScale.
	float m_spread = 0.0035f;
	static constexpr float m_scopedSpreadScale = 0.12f;

	// --- Magazine ------------------------------------------------------------
	static const int m_magSize = 5;
	int m_rounds = m_magSize;

	// --- Clip speeds ---------------------------------------------------------
	// The authored bolt cycle is a deliberate 1.63 s and the mag swap 1.47 s.
	// The cycle is left close to authored on purpose — it is the weapon's whole
	// drawback and hurrying it would erase the trade.
	static constexpr float m_cycleSpeed  = 1.15f; // -> 1.42 s
	static constexpr float m_reloadSpeed = 1.35f; // -> 1.09 s

	// --- The chambered round / spent case ------------------------------------
	// ONE 'bullet' mesh plays both parts, exactly like the shotgun's 'slug': it
	// rides the bolt back as the extracted case, is flicked clear, and then
	// reappears seated as the fresh round. It has to be hidden and shown across
	// that flick or it visibly teleports back into the breech.
	MeshPart m_round;
	bool     m_caseHandedOff = false; // a physics casing has replaced it this cycle

	// Driven off the round's MEASURED displacement, not off a frame number.
	// The LMG shipped with frame-derived triggers for its belt and they did not
	// fire at the frames the .glb analysis predicted; asking the joint where the
	// part actually is cannot be wrong about that, and needs no constant kept in
	// step with the asset. The case is thrown the moment the round breaks away
	// from its seated position, and the fresh one appears when it returns.
	irr::core::vector3df m_roundRest;
	bool                 m_roundRestValid = false;

	// Model units. The extraction lifts the round 2.0 and the flick throws it 8.0,
	// against a seated position that is otherwise dead still.
	static constexpr float m_roundLooseEpsilon = 2.6f;

	// --- Scope ---------------------------------------------------------------
	// 0 = hip, 1 = fully scoped. Everything the sight does is a function of this
	// one number, so the transition can never half-apply: the FOV, the spread,
	// the sway damping and the overlay all read the same blend.
	float m_scopeBlend = 0.0f;
	bool  m_scopeWanted = false;

	static constexpr float m_scopeBlendSpeed = 9.0f;   // per second
	static constexpr float m_scopeFovZoom    = -42.0f; // degrees off the base FOV

	// Past this the model is swapped for the sight. Not 1.0: the swap wants to
	// happen while the view is still closing, so the sight is already there by
	// the time the player is looking through it.
	static constexpr float m_scopeOverlayAt = 0.75f;

	// --- The sight itself ----------------------------------------------------
	// Drawn by the "scope" post-process pass (scope.frag), not by a masking
	// texture. A texture mask had to be stretched to fill the screen, which
	// turned its circular aperture into an ellipse and could only ever put a flat
	// colour outside it; the pass gets a true circle at any aspect and shows the
	// world out of focus instead.
	//
	// Aperture is in the shader's units: 1.0 is half the SHORT screen axis. It
	// opens from Open to Full across the last quarter of the blend, so raising
	// the sight reads as the view closing in.
	static constexpr float m_scopeApertureOpen = 0.85f;
	static constexpr float m_scopeApertureFull = 0.42f;
	static constexpr float m_scopeSoftness     = 0.02f;  // feather at the edge
	static constexpr float m_scopeBlurRadius   = 9.0f;   // tap spread, pixels
	static constexpr float m_scopeVignette     = 0.35f;  // surround brightness

	float m_scopeAperture = m_scopeApertureFull; // current, for the reticle to size off

	// Mirrors whether the post-process pass is switched on, so the toggle is
	// only pushed to the renderer when it actually changes rather than every frame.
	bool  m_scopePassOn = false;

	// Sway is cut right down when scoped — a scope magnifies hand movement just
	// as much as it magnifies the target, and at 1x sway the reticle is unusable.
	static constexpr float m_scopedSwayScale = 0.15f;

	// Set while the overlay is up and the viewmodel node is deliberately hidden.
	// update() must not treat that as "holstered" and bail, so the early-out
	// tests this too.
	bool m_modelHiddenByScope = false;

	// Raising a scope onto a target you cannot see is worse than useless, so the
	// sight drops for anything that moves the gun: firing, cycling, reloading and
	// both weapon transitions all force it down and let the player raise it again.
	bool scopeAllowed() const { return m_state == State::Idle; }

	// --- Audio ---------------------------------------------------------------
	// Seconds from the start of each .wav to the transient that should land on
	// the visual event. SECONDS, not frames: these clips run at their own speeds,
	// so the frame count a fixed lead-in spans differs from 1x. Convert with
	// soundLeadFrames() at the point of use, never with a constant.
	static constexpr float m_boltLeadSec      = 0.047f; // cock_rifle.wav peak
	static constexpr float m_removeMagLeadSec = 0.276f; // remove_mag.wav peak
	static constexpr float m_insertMagLeadSec = 0.400f; // insert_mag.wav peak

	// Frames within the clips, measured off the .glb (see init())
	static const int m_boltLiftFrame  = 24; // handle rotates up
	static const int m_boltHomeFrame  = 47; // handle rotates back down
	static const int m_magOutFrame    = 72; // magazine breaks free
	static const int m_magInFrame     = 89; // fresh magazine seated — ammo lands HERE

	bool m_boltLiftPlayed = false;
	bool m_boltHomePlayed = false;
	bool m_magOutPlayed   = false;
	bool m_magInPlayed    = false;
	bool m_ammoCredited   = false;

	// Set when fire is pressed while the action is busy. Buffered rather than
	// dropped, so a shot asked for a few frames before the bolt closes still
	// goes off — but never applied mid-cycle, because the gun is not loaded yet.
	bool m_fireAfterCycle = false;

	WeaponEffects m_effects;

	irr::video::ITexture* m_crosshair = nullptr;

	void enterState(State next);
	void updateRound();
	void ejectSpentCase();
	void updateScope(float dt);
	void disableScopePass();
	void updateCycleSounds(float frame);
	void updateReloadSounds(float frame);
	void drawScopeOverlay();

public:
	WeaponEffects* debugEffects() override { return &m_effects; }
};
