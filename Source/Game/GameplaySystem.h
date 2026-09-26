#pragma once

#include <unordered_set>

#include <SColor.h>
#include <vector>

namespace irr { namespace scene { class ISceneNode; } }

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

	// 'ctx' tells GoreManager where the hit landed and which way it was going.
	// It is optional so the ~25 existing call sites keep compiling, but any
	// caller that knows should pass it — a default context sprays from the
	// entity's bounding-box centre, which reads as a canned effect.
	HIT_RESULT damageEntity(entityid id, unsigned int damage, DAMAGE_TYPE type = DAMAGE_TYPE::DEFAULT,
	                        const DamageContext& ctx = DamageContext());
	void healEntity(entityid id, unsigned int heal);
	void setInvulnerable(entityid id, bool set = true);
	void setBuddha(entityid id, bool set = true);

	std::vector<std::pair<irr::core::vector3df, irr::core::vector3df>> getWaterZones() { return m_waterZones; }

	// Same list without the copy. WaterBallistics reads this per projectile per
	// frame, which is not a place to be allocating a vector.
	const std::vector<std::pair<irr::core::vector3df, irr::core::vector3df>>& waterZones() const { return m_waterZones; }

	// Is this scene node the renderable of a water entity? Rebuilt every game
	// tick alongside m_waterZones, so it is only populated in game mode.
	//
	// Node pointers rather than entity ids because a raycast hands back a node,
	// and node->getID() is not a reliable way back to an entity (IDs default to
	// 0, which is itself a valid entity id).
	bool isWaterNode(const irr::scene::ISceneNode* node) const;

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
	std::vector<const irr::scene::ISceneNode*> m_waterNodes;

	void propagateLogicSignal(anax::Entity& entity, std::unordered_set<entityid>& visited);

	// Register the effects this system owns with ParticleManager, re-registering
	// them whenever they have gone away. Retried from update() rather than done
	// once in init() for TWO reasons, either of which is fatal on its own:
	//
	//  1. init() runs from the WorldManager CONSTRUCTOR, and Engine declares
	//     m_worldManager BEFORE m_particleManager -- so ParticleManager::Get()
	//     is still null there and the precache is silently skipped.
	//  2. Engine::clearScene() calls ParticleManager::clear(), which erases the
	//     whole effect table, and it runs on every editor<->game transition.
	//     init() is only ever reached once.
	//
	// 'spark' survived both because every weapon precaches it again in its own
	// precache(); 'water_splash' has no second owner, so it never loaded at all
	// and every bullet into a pool logged "spawn: unknown effect 'water_splash'".
	// Same shape, and same reasoning, as GoreManager::ensureEffects().
	void ensureEffects();

	// Latched when a .psys genuinely fails to load, so a missing file is not
	// re-read (and re-logged) once per frame forever.
	bool m_effectsUnavailable = false;

	// Resolve a CSV entity-name list (LogicComponent::receiver convention) and
	// propagate a logic signal into every match.  Shared visited set across
	// tokens, mirroring the per-frame LOGIC pass.
	void fireReceiverList(const std::string& csv);

	// Per-frame player-vs-brush volume tests: CONTENT_TRIGGER brushes fire
	// their receiver list on enter (edge-triggered, re-arms on exit);
	// CONTENT_LADDER overlap feeds PlayerController::setOnLadder;
	// CONTENT_HURT overlap drains health at the brush's authored rate.
	// (CONTENT_FOG is rendered per-view-ray from a volume array gathered in
	// RenderManager::updatePerFrameUBO, not resolved here.)
	void updateBrushVolumes();

};
