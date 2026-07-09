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

private:
	// Swing state machine — the strike lands on the animation's contact frame,
	// not the frame the button was pressed (anticipation → payoff)
	void startSwing(int startFrame, int endFrame, int contactFrame, unsigned int damage);
	void performStrike();

	bool m_isSwinging   = false;
	bool m_damageDone   = false;
	int  m_contactFrame = 0;
	unsigned int m_attackDamage = 10;
};
