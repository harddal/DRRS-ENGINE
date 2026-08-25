#pragma once

#include "../WeaponData.h"

class Weapon_Melee : public PlayerWeapon
{
public:
	void precache();
	void init();
	void update();
	void persist() { return; };
	void destroy();
	void equip();
	void unequip();
	void idle();
	void move();
	void fire();
	void reload();

	// Animated holster: WeaponController calls startUnequip() and then holds the
	// switch open until isUnequipping() goes false, so the clip gets to finish
	// before the next weapon's equip() runs. Without these overrides the base
	// class hides the node instantly and both clips are never seen.
	void startUnequip() override;
	bool isUnequipping() const override { return m_isUnequipping; }

private:
	// Swing state machine — the strike lands on the animation's contact frame,
	// not the frame the button was pressed (anticipation → payoff). 'contactFrame'
	// is absolute (same numbering as the animation list), not relative to the clip.
	void startSwing(const std::string& animation, int contactFrame, unsigned int damage);
	void performStrike();

	bool m_isSwinging    = false;
	bool m_damageDone    = false;
	bool m_isEquipping   = false;  // playing "equip", hand off to idle when it ends
	bool m_isUnequipping = false;  // playing "unequip", hide the node when it ends
	int  m_contactFrame  = 0;
	unsigned int m_attackDamage = 25; // Overwritten by struct MeleeAttack at top of *.cpp for multi attacks
};
