#pragma once

#include <unordered_set>

#include "anax/anax.hpp"

#include "Game/Components/DamageReceiverComponent.h"
#include "Engine/World/Components/DescriptorComponent.h"

class GameplaySystem : public anax::System<anax::Requires<DescriptorComponent>>
{
public:
    void onEntityAdded(anax::Entity& entity) override;

    void onEntityRemoved(anax::Entity& entity) override;

	void init();
    void update();
	void destroy();

	HIT_RESULT damageEntity(entityid id, unsigned int damage, DAMAGE_TYPE type = DAMAGE_TYPE::DEFAULT);
	void healEntity(entityid id, unsigned int heal);
	void setInvulnerable(entityid id, bool set = true);
	void setBuddha(entityid id, bool set = true);

	std::vector<std::pair<irr::core::vector3df, irr::core::vector3df>> getWaterZones() { return m_waterZones; }

	void interact(entityid receiver);

	void setShowEntityLinks(bool show) { m_showEntityLinks = show; }
	bool isShowEntityLinks() const { return m_showEntityLinks; }

	// F8 debug view: arrows for every LogicComponent/TriggerZoneComponent link.
	// Called by WorldManager every logic tick (unlike update(), which is
	// game-mode only) so the view also works in the pure editor.
	void drawEntityLinkDebug();
private:
	irr::f32 m_current, m_last, m_time;

	// On by default: the editor shows link arrows until F8 turns them off.
	// Outside the editor the isDebugSpriteVisible() gate still keeps them hidden.
	bool m_showEntityLinks = true;

	std::vector<std::pair<irr::core::vector3df, irr::core::vector3df>> m_waterZones;

	void propagateLogicSignal(anax::Entity& entity, std::unordered_set<entityid>& visited);

};
