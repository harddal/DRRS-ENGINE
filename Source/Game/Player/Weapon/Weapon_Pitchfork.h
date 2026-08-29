#pragma once

#include <vector>

#include "../WeaponData.h"

// ---------------------------------------------------------------------------
// Weapon_Pitchfork — a long two-handed melee weapon, built on Weapon_Melee's
// shape: an attack table, a swing state machine, and a strike that lands on the
// animation's CONTACT FRAME rather than on the frame the button went down.
//
// What it adds over the knife is reach and an arc. A pitchfork is a polearm:
// the thrusts reach much further than a blade, and the wide sweep is swung
// across a front rather than at a point — so an attack carries its own reach
// and its own fan of rays, and the sweep can catch more than one target.
//
// NOTE: pitchfork_animated.glb has NO draw or holster clip. Every rest-to-rest
// range in the take is either an attack or the idle. The weapon therefore
// appears and disappears instantly, softened only by a view kick on the draw —
// see equip(). If a draw is ever authored, add it to the clip table and restore
// the startUnequip()/isUnequipping() overrides the knife has.
// ---------------------------------------------------------------------------
class Weapon_Pitchfork : public PlayerWeapon
{
public:
	void precache();
	void init();
	void update();
	void persist() { return; }
	void destroy();
	void equip();
	void unequip();
	void idle();
	void move();
	void fire();
	void reload();

	// One row per attack, so a clip's name, the frame it connects on, its damage,
	// its reach and its spread stay together instead of being repeated as
	// literals at the call site. That duplication is exactly what let the knife's
	// contact frames silently keep their old .b3d numbers through a mesh change.
	//
	// contactFrame is an ABSOLUTE frame in the shared 0-115 timeline, matching
	// m_mesh.animationList. Measured from the TINE TIPS' travel in the .glb, not
	// from the root: on a weapon this long the root barely moves compared to the
	// business end, and it is the tips that decide when a hit reads as landing.
	//
	// PUBLIC only so the attack table can live as file-static constants in the
	// .cpp, where it belongs — a private nested type is not visible there. The
	// struct describes an attack and exposes nothing that needs protecting.
	struct MeleeAttack
	{
		const char*  anim;
		int          contactFrame;
		unsigned int damage;
		float        reach;      // world units from the camera
		int          rays;       // 1 for a thrust; a fan for the sweep
		float        arcDegrees; // total width of that fan
	};

private:
	void startSwing(const MeleeAttack& attack);
	void performStrike();

	bool m_isSwinging = false;
	bool m_damageDone = false;

	// The attack currently running. Held by value rather than by pointer so the
	// table can stay a set of file-static constants in the .cpp.
	MeleeAttack m_attack {};

	// Entities already damaged by the swing in progress. The sweep casts a fan of
	// rays across its arc, and without this a target standing in the middle of
	// that fan takes the hit once per ray.
	std::vector<entityid> m_hitThisSwing;
};
