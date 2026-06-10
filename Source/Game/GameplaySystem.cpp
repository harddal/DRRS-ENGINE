#include "GameplaySystem.h"

#include <spdlog/spdlog.h>

#include "Engine/Engine.h"
#include "Engine/Resource/FilePaths.h"
#include "Engine/World/WorldManager.h"
#include "Game/Components.h"
#include "Player/PlayerController.h"

#include "Game/Item/ItemDatabase.h"

#include "DialogManager.h"

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
	
}

static std::vector<std::string> splitReceiver(const std::string& s)
{
    std::vector<std::string> tokens;
    std::stringstream ss(s);
    std::string tok;
    while (getline(ss, tok, ','))
        tokens.push_back(tok);
    return tokens;
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
            for (auto& token : splitReceiver(logic.receiver))
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

void GameplaySystem::update()
{
	m_waterZones.clear();

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
		if (entity.hasComponent<InteractionComponent>() && entity.hasComponent<ItemComponent>())
		{
			if (entity.getComponent<InteractionComponent>().interact)
			{
				if (g_PlayerController->inventoryController()->pickupItem(
					entity.getComponent<ItemComponent>().item,
					entity.getComponent<DescriptorComponent>().id))
				{
					WorldManager::Get()->killEntityByID(entity.getComponent<DescriptorComponent>().id);

					continue;
				}
			}
		}

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

					for (auto& token : splitReceiver(logicComponent.receiver))
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

					std::vector<std::string> tokens;
					std::stringstream check1(triggerzoneComponent.triggered_entity);
					std::string intermediate;
					while (getline(check1, intermediate, ','))
					{
						tokens.push_back(intermediate);
					}

					for (auto& token : tokens)
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
									for (auto& t : splitReceiver(ent_logic.receiver))
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
	
}

void GameplaySystem::damageEntity(entityid id, unsigned int damage, DAMAGE_TYPE type)
{
	auto& entities = getEntities();
	for (auto i = 0U; i < entities.size(); i++)
	{
		if (entities[i].getComponent<DescriptorComponent>().id == id)
		{
			if (entities[i].hasComponent<DamageReceiverComponent>())
			{
				auto& dcomp = entities[i].getComponent<DamageReceiverComponent>();

				dcomp.damageReceived += damage;
				dcomp.lastReceivedType = type;
				dcomp.receivedDamage = true;

				return;
			}
		}
	}
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