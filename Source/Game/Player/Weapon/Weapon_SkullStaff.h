#pragma once

#include <vector>

#include "../WeaponData.h"
#include "WeaponEffects.h"

// ---------------------------------------------------------------------------
// Weapon_SkullStaff — a casting weapon, and the spell framework behind it.
//
// The point of this file is the FRAMEWORK. A spell is a row of data, not a
// subclass: SpellDesc holds everything a spell needs, s_spells[] in the .cpp is
// the table, and castSpell() switches on one behaviour enum. Adding a spell that
// throws something is a table entry and nothing else. Adding a spell that works
// in a genuinely new way — a beam, a heal, a summon — is a table entry plus one
// case in castSpell(), and the places to put it are marked.
//
// The asset supports this well: skullstaff_animated.glb ships TWO distinct cast
// animations, a quick jab and a long channelled hold, so a spell chooses its own
// clip and heavy spells can look heavy. Both are already wired.
//
// Right mouse cycles the equipped spell; the current one and the mana pool are
// drawn under the crosshair, since there is no HUD to put them in.
// ---------------------------------------------------------------------------

// How a spell delivers itself. Only SPELL_PROJECTILE is implemented — the other
// two are declared so the switch in castSpell() has somewhere obvious to grow,
// and so a table entry cannot silently do the wrong thing by defaulting.
enum SpellBehaviour
{
	SPELL_PROJECTILE, // a bolt that flies out and detonates      (implemented)
	SPELL_HITSCAN,    // an instant beam to the crosshair          (not yet)
	SPELL_SELF,       // a buff or heal; nothing leaves the staff  (not yet)
};

// Optional per-spell hooks.
//
// These are what make UTILITY spells work. The behaviour enum above answers
// "what shape is the delivery" — projectile, beam, nothing — and that question
// repeats across spells, so it belongs in data. A utility spell asks a different
// question: what does THIS ONE, specifically, do? Heals, shields, teleports and
// summons have nothing in common with each other, so folding them into the enum
// turns castSpell() into a dumping ground of one-off cases.
//
// POINTER-TO-MEMBER, not a free function pointer, because every interesting
// utility spell needs the staff's own state — mana, effects, the projectile
// list, the player entity. A free void(*)(Weapon_SkullStaff&, ...) would force
// either friend declarations or making internals public; a member pointer gets
// that access for nothing and is still a compile-time constant, so the table
// stays a static const array. Not std::function either: heavier than needed, and
// it can allocate.
class Weapon_SkullStaff;
struct SpellDesc;

// Extra behaviour, run AFTER the delivery in `behaviour`. That ordering is the
// point: a pure utility spell is SPELL_SELF (a delivery that does nothing) plus
// a hook, and a bolt with a rider effect is SPELL_PROJECTILE plus a hook. The
// two compose instead of competing.
using SpellHook = void (Weapon_SkullStaff::*)(const SpellDesc&);

// Optional gate. Null means always castable. This exists because utility spells
// are the ones that can be pointless to cast — a heal at full health, a shield
// already up — and spending the mana to find that out is a bad trade. The HUD
// reads the same predicate, so an uncastable spell greys out rather than failing
// silently.
using SpellPredicate = bool (Weapon_SkullStaff::*)(const SpellDesc&) const;

struct SpellDesc
{
	const char*    name;
	SpellBehaviour behaviour;

	int   manaCost;
	float cooldownMs;

	// Which authored cast animation this spell uses, and how fast to run it.
	// "cast" is the 0.67 s jab; "cast_channel" is the 2.5 s hold.
	const char* castClip;
	float       castSpeed;

	// Milliseconds from the start of the cast to the spell actually leaving the
	// skull. Timed rather than keyed to an animation frame ON PURPOSE: the LMG
	// shipped with frame-derived triggers that did not fire where the .glb
	// analysis said they would, and a wall-clock delay cannot drift from the
	// clip speed the way a frame number can.
	float releaseDelayMs;

	// --- Projectile behaviour (SPELL_PROJECTILE) ---------------------------
	float speed;        // units/sec
	float gravity;      // units/sec^2; 0 flies dead straight
	float lifetimeMs;

	float directDamage;
	float splashDamage; // 0 disables the splash entirely
	float splashRadius;

	// --- Presentation --------------------------------------------------------
	const char*         trailTexture;  // particle texture streamed behind the bolt
	irr::video::SColor  trailColorFrom;
	irr::video::SColor  trailColorTo;
	const char*         impactParticle; // ParticleManager effect on detonation
	irr::video::SColorf lightColor;     // bolt's own point light, and the flash
	float               lightRadius;
	const char*         castSound;

	// --- Hooks (see above) ---------------------------------------------------
	SpellHook      onCast  = nullptr; // extra behaviour; null = delivery only
	SpellPredicate canCast = nullptr; // gate; null = always castable
};

class Weapon_SkullStaff : public PlayerWeapon
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

	// Spell selection, exposed so a pickup or a console command can drive it
	// later rather than only the right mouse button.
	void selectSpell(int index);
	void cycleSpell();
	int  spellIndex() const { return m_spell; }

private:
	enum class State
	{
		Idle,
		Equipping,
		Unequipping,
		Casting, // whichever clip the spell named -> Idle
	};

	State m_state = State::Idle;

	// --- Spellbook -----------------------------------------------------------
	// The table is a MEMBER, defined in the .cpp, rather than a file-static
	// array. That is not decoration: its rows take pointers to the private hook
	// members below, and only a member definition is inside the class's access
	// scope. A namespace-scope table would force the hooks public.
	static const SpellDesc s_spells[];
	static const int       s_spellCount;

	int m_spell = 0;

	const SpellDesc& spell() const;

	// Set once the cast has actually let go of its spell, so a clip that runs on
	// past the release cannot fire a second one.
	bool  m_spellReleased = false;
	float m_castStartTime = 0.0f;

	// Per-spell cooldowns, so switching spells does not launder one spell's
	// cooldown into another's. Sized to the table in the .cpp.
	std::vector<float> m_nextReadyTime;

	// --- Mana ----------------------------------------------------------------
	// A pool rather than ammunition: it regenerates, so the staff is never truly
	// out, only spent. That is what lets a spell be expensive without the weapon
	// needing a reload animation it does not have.
	float m_mana = 100.0f;
	static constexpr float m_manaMax   = 100.0f;
	static constexpr float m_manaRegen = 14.0f; // per second

	bool m_cyclePressed = false; // right mouse edge, so one press is one cycle

	// --- Projectiles ---------------------------------------------------------
	// Spell bolts in flight. Kept here rather than in a shared system because
	// nothing else in the project owns one yet; if a second casting weapon ever
	// appears, this is the piece to lift out.
	std::vector<WeaponProjectile> m_projectiles;

	irr::video::E_MATERIAL_TYPE m_trailMaterialType =
		irr::video::E_MATERIAL_TYPE::EMT_TRANSPARENT_ADD_COLOR;

	// The spell each bolt was cast with, parallel to m_projectiles — a bolt has
	// to remember its own damage and impact effect, because the player can
	// switch spells while it is still in the air.
	std::vector<int> m_projectileSpell;

	WeaponEffects m_effects;

	irr::video::ITexture* m_crosshair = nullptr;

	// True when the equipped spell could actually be cast right now: off
	// cooldown, affordable, and passing its own canCast gate if it has one.
	// One definition, read by both the input path and the HUD, so what the
	// player is shown and what a click does cannot disagree.
	bool canCastCurrent() const;

	// --- Spell hooks ---------------------------------------------------------
	// One pair per utility spell. They are ordinary private members, which is
	// exactly why the table stores pointer-to-member: no friends, no public
	// internals, and the compiler still checks the signature.
	void spellMend(const SpellDesc& desc);
	bool mendWouldHelp(const SpellDesc& desc) const;

	void enterState(State next);
	void castSpell();
	void spawnSpellBolt(const SpellDesc& desc, int spellIndex);
	void updateProjectiles(float dt);
	void detonate(const SpellDesc& desc, const irr::core::vector3df& pos,
	              entityid directHitID, const irr::core::vector3df& surfaceNormal);
	void applySplashDamage(const SpellDesc& desc, const irr::core::vector3df& epicentre,
	                       entityid directHitEntityID);
	void drawSpellHud();

public:
	WeaponEffects* debugEffects() override { return &m_effects; }
};
