#include "GameplaySystem.h"

#include <cmath>
#include <cstdint>
#include <unordered_map>

#include <spdlog/spdlog.h>

#include "Engine/Engine.h"
#include "Engine/Resource/FilePaths.h"
#include "Engine/Resource/MaterialBuilder.h"
#include "Engine/World/WorldManager.h"
#include "Engine/Brush/BrushGeometry.h"
#include "Engine/Brush/BrushManager.h"
#include "Engine/Renderer/RenderManager.h"
#include "Engine/Renderer/DecalManager.h"
#include "Engine/Renderer/Particle/ParticleManager.h"
#include "Engine/World/Components/MeshComponent.h"
#include "Game/Components.h"
#include "Game/LogicLinks.h"
#include "Player/PlayerController.h"

#include "Utility/Utility.h"

#include "Game/Item/ItemDatabase.h"
#include "Game/Gore/GoreManager.h"
#include "Game/Gore/FractureManager.h"

#include "DialogManager.h"

namespace
{
	// --- Non-flesh impact FX -------------------------------------------------
	// A hit that neither bleeds (GoreManager) nor shatters the prop
	// (FractureManager) still needs to read on screen. One row per IMPACT_SURFACE
	// value; FLESH and NONE are handled before the table is ever indexed.
	//
	// STUB: only spark.psys and bullet_hole.png exist today, so every row points
	// at them. Swap the names here once dedicated wood/stone/glass/dirt effects
	// are authored — nothing else changes.
	struct SurfaceImpactFx
	{
		const char* particle;   // ParticleManager effect key, precached in init()
		const char* decal;      // decal texture, nullptr to skip
		float       decalSize;
	};

	const SurfaceImpactFx k_surfaceImpactFx[IMPACT_SURFACE_COUNT] = {
		/* IMPACT_AUTO  */ { "spark", "content/texture/fx/bullet_hole.png", 0.18f },
		/* IMPACT_FLESH */ { nullptr, nullptr,                              0.0f  },
		/* IMPACT_WOOD  */ { "spark", "content/texture/fx/bullet_hole.png", 0.20f },
		/* IMPACT_METAL */ { "spark", "content/texture/fx/bullet_hole.png", 0.16f },
		/* IMPACT_STONE */ { "spark", "content/texture/fx/bullet_hole.png", 0.22f },
		/* IMPACT_GLASS */ { "spark", "content/texture/fx/bullet_hole.png", 0.18f },
		/* IMPACT_DIRT  */ { "spark", "content/texture/fx/bullet_hole.png", 0.24f },
		/* IMPACT_NONE  */ { nullptr, nullptr,                              0.0f  },
	};

	// Map the texture-name classifier's output onto an impact surface.
	IMPACT_SURFACE impactSurfaceFromManaged(E_MANAGED_MATERIAL m)
	{
		switch (m)
		{
		case MAT_WOOD:                return IMPACT_WOOD;
		case MAT_METAL:               return IMPACT_METAL;
		case MAT_STONE:               return IMPACT_STONE;
		case MAT_GLASS:               return IMPACT_GLASS;
		case MAT_EARTH:
		case MAT_GRAVEL:
		case MAT_CARPET:
		case MAT_WATER:               return IMPACT_DIRT;
		case MAT_INVALID:
		default:                      return IMPACT_AUTO;   // generic debris row
		}
	}

	// Resolve the effective surface for an entity. 'stored' is the component's
	// serialized override (IMPACT_SURFACE as int).
	IMPACT_SURFACE resolveImpactSurface(const anax::Entity& entity, int stored)
	{
		if (stored > IMPACT_AUTO && stored < IMPACT_SURFACE_COUNT)
			return static_cast<IMPACT_SURFACE>(stored);

		// AUTO: the entity's behaviour gets first say (the retired NPCComponent
		// no longer marks creatures — MeleeZombieBehavior::bloodType() and its
		// kin do). A behaviour returning IMPACT_AUTO has no opinion.
		if (entity.hasComponent<BehaviorComponent>())
		{
			const auto& bc = entity.getComponent<BehaviorComponent>();
			if (bc.behavior)
			{
				const IMPACT_SURFACE bt = bc.behavior->bloodType();
				if (bt > IMPACT_AUTO && bt < IMPACT_SURFACE_COUNT)
					return bt;
			}
		}

		// Otherwise classify the first diffuse texture through MaterialBuilder,
		// the same path Weapon_Crossbow uses to decide stick-vs-shatter.
		if (entity.hasComponent<MeshComponent>())
		{
			const auto& mc = entity.getComponent<MeshComponent>();
			if (!mc.textures.empty() && !mc.textures[0].empty())
			{
				const E_MANAGED_MATERIAL m = Engine::Get()->getMaterialBuilder()
					.getMaterialFromTexture(Utility::FilenameFromPath(mc.textures[0]));

				return impactSurfaceFromManaged(m);
			}
		}

		return IMPACT_AUTO; // generic debris
	}

	// Spawn the particle + decal for a resolved non-flesh surface. 'lethal'
	// bumps the scale so a killing blow reads heavier than a graze.
	void playSurfaceImpact(IMPACT_SURFACE surf, const DamageContext& ctx, bool lethal)
	{
		if (surf == IMPACT_FLESH || surf == IMPACT_NONE || !ctx.valid)
			return;

		const SurfaceImpactFx& fx = k_surfaceImpactFx[surf];

		irr::core::vector3df n = ctx.normal;
		if (n.getLengthSQ() < 0.0001f)
			n.set(0.0f, 1.0f, 0.0f);
		n.normalize();

		const float scale = lethal ? 1.6f : 1.0f;

		if (fx.particle)
		{
			if (auto* pm = ParticleManager::Get())
			{
				const uint32_t handle = pm->spawn(
					fx.particle, SPK::Vector3D(ctx.point.X, ctx.point.Y, ctx.point.Z));

				if (handle)
				{
					pm->setEmitterDirection(handle, n);
					if (scale != 1.0f)
						pm->setScale(handle, scale);
				}
			}
		}

		if (fx.decal)
		{
			if (auto* rm = RenderManager::Get())
				if (rm->decals())
					rm->decals()->spawn(ctx.point, n, fx.decalSize * scale, fx.decal);
		}
	}
} // namespace

void GameplaySystem::onEntityAdded(anax::Entity& entity)
{
	if (entity.hasComponent<AutoKillComponent>()) 
	{
		entity.getComponent<AutoKillComponent>().spawn_encoded = Engine::Get()->getCurrentTime();
	}

	if (entity.hasComponent<NPCComponent>())
	{
		auto& npc = entity.getComponent<NPCComponent>();

		if (npc.current_waypoint.empty()) 
		{
			npc.current_waypoint = npc.start_waypoint;
		}
	}
}

void GameplaySystem::onEntityRemoved(anax::Entity& entity)
{
    
}

void GameplaySystem::init()
{
	GoreManager::create();
	GoreManager::Get()->precache();

	FractureManager::create();

	// Non-flesh impact FX (crates, barrels, props). STUB: all surfaces reuse the
	// weapon spark effect until dedicated debris effects are authored.
	if (auto* pm = ParticleManager::Get())
		pm->precache("spark", _asset_psys("spark"));
}

void GameplaySystem::propagateLogicSignal(anax::Entity& entity, std::unordered_set<entityid>& visited)
{
    if (!entity.isValid()) return;

    auto id = entity.getComponent<DescriptorComponent>().id;
    if (visited.count(id)) return;
    visited.insert(id);

    if (entity.hasComponent<LogicComponent>() && entity.hasComponent<ScriptComponent>())
    {
        auto& logic  = entity.getComponent<LogicComponent>();
        auto& script = entity.getComponent<ScriptComponent>();

        if (script.hasOnLogicEventActivate)
        {
            logic.isActivated = false;
            ScriptManager::Get()->execute(script, script.onLogicEventActivate, id);
        }
        else
        {
            logic.isActivated = true;
        }

        if (logic.isActivated)
        {
            for (auto& token : LogicLinks::splitNameList(logic.receiver))
            {
                for (auto* next : WorldManager::Get()->managerSystem()->getEntitiesByName(token))
                    propagateLogicSignal(*next, visited);
            }
            logic.isActivated = false;
        }
    }
    else if (entity.hasComponent<BehaviorComponent>())
    {
        auto& bc = entity.getComponent<BehaviorComponent>();
        if (bc.behavior)
            bc.behavior->onLogicSignal(entity);
    }
    else if (entity.hasComponent<LightComponent>())
    {
        entity.getComponent<RenderComponent>().isVisible =
            !entity.getComponent<RenderComponent>().isVisible;
    }
}

void GameplaySystem::fireReceiverList(const std::string& csv)
{
    std::unordered_set<entityid> visited;
    for (auto& token : LogicLinks::splitNameList(csv))
    {
        for (auto* target : WorldManager::Get()->managerSystem()->getEntitiesByName(token))
            propagateLogicSignal(*target, visited);
    }
}

void GameplaySystem::updateBrushVolumes()
{
    if (!BrushManager::Get())
        return;

    auto* ms = WorldManager::Get()->managerSystem();
    if (!ms->doesEntityExist("player"))
        return;
    auto& player = ms->getEntityByName("player");
    if (!player.isValid() || !player.hasComponent<TransformComponent>()
        || !player.hasComponent<DescriptorComponent>())
        return;

    const irr::core::vector3df p = player.getComponent<TransformComponent>().position;

    // Body test points: feet and chest.  Two points make ladder topping-out
    // smooth (you stay climbable until your feet clear the volume) and let a
    // shallow hurt volume — lava, a slime pool — catch the player by the feet.
    const irr::core::vector3df bodyFeet  = p + irr::core::vector3df(0.0f, 0.3f, 0.0f);
    const irr::core::vector3df bodyChest = p + irr::core::vector3df(0.0f, 1.3f, 0.0f);
    bool onLadder = false;

    auto touchesBody = [&](const Brush& b)
    {
        return (b.bounds.isPointInside(bodyFeet)  && BrushGeometry::containsPoint(b, bodyFeet)) ||
               (b.bounds.isPointInside(bodyChest) && BrushGeometry::containsPoint(b, bodyChest));
    };

    // Damage is authored per second but dealt in whole points, so each hurt
    // brush carries its own fractional remainder across frames.  Overlapping
    // hurt volumes stack (Quake-style) rather than taking the max.
    const auto& playerDesc = player.getComponent<DescriptorComponent>();
    const float dtSeconds  = Engine::Get()->getDeltaTime() / 1000.0f;

    for (auto& brush : BrushManager::Get()->getAllBrushesMutable())
    {
        if (!brush.geometryValid || brush.isMoverBrush())
            continue;

        if ((brush.contentFlags & CONTENT_LADDER) && !onLadder)
            onLadder = touchesBody(brush);

        if ((brush.contentFlags & CONTENT_HURT) && brush.hurtDamagePerSecond > 0.0f)
        {
            if (playerDesc.isAlive && touchesBody(brush))
            {
                brush.hurtAccum += brush.hurtDamagePerSecond * dtSeconds;
                if (brush.hurtAccum >= 1.0f)
                {
                    const unsigned int points = static_cast<unsigned int>(brush.hurtAccum);
                    brush.hurtAccum -= static_cast<float>(points);
                    damageEntity(playerDesc.id, points);
                }
            }
            else
            {
                // Drop the remainder on exit so brief re-entries can't bank
                // damage and land it all in one tick
                brush.hurtAccum = 0.0f;
            }
        }

        if (!(brush.contentFlags & CONTENT_TRIGGER))
            continue;
        if ((brush.contentFlags & CONTENT_TRIGGER_ONCE) && brush.triggerFired)
            continue;

        const bool inside = brush.bounds.isPointInside(p) && BrushGeometry::containsPoint(brush, p);
        if (inside && !brush.triggerInside)
        {
            brush.triggerFired = true;
            if (!brush.receiver.empty())
                fireReceiverList(brush.receiver);
        }
        brush.triggerInside = inside;
    }

    if (g_PlayerController)
        g_PlayerController->setOnLadder(onLadder);

    // Fog is no longer resolved here: the global scene fog stays as WorldManager
    // set it at scene load, and per-zone fog is rendered per-view-ray from the
    // CONTENT_FOG volume array gathered in RenderManager::updatePerFrameUBO.
}

static void s_drawLinkArrow(const irr::core::vector3df& from, const irr::core::vector3df& to,
	const irr::video::SColor& color)
{
	auto* rm = RenderManager::Get();

	irr::core::vector3df dir = to - from;
	if (dir.getLengthSQ() < 0.01f)
	{
		// Self-link: a short vertical tick so it stays visible
		rm->renderLine3D(Line3D(irr::core::line3df(from, from + irr::core::vector3df(0, 0.5f, 0)), color));
		return;
	}

	rm->renderLine3D(Line3D(irr::core::line3df(from, to), color));

	irr::core::vector3df ndir = dir;
	ndir.normalize();

	irr::core::vector3df side = ndir.crossProduct(irr::core::vector3df(0, 1, 0));
	if (side.getLengthSQ() < 0.001f)
		side = ndir.crossProduct(irr::core::vector3df(1, 0, 0));
	side.normalize();
	irr::core::vector3df up = ndir.crossProduct(side);

	const float headLen = 0.4f;
	const float headWidth = 0.18f;

	// Head sits at the line midpoint (target ends are usually inside mesh
	// geometry). One global head color for every link kind: white — contrasts
	// with all line hues and can't be mistaken for the red error markers.
	const irr::video::SColor headColor(color.getAlpha(), 255, 255, 255);

	irr::core::vector3df tip = from + dir * 0.5f;
	irr::core::vector3df base = tip - ndir * headLen;

	rm->renderLine3D(Line3D(irr::core::line3df(tip, base + side * headWidth), headColor));
	rm->renderLine3D(Line3D(irr::core::line3df(tip, base - side * headWidth), headColor));
	rm->renderLine3D(Line3D(irr::core::line3df(tip, base + up * headWidth), headColor));
	rm->renderLine3D(Line3D(irr::core::line3df(tip, base - up * headWidth), headColor));
}

static void s_drawBrokenLinkMarker(const irr::core::vector3df& at, const irr::video::SColor& color)
{
	auto* rm = RenderManager::Get();

	// Vertical stem with an X on top: a link target name that resolves to nothing
	irr::core::vector3df top = at + irr::core::vector3df(0, 1.0f, 0);
	rm->renderLine3D(Line3D(irr::core::line3df(at, top), color));

	const float s = 0.25f;
	rm->renderLine3D(Line3D(irr::core::line3df(top + irr::core::vector3df(-s, 0.0f, 0), top + irr::core::vector3df(s, 2.0f * s, 0)), color));
	rm->renderLine3D(Line3D(irr::core::line3df(top + irr::core::vector3df(-s, 2.0f * s, 0), top + irr::core::vector3df(s, 0.0f, 0)), color));
}

void GameplaySystem::drawEntityLinkDebug()
{
	static bool f8 = false;
	if (InputManager::Get()->getKeyPressOnce(KEYBOARD_KEY::KEY_F8, &f8))
	{
		m_showEntityLinks = !m_showEntityLinks;
	}

	if (!m_showEntityLinks)
		return;

	// Same designer-debug gate as the waypoint lines below
	if (!Engine::Get()->isEditorMode() && !WorldManager::Get()->renderSystem()->isDebugSpriteVisible())
		return;

	const irr::video::SColor colorLogic(255, 0, 200, 255);      // LogicComponent::receiver
	const irr::video::SColor colorTriggered(255, 0, 255, 100);  // TriggerZoneComponent::triggered_entity
	const irr::video::SColor colorDetect(255, 255, 0, 255);     // TriggerZoneComponent::entity
	const irr::video::SColor colorBroken(255, 255, 40, 40);     // unresolved target name

	struct LinkSource
	{
		irr::core::vector3df pos;
		std::vector<std::string> tokens;
		irr::video::SColor color;
	};

	// Single pass over all entities: name lookup map + link sources
	std::unordered_map<std::string, std::vector<irr::core::vector3df>> nameToPositions;
	std::vector<LinkSource> sources;

	for (auto entity : WorldManager::Get()->world()->getEntities())
	{
		if (!entity.hasComponent<TransformComponent>() || !entity.hasComponent<DescriptorComponent>())
			continue;

		auto pos = entity.getComponent<TransformComponent>().getPosition();

		nameToPositions[entity.getComponent<DescriptorComponent>().name].push_back(pos);

		if (entity.hasComponent<LogicComponent>())
		{
			auto& logic = entity.getComponent<LogicComponent>();
			if (!logic.receiver.empty())
				sources.push_back({ pos, LogicLinks::splitNameList(logic.receiver), colorLogic });
		}

		if (entity.hasComponent<TriggerZoneComponent>())
		{
			auto& zone = entity.getComponent<TriggerZoneComponent>();
			if (!zone.triggered_entity.empty() && zone.triggered_entity != "null")
				sources.push_back({ pos, LogicLinks::splitNameList(zone.triggered_entity), colorTriggered });
			if (!zone.entity.empty() && zone.entity != "null")
				sources.push_back({ pos, std::vector<std::string>{ zone.entity }, colorDetect });
		}
	}

	// Trigger brushes fire into entities the same way — arrows from the
	// brush volume's center
	if (BrushManager::Get())
	{
		const irr::video::SColor colorBrush(255, 255, 150, 0);   // Brush::receiver
		for (const auto& brush : BrushManager::Get()->getAllBrushes())
		{
			if ((brush.contentFlags & CONTENT_TRIGGER) && brush.geometryValid && !brush.receiver.empty())
				sources.push_back({ brush.bounds.getCenter(), LogicLinks::splitNameList(brush.receiver), colorBrush });
		}
	}

	for (auto& src : sources)
	{
		for (auto& token : src.tokens)
		{
			auto it = nameToPositions.find(token);
			if (it == nameToPositions.end())
			{
				s_drawBrokenLinkMarker(src.pos, colorBroken);
				continue;
			}

			// Duplicate names fan out at runtime, so draw an arrow to every match
			for (auto& target : it->second)
				s_drawLinkArrow(src.pos, target, src.color);
		}
	}
}

void GameplaySystem::update()
{
	m_waterZones.clear();

	updateBrushVolumes();

	// Gore ticks here rather than in WorldManager so it only simulates in game
	// mode — gibs must not tumble around the editor viewport.
	if (GoreManager::Get())
		GoreManager::Get()->update(Engine::Get()->getDeltaTime());

	if (FractureManager::Get())
		FractureManager::Get()->update(Engine::Get()->getDeltaTime());

	// Stops zones from overwriting each other if there are more than one, only works for player
	static bool player_in_trigger_zone = false, player_in_water_zone = false;

	auto& entities = getEntities();
	
	for (auto& entity : entities)
	{
		auto& descriptorComponent = entity.getComponent<DescriptorComponent>();

		if (descriptorComponent.type == ET_NULL || descriptorComponent.type == ET_STATIC)
		{
			continue;
		}
		
// ---- POSITION SOUND LISTENER
		if (descriptorComponent.type == ET_PLAYER)
		{
			auto camera = entity.getComponent<CameraComponent>().camera;
			
			auto lPos    = camera->getAbsolutePosition();
			auto lTarget = camera->getTarget();
			auto lUp     = camera->getUpVector();
			// SoLoud expects a direction vector, not a target point.
			auto lLook = (lTarget - lPos).normalize();
			SoundManager::Get()->sound()->setListenerPosition(
				{ lPos.X,  lPos.Y,  lPos.Z  },
				{ lLook.X, lLook.Y, lLook.Z },
				{},
				{ lUp.X,   lUp.Y,   lUp.Z   });
		}

// ---- ENTITY INTERACTION
		if (entity.hasComponent<InteractionComponent>() && !entity.hasComponent<DialogComponent>())
		{
			if (entity.hasComponent<ScriptComponent>() && !entity.hasComponent<ItemComponent>())
			{
				if (entity.getComponent<InteractionComponent>().interact)
				{
					auto& script = entity.getComponent<ScriptComponent>();

					if (script.hasOnInteraction)
					{
						ScriptManager::Get()->execute(
							script, script.onPlayerInteractionFunc, descriptorComponent.id);
					}

					entity.getComponent<InteractionComponent>().interact = false;
				}
			}
		}

// ---- PLAYER COLLIDE
		if (entity.hasComponent<ScriptComponent>() && entity.hasComponent<TransformComponent>())
		{
			auto& script = entity.getComponent<ScriptComponent>();

			if (script.hasOnPlayerCollide && WorldManager::Get()->managerSystem()->doesEntityExist("player"))
			{
				auto& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
				if (player.isValid())
				{
					auto& transformComponent = entity.getComponent<TransformComponent>();
					auto& playerTransform    = player.getComponent<TransformComponent>();

					constexpr float PLAYER_HALF_WIDTH  = 0.25f;
					constexpr float PLAYER_HALF_HEIGHT = 0.875f;

					irr::core::vector3df xyz_min, xyz_max;
					xyz_min.X = transformComponent.getPosition().X - transformComponent.getScale().X * 0.5f - PLAYER_HALF_WIDTH;
					xyz_min.Y = transformComponent.getPosition().Y - transformComponent.getScale().Y * 0.5f - PLAYER_HALF_HEIGHT;
					xyz_min.Z = transformComponent.getPosition().Z - transformComponent.getScale().Z * 0.5f - PLAYER_HALF_WIDTH;
					xyz_max.X = transformComponent.getPosition().X + transformComponent.getScale().X * 0.5f + PLAYER_HALF_WIDTH;
					xyz_max.Y = transformComponent.getPosition().Y + transformComponent.getScale().Y * 0.5f + PLAYER_HALF_HEIGHT;
					xyz_max.Z = transformComponent.getPosition().Z + transformComponent.getScale().Z * 0.5f + PLAYER_HALF_WIDTH;

					auto test_point = playerTransform.position;
					bool overlapping =
						test_point.X <= xyz_max.X && test_point.X >= xyz_min.X &&
						test_point.Y <= xyz_max.Y && test_point.Y >= xyz_min.Y &&
						test_point.Z <= xyz_max.Z && test_point.Z >= xyz_min.Z;

					if (overlapping)
					{
						ScriptManager::Get()->execute(script, script.onPlayerCollideFunc, descriptorComponent.id);
					}
				}
			}
		}

// ---- ITEM INTERACTION
		// Deliberately gone. Taking an item is PickupBehavior's job now, with
		// requiresInteract=true selecting press-to-take over walk-over — one
		// class that knows about proximity, worth-testing, sounds and the
		// deferred kill, rather than a second hard-coded path here that knew
		// about none of it. ItemComponent stays as the world-side "what is this"
		// marker: the HUD aim prompt reads it, and the behavior falls back to it
		// so an item entity names its item once.

// ---- DIALOG INTERACTION
		if (entity.hasComponent<InteractionComponent>() && entity.hasComponent<DialogComponent>())
		{
			if (entity.getComponent<InteractionComponent>().interact)
			{
				entity.getComponent<DialogComponent>().active = true;

				entity.getComponent<InteractionComponent>().interact = false;
			}
		}

// ---- ENTITY DEATH
		if (!descriptorComponent.isAlive && !descriptorComponent.processedDeathAction)
		{
			descriptorComponent.processedDeathAction = true;

			if (entity.hasComponent<ScriptComponent>())
			{
				auto& script = entity.getComponent<ScriptComponent>();

				if (script.hasOnKillEventFunc) 
				{
					ScriptManager::Get()->execute(
						script, script.onKillEventFunc, descriptorComponent.id);
				}

				continue;
			}
		}
		
// ---- AUTOKILL COMPONENT
		if (entity.hasComponent<AutoKillComponent>())
		{
			auto& autokillComponent = entity.getComponent<AutoKillComponent>();
			
			if (Engine::Get()->getCurrentTime() - autokillComponent.spawn_encoded > autokillComponent.lifetime_ms)
			{
				descriptorComponent.isAlive = false;
			}
		}
		
// ---- DAMAGE COMPONENT
		if (entity.hasComponent<DamageReceiverComponent>())
		{
			auto &damageComponent = entity.getComponent<DamageReceiverComponent>();

			if (!damageComponent.invulnerable) 
			{
				damageComponent.health = damageComponent.threshold - damageComponent.damageReceived;
			}
			else 
			{
				damageComponent.health = damageComponent.threshold;
			}

			if (damageComponent.health > damageComponent.threshold) 
			{
				damageComponent.health = damageComponent.threshold;
			}

			if (damageComponent.health <= 0 && !damageComponent.buddha) 
			{
				descriptorComponent.isAlive = false;
			}
		}

// ---- DIALOG COMPONENT
		if (entity.hasComponent<DialogComponent>())
		{
			auto& dialog = entity.getComponent<DialogComponent>();
			
			static auto current_entry = 0U;

			static bool e_pressed = false;
			// Should detect the 'use' action need to make a way to detect action press once
			if (InputManager::Get()->getKeyPressOnce(KEYBOARD_KEY::KEY_E, &e_pressed) && dialog.active)
			{
				dialog.active = false;

				InputManager::Get()->centerMouse();

				g_PlayerController->lockPlayer(false);
				g_PlayerController->hudController()->hide(false);
			}

			if (dialog.active)
			{
				g_PlayerController->lockPlayer();
				g_PlayerController->hudController()->hide();

				if (!dialog.data.empty())
				{
					auto entry = DialogManager::GetDialog(dialog.data.at(0));

					static bool space_pressed = false;
					// Should detect the 'use' action need to make a way to detect action press once
					if (InputManager::Get()->getKeyPressOnce(KEYBOARD_KEY::KEY_SPACE, &space_pressed) && dialog.active)
					{
						if (current_entry < entry.entries.size() - 1)
						{
							// Control which entries we see
							if (entry.entries.at(current_entry + 1).id == 0)
							{
								current_entry++;
							}
						}
					}

					std::string current_text;

					current_text = entry.entries.at(current_entry).text;

					RenderManager::Get()->renderText2D(
						current_text.c_str(),
						TEXT_DEFAULT_FONT::SMALL,
						irr::core::rect<irr::s32>(
						(RenderManager::Get()->getConfiguration().width / 2) - 250, (RenderManager::Get()->getConfiguration().height / 2) + 450, 0, 0));
				}
			}
			else
			{
				current_entry = 0;
			}
		}
		
// ---- LOGIC COMPONENT
		if (entity.hasComponent<LogicComponent>())
		{
			if (entity.hasComponent<ScriptComponent>())
			{
				auto& logicComponent = entity.getComponent<LogicComponent>();

				if (logicComponent.isActivated)
				{
					std::unordered_set<entityid> visited;
					visited.insert(entity.getComponent<DescriptorComponent>().id);

					for (auto& token : LogicLinks::splitNameList(logicComponent.receiver))
					{
						for (auto* r_ent : WorldManager::Get()->managerSystem()->getEntitiesByName(token))
							propagateLogicSignal(*r_ent, visited);
					}

					logicComponent.isActivated = false;
				}
			}
		}
		
// ---- MARKER COMPONENT
		if (entity.hasComponent<MarkerComponent>())
		{
			auto& transformComponent = entity.getComponent<TransformComponent>();
			auto& markerComponent   = entity.getComponent<MarkerComponent>();

			switch (markerComponent.type)
			{
			case MT_NULL:
				break;

			case MT_PLAYER_START:
				if (Engine::Get()->isGameMode() && !markerComponent.hasUpdated)
				{
					if (!WorldManager::Get()->managerSystem()->doesEntityExist("player"))
					{
						WorldManager::Get()->spawnEntity(
							_asset_ent("player/player"), "player", false,
							transformComponent.position - irr::core::vector3df(0.0f, PLAYER_HEIGHT, 0.0f),
							irr::core::vector3df(0.0f, transformComponent.rotation.Y, 0.0f));
						
						markerComponent.hasUpdated = true;
					}
					else
					{
						spdlog::debug("MT_PLAYER_START did not spawn a player controller, one already exists");
					}
				}

				if (WorldManager::Get()->managerSystem()->doesEntityExist("player"))
				{
					auto& playerTransform = WorldManager::Get()->managerSystem()->getEntityByName("player").getComponent<TransformComponent>();
					
					transformComponent.setPosition(playerTransform.getPosition() + irr::core::vector3df(0.0f, PLAYER_HEIGHT, 0.0f));
					transformComponent.setRotation(irr::core::vector3df(0.0f, playerTransform.getRotation().Y, 0.0f));
				}
				break;

			case MT_FREECAMERA:
				if (Engine::Get()->isGameMode() && !markerComponent.hasUpdated)
				{
					if (!WorldManager::Get()->managerSystem()->getEntityByName("freecamera").isValid())
					{
						WorldManager::Get()->spawnEntity(_asset_ent("player/freecamera"), "", false, transformComponent.position,
							transformComponent.rotation);

						markerComponent.hasUpdated = true;
					}
					else
					{
						spdlog::debug("MT_FREECAMERA did not spawn a freecamera controller, one already exists");
					}
				}
				break;

			case MT_SKY_CAMERA:
				// Feed the 3D-skybox anchor + parallax scale to the renderer.
				// Anchor is static during play, but this is cheap enough to set each frame.
				RenderManager::Get()->setSkyCamera(
					transformComponent.getPosition(), markerComponent.skyScale, true);
				break;

			case MT_WAYPOINT:
				if (entity.hasComponent<DataComponent>())
				{
					auto& dataComponent = entity.getComponent<DataComponent>();

					if (!dataComponent.data.empty())
					{
						auto& next_waypoint = WorldManager::Get()->managerSystem()->getEntityByName(dataComponent.data.back());

						if (next_waypoint.isValid())
						{
							if (Engine::Get()->isEditorMode() || WorldManager::Get()->renderSystem()->isDebugSpriteVisible())
							{
								RenderManager::Get()->renderLine3D(Line3D(
									irr::core::line3df(transformComponent.getPosition(),
										next_waypoint.getComponent<TransformComponent>().getPosition()),
									irr::video::SColor(255, 255, 255, 0)));
							}
						}
					}
				}

				break;
			}
		}

// ---- TRIGGERZONE COMPONENT
		if (entity.hasComponent<TriggerZoneComponent>())
		{
			if (entity.hasComponent<TransformComponent>())
			{
				auto& transformComponent   = entity.getComponent<TransformComponent>();
				auto& triggerzoneComponent = entity.getComponent<TriggerZoneComponent>();

				irr::core::vector3df xyz_min, xyz_max, test_point;
				xyz_min.X = transformComponent.getPosition().X - transformComponent.getScale().X * 0.5f;
				xyz_min.Y = transformComponent.getPosition().Y - transformComponent.getScale().Y * 0.5f;
				xyz_min.Z = transformComponent.getPosition().Z - transformComponent.getScale().Z * 0.5f;
				xyz_max.X = transformComponent.getPosition().X + transformComponent.getScale().X * 0.5f;
				xyz_max.Y = transformComponent.getPosition().Y + transformComponent.getScale().Y * 0.5f;
				xyz_max.Z = transformComponent.getPosition().Z + transformComponent.getScale().Z * 0.5f;


				if (triggerzoneComponent.mask == TRIGGER_ZONE_MASK::PLAYER_ONLY && !player_in_trigger_zone)
				{
					if (WorldManager::Get()->managerSystem()->doesEntityExist("player"))
					{
						auto& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
						if (player.isValid())
						{
							test_point = player.getComponent<TransformComponent>().position;
							triggerzoneComponent.triggered =
								test_point.X <= xyz_max.X && test_point.X >= xyz_min.X &&
								test_point.Y <= xyz_max.Y && test_point.Y >= xyz_min.Y &&
								test_point.Z <= xyz_max.Z && test_point.Z >= xyz_min.Z;

							player_in_trigger_zone = triggerzoneComponent.triggered;
						}
					}
				}
				if (triggerzoneComponent.mask == TRIGGER_ZONE_MASK::ENTITY_NAME) // May not work with multiple zones as if entity is in one but not the other they will overwrite triggerzoneComponent.triggered
				{
					auto& test_entity = WorldManager::Get()->managerSystem()->getEntityByName(triggerzoneComponent.entity);
					
					if (test_entity.isValid())
					{
						test_point = test_entity.getComponent<TransformComponent>().position;
						
						triggerzoneComponent.triggered =
							(test_point.X <= xyz_max.X && test_point.X >= xyz_min.X) &&
							(test_point.Y <= xyz_max.Y && test_point.Y >= xyz_min.Y) &&
							(test_point.Z <= xyz_max.Z && test_point.Z >= xyz_min.Z);
					}
				}

				if (!triggerzoneComponent.triggered && !triggerzoneComponent.single_use)
				{
					triggerzoneComponent.reset = true;
				}
				if (triggerzoneComponent.triggered && !triggerzoneComponent.single_use && !triggerzoneComponent.toggle)
				{
					triggerzoneComponent.reset = true;
				}

				if (triggerzoneComponent.triggered && triggerzoneComponent.reset)
				{
					if (triggerzoneComponent.triggered_entity == "null" || triggerzoneComponent.triggered_entity.empty())
					{
						continue;
					}

					for (auto& token : LogicLinks::splitNameList(triggerzoneComponent.triggered_entity))
					{
						auto& entity = WorldManager::Get()->managerSystem()->getEntityByName(token);
						if (entity.isValid())
						{
							if (entity.hasComponent<LogicComponent>() && entity.hasComponent<ScriptComponent>())
							{
								auto& ent_logic = entity.getComponent<LogicComponent>();
								auto& ent_script = entity.getComponent<ScriptComponent>();

								if (ent_script.hasOnLogicEventActivate)
								{
									ent_logic.isActivated = false;
									ScriptManager::Get()->execute(
										ent_script, ent_script.onLogicEventActivate,
										entity.getComponent<DescriptorComponent>().id);
								}
								else
								{
									ent_logic.isActivated = !triggerzoneComponent.invert;
								}

								if (ent_logic.isActivated && !ent_logic.receiver.empty())
								{
									std::unordered_set<entityid> visited;
									visited.insert(entity.getComponent<DescriptorComponent>().id);
									for (auto& t : LogicLinks::splitNameList(ent_logic.receiver))
									{
										for (auto* next : WorldManager::Get()->managerSystem()->getEntitiesByName(t))
											propagateLogicSignal(*next, visited);
									}
									ent_logic.isActivated = false;
								}

								triggerzoneComponent.reset = false;
							}
							else if (entity.hasComponent<LightComponent>())
							{
								auto& render = entity.getComponent<RenderComponent>();
								if (triggerzoneComponent.toggle)
								{
									render.isVisible = !render.isVisible;
								}
								if (triggerzoneComponent.invert && !triggerzoneComponent.toggle)
								{
									render.isVisible = false;
								}
								if (!triggerzoneComponent.invert && !triggerzoneComponent.toggle)
								{
									render.isVisible = true;
								}


								triggerzoneComponent.reset = false;
							}
							else if (entity.hasComponent<SoundComponent>())
							{
								auto& sound = entity.getComponent<SoundComponent>();

								if (triggerzoneComponent.toggle)
								{
									sound.play(sound.sounds.at(0).name);
								}
								else
								{
									triggerzoneComponent.toggle = true;
									triggerzoneComponent.single_use = true;

									sound.play(sound.sounds.at(0).name);
								}

								triggerzoneComponent.reset = false;
							}
						}
					}
				}
			}
		}

		// --------- WATER COMPONENT
		if (entity.hasComponent<WaterComponent>() && !player_in_water_zone)
		{
			if (entity.hasComponent<TransformComponent>())
			{
				auto& transformComponent = entity.getComponent<TransformComponent>();

				irr::core::vector3df xyz_min, xyz_max;

				xyz_min.X = transformComponent.getPosition().X - transformComponent.getScale().X * 0.5f;
				xyz_min.Y = transformComponent.getPosition().Y - transformComponent.getScale().Y * 0.5f;
				xyz_min.Z = transformComponent.getPosition().Z - transformComponent.getScale().Z * 0.5f;
				xyz_max.X = transformComponent.getPosition().X + transformComponent.getScale().X * 0.5f;
				xyz_max.Y = transformComponent.getPosition().Y + transformComponent.getScale().Y * 0.5f;
				xyz_max.Z = transformComponent.getPosition().Z + transformComponent.getScale().Z * 0.5f;

				m_waterZones.emplace_back(xyz_min, xyz_max);

				if (WorldManager::Get()->managerSystem()->doesEntityExist("player"))
				{
					auto& player = WorldManager::Get()->managerSystem()->getEntityByName("player");

					if (player.isValid())
					{
						// Get camera position so player isn't swimming until camera is under water
						auto test_point1 = player.getComponent<CameraComponent>().camera->getAbsolutePosition() - irr::core::vector3df(0.0, 0.75, 0.0);
						auto test_point2 = player.getComponent<CameraComponent>().camera->getAbsolutePosition() + irr::core::vector3df(0.0, 0.0, 0.0);

						g_PlayerController->setIsSwimming(
							test_point1.X <= xyz_max.X && test_point1.X >= xyz_min.X &&
							test_point1.Y <= xyz_max.Y && test_point1.Y >= xyz_min.Y &&
							test_point1.Z <= xyz_max.Z && test_point1.Z >= xyz_min.Z);

						g_PlayerController->setIHeadUnderWater(
							test_point2.X <= xyz_max.X && test_point2.X >= xyz_min.X &&
							test_point2.Y <= xyz_max.Y && test_point2.Y >= xyz_min.Y &&
							test_point2.Z <= xyz_max.Z && test_point2.Z >= xyz_min.Z);

						player_in_water_zone = g_PlayerController->isSwimming();
					}
				}
			}
		}
	}

	player_in_trigger_zone = false;
	player_in_water_zone = false;
}

void GameplaySystem::destroy()
{
	if (GoreManager::Get())
		GoreManager::Get()->clear();

	GoreManager::destroy();

	if (FractureManager::Get())
		FractureManager::Get()->clear();

	FractureManager::destroy();
}

HIT_RESULT GameplaySystem::damageEntity(entityid id, unsigned int damage, DAMAGE_TYPE type,
                                        const DamageContext& ctx)
{
	auto& entities = getEntities();
	for (auto i = 0U; i < entities.size(); i++)
	{
		if (entities[i].getComponent<DescriptorComponent>().id == id)
		{
			if (entities[i].hasComponent<DamageReceiverComponent>())
			{
				auto& desc  = entities[i].getComponent<DescriptorComponent>();
				auto& dcomp = entities[i].getComponent<DamageReceiverComponent>();

				// Was it lethally damaged before this hit? (update() may not have
				// flipped isAlive yet — mirror its health rule so same-frame
				// follow-up hits on a dying entity don't double-report the kill)
				bool pendingDead = !dcomp.invulnerable && !dcomp.buddha &&
					(dcomp.threshold - dcomp.damageReceived) <= 0;

				dcomp.damageReceived += damage;
				dcomp.lastReceivedType = type;
				dcomp.receivedDamage = true;

				// Flesh bleeds; a crate or barrel throws debris instead. Resolved
				// once here so the corpse and alive branches agree.
				const IMPACT_SURFACE impactSurf =
					resolveImpactSurface(entities[i], dcomp.impactSurface);
				const bool fleshy = (impactSurf == IMPACT_FLESH);

				if (!desc.isAlive || pendingDead)
				{
					// A fractured prop no longer exists — its entity is already queued
					// for removal and its shards are in the air. Anything still
					// landing on it this frame gets no further theatre.
					if (dcomp.fractured)
						return HIT_RESULT::NONE;

					// Corpse. Damage is still recorded above — that accumulation is
					// what lets a body keep climbing the overkill ladder while it is
					// shot, and cross the gib threshold long after it died.
					if (fleshy && GoreManager::Get() && !dcomp.gibbed)
					{
						const float overkill = dcomp.overkillRatio();

						if (overkill >= GoreManager::Get()->gibRatio)
						{
							dcomp.gibbed = true;
							GoreManager::Get()->gib(entities[i], ctx, overkill);
						}
						else
						{
							GoreManager::Get()->wound(entities[i], ctx, damage);
						}
					}
					else if (!fleshy)
					{
						// A non-fracturing prop that keeps eating fire after it "died"
						// — pockmark it, don't bleed it.
						playSurfaceImpact(impactSurf, ctx, false);
					}

					return HIT_RESULT::NONE; // no hitmarker for a corpse
				}

				bool willDie = !dcomp.invulnerable && !dcomp.buddha &&
					(dcomp.threshold - dcomp.damageReceived) <= 0;

				// Fracture wins over the gore ladder: a crate should not bleed.
				// fracture() returns false when it cannot take this entity apart
				// (skinned mesh, no node, degenerate bounds), and the gore path
				// below then runs as normal — a mis-flagged NPC still dies
				// properly instead of dying silently.
				if (willDie && dcomp.fractureOnDeath && !dcomp.fractured && FractureManager::Get())
				{
					if (FractureManager::Get()->fracture(entities[i], ctx))
					{
						dcomp.fractured     = true;
						dcomp.deathResolved = true;

						return HIT_RESULT::KILL;
					}
				}

				if (fleshy && GoreManager::Get())
				{
					if (willDie)
					{
						const float overkill = dcomp.overkillRatio();

						dcomp.deathResolved = true;

						// TIER_GIB means the body is removed outright, so the behavior
						// layer must not also play a death animation over the top.
						if (GoreManager::Get()->kill(entities[i], ctx, overkill) == TIER_GIB)
							dcomp.gibbed = true;
					}
					else
					{
						GoreManager::Get()->wound(entities[i], ctx, damage);
					}
				}
				else if (!fleshy)
				{
					// Non-flesh and either not flagged to fracture, or fracture()
					// declined (skinned mesh, no node, degenerate bounds). Either
					// way: debris, not blood. A killing blow reads heavier.
					if (willDie)
						dcomp.deathResolved = true;

					playSurfaceImpact(impactSurf, ctx, willDie);
				}

				return willDie ? HIT_RESULT::KILL : HIT_RESULT::HIT;
			}
			return HIT_RESULT::NONE;
		}
	}
	return HIT_RESULT::NONE;
}

void GameplaySystem::healEntity(entityid id, unsigned int heal)
{
	auto& entities = getEntities();
	for (auto i = 0U; i < entities.size(); i++)
	{
		if (entities[i].getComponent<DescriptorComponent>().id == id)
		{
			if (entities[i].hasComponent<DamageReceiverComponent>())
			{
				auto& dcomp = entities[i].getComponent<DamageReceiverComponent>();

				dcomp.damageReceived -= heal;

				if (dcomp.damageReceived < 0)
				{
					dcomp.damageReceived = 0;
				}

				return;
			}
		}
	}
}

void GameplaySystem::setInvulnerable(entityid id, bool set)
{
	auto& entities = getEntities();
	for (auto i = 0U; i < entities.size(); i++)
	{
		if (entities[i].getComponent<DescriptorComponent>().id == id)
		{
			entities[i].getComponent<DamageReceiverComponent>().invulnerable = set;
			
			return;
		}
	}
}

void GameplaySystem::setBuddha(entityid id, bool set)
{
	auto& entities = getEntities();
	for (auto i = 0U; i < entities.size(); i++)
	{
		if (entities[i].getComponent<DescriptorComponent>().id == id)
		{
			entities[i].getComponent<DamageReceiverComponent>().buddha = set;
			
			return;
		}
	}
}

void GameplaySystem::interact(entityid receiver)
{
	auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(receiver);

	if (entity.isValid()) 
	{
		if (entity.hasComponent<InteractionComponent>()) 
		{
			entity.getComponent<InteractionComponent>().interact = true;
		}
	}
}