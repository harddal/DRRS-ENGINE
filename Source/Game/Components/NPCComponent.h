#pragma once

#include <vector>

#include "anax/Component.hpp"

#include "cereal/cereal.hpp"
#include "cereal/types/string.hpp"
#include "Engine/Types.h"
#include "irrlicht.h"

enum class NPC_AI_STATE
{
    INACTIVE,
    IDLE,
    PATROL,
    ALERT,
    ATTACK,
    CHASE,
    FLEE,
    DEAD
};

enum class NPC_AI_DISPOSITION
{
	NEUTRAL,
	ENEMY,
	FRIENDLY
};

struct NPCComponent : anax::Component
{
    float visionRange, chaseRange, attackRange;

    std::string name;

    NPC_AI_STATE state;
	NPC_AI_DISPOSITION disposition;

    std::string start_waypoint, current_waypoint;

    float last_attack_time = 0.0f, attackDelay;

    // Runtime speed multiplier — scripts use npc::moveSpeedScale() to apply slow/freeze effects
    float moveSpeedScale = 1.0f;

    // Nav path cache — not serialized
    std::vector<irr::core::vector3df> navPath;
    int   navPathIndex  = 0;
    float navRepath     = 0.0f;   // Simulation time of last path request (ms)

    // Script movement contract — not serialized
    // scriptControlled kept for backwards compatibility; NPCSystem has no C++ state machine to suppress
    bool scriptControlled    = false;
    irr::core::vector3df move_target;
    bool scriptWantsMovement = false;

	template <class Archive>
	void serialize(Archive& archive) { archive(CEREAL_NVP(name), CEREAL_NVP(visionRange), CEREAL_NVP(chaseRange), CEREAL_NVP(attackRange), CEREAL_NVP(attackDelay), CEREAL_NVP(state), CEREAL_NVP(disposition), CEREAL_NVP(start_waypoint), CEREAL_NVP(current_waypoint)); }

    NPCComponent() : visionRange(5.0f), chaseRange(5.0f), attackRange(1.25f), state(NPC_AI_STATE::IDLE), moveSpeedScale(1.0f), attackDelay(2000.0f) {}
};
