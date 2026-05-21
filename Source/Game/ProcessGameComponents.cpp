#include "Game/GameState.h"

#include "Engine/Engine.h"

#include "Components.h"

#include <spdlog/spdlog.h>

void GameState::serializeComponent(anax::Entity& entity, cereal::XMLOutputArchive& archive)
{
	if (entity.hasComponent<AutoKillComponent>())
	{
		archive.setNextName("autokill");
		archive.startNode();
		archive(entity.getComponent<AutoKillComponent>());
		archive.finishNode();
	}

	if (entity.hasComponent<DamageReceiverComponent>())
	{
		archive.setNextName("damagereceiver");
		archive.startNode();
		archive(entity.getComponent<DamageReceiverComponent>());
		archive.finishNode();
	}

	if (entity.hasComponent<DataComponent>())
	{
		archive.setNextName("data");
		archive.startNode();
		archive(entity.getComponent<DataComponent>());
		archive.finishNode();
	}

	if (entity.hasComponent<DialogComponent>())
	{
		archive.setNextName("dialog");
		archive.startNode();
		archive(entity.getComponent<DialogComponent>());
		archive.finishNode();
	}

	if (entity.hasComponent<InteractionComponent>())
	{
		archive.setNextName("interaction");
		archive.startNode();
		archive(entity.getComponent<InteractionComponent>());
		archive.finishNode();
	}

	if (entity.hasComponent<ItemComponent>())
	{
		archive.setNextName("item");
		archive.startNode();
		archive(entity.getComponent<ItemComponent>());
		archive.finishNode();
	}

	if (entity.hasComponent<LogicComponent>())
	{
		archive.setNextName("logic");
		archive.startNode();
		archive(entity.getComponent<LogicComponent>());
		archive.finishNode();
	}

	if (entity.hasComponent<MarkerComponent>())
	{
		archive.setNextName("marker");
		archive.startNode();
		archive(entity.getComponent<MarkerComponent>());
		archive.finishNode();
	}

	if (entity.hasComponent<NPCComponent>())
	{
		archive.setNextName("npc");
		archive.startNode();
		archive(entity.getComponent<NPCComponent>());
		archive.finishNode();
	}

	if (entity.hasComponent<TriggerZoneComponent>())
	{
		archive.setNextName("triggerzone");
		archive.startNode();
		archive(entity.getComponent<TriggerZoneComponent>());
		archive.finishNode();
	}

	if (entity.hasComponent<WaterComponent>())
	{
		archive.setNextName("water");
		archive.startNode();
		archive(entity.getComponent<WaterComponent>());
		archive.finishNode();
	}

}

void GameState::deserializeComponent(anax::Entity& entity, cereal::XMLInputArchive& archive,
                                     const std::unordered_set<std::string>& present,
                                     const std::string& entityName)
{
	auto has = [&](const char* n) { return present.count(n) > 0; };

	auto load = [&](const char* nodeName, auto loadFn)
	{
		if (!has(nodeName)) return;
		spdlog::debug("  [{}] loading component: {}", entityName, nodeName);
		archive.setNextName(nodeName);
		archive.startNode();
		loadFn();
		archive.finishNode();
	};

	load("autokill", [&]() {
		entity.addComponent<AutoKillComponent>();
		archive(entity.getComponent<AutoKillComponent>());
	});

	load("damagereceiver", [&]() {
		entity.addComponent<DamageReceiverComponent>();
		archive(entity.getComponent<DamageReceiverComponent>());
	});

	load("data", [&]() {
		entity.addComponent<DataComponent>();
		archive(entity.getComponent<DataComponent>());
	});

	load("dialog", [&]() {
		entity.addComponent<DialogComponent>();
		archive(entity.getComponent<DialogComponent>());
	});

	load("interaction", [&]() {
		entity.addComponent<InteractionComponent>();
		archive(entity.getComponent<InteractionComponent>());
	});

	load("item", [&]() {
		entity.addComponent<ItemComponent>();
		archive(entity.getComponent<ItemComponent>());
	});

	load("logic", [&]() {
		entity.addComponent<LogicComponent>();
		archive(entity.getComponent<LogicComponent>());
	});

	load("marker", [&]() {
		entity.addComponent<MarkerComponent>();
		archive(entity.getComponent<MarkerComponent>());
	});

	load("npc", [&]() {
		entity.addComponent<NPCComponent>();
		archive(entity.getComponent<NPCComponent>());
	});

	load("triggerzone", [&]() {
		entity.addComponent<TriggerZoneComponent>();
		archive(entity.getComponent<TriggerZoneComponent>());
	});

	load("water", [&]() {
		entity.addComponent<WaterComponent>();
		archive(entity.getComponent<WaterComponent>());
	});

}
