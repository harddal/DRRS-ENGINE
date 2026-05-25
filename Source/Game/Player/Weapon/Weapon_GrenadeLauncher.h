#pragma once

#include "../WeaponData.h"

class Weapon_GrenadeLauncher : public PlayerWeapon
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
};
