#include "ScriptSystem.h"

#include <cassert>
#include <string>

#include "Engine/Script/Bindings.h"

#include "Engine/Resource/FilePaths.h"

#include <boost/range/iterator_range.hpp>
#include <boost/filesystem.hpp>
#include <boost/regex.hpp>

#include "Utility/Utility.h"
#include <spdlog/spdlog.h>

#include "Engine/Engine.h"

using namespace std;
using namespace boost;
using namespace filesystem;
using namespace anax;

ScriptSystem::ScriptSystem()
{
	m_id_generator = 0L;
}
ScriptSystem::~ScriptSystem()
{

}

void ScriptSystem::onEntityAdded(Entity& entity)
{
	auto& scriptComponent = entity.getComponent<ScriptComponent>();
	if (scriptComponent.script.empty()) return;

	// TODO: *** CRITICAL: Need proper unique module implementation, non-unique modules WILL cause a crash
	scriptComponent.module = "mod" + std::to_string(m_id_generator++); // <- This is unacceptable, but it works for now

	ScriptManager::Get()->compile(scriptComponent);

	if (SCRIPT_EDIT_CONTINUE_SUPPORT)
	{
		if (boost::filesystem::exists(scriptComponent.script))
			m_scriptModTimes[scriptComponent.script] = boost::filesystem::last_write_time(scriptComponent.script);
	}
}

void ScriptSystem::onEntityRemoved(Entity& entity)
{
	ScriptManager::Get()->removeModule(entity.getComponent<ScriptComponent>().module);
}

void ScriptSystem::update()
{
	ScriptBindings::ResetRenderAccumulators();
	ScriptBindings::ResetSoundAccumulators();

	auto& entities = getEntities();

	for (auto& entity : entities)
	{

		auto& descriptorComponent = entity.getComponent<DescriptorComponent>();
		auto& scriptComponent = entity.getComponent<ScriptComponent>();

		if (scriptComponent.active && scriptComponent.initialized) {

			if (scriptComponent.hasUpdate) {
				ScriptManager::Get()->execute(
					scriptComponent, scriptComponent.updateFunc, descriptorComponent.id);
			}

			continue;
		}

		if (!scriptComponent.initialized) {
			if (scriptComponent.hasInit) {
				ScriptManager::Get()->execute(
					scriptComponent, scriptComponent.initFunc, descriptorComponent.id);
			}

			scriptComponent.initialized = true;
			scriptComponent.active = true;
		}
	}
}

void ScriptSystem::updateHotReloadWatch()
{
	auto& entities = getEntities();

	// Poll for script file changes every 500ms
	bool shouldPoll = false;
	if (SCRIPT_EDIT_CONTINUE_SUPPORT)
	{
		double now = static_cast<double>(Engine::Get()->getCurrentTime());
		if (now >= m_nextPollTime)
		{
			m_nextPollTime = now + 500.0;
			shouldPoll = true;
		}
	}

	for (auto& entity : entities) {

		auto& descriptorComponent = entity.getComponent<DescriptorComponent>();
		auto& scriptComponent = entity.getComponent<ScriptComponent>();

		if (SCRIPT_EDIT_CONTINUE_SUPPORT && shouldPoll && !scriptComponent.script.empty())
		{
			const std::string& scriptPath = scriptComponent.script;
			auto it = m_scriptModTimes.find(scriptPath);
			bool fileExists = boost::filesystem::exists(scriptPath);

			if (it != m_scriptModTimes.end() && fileExists)
			{
				std::time_t modTime = boost::filesystem::last_write_time(scriptPath);
				if (it->second != modTime)
				{
					it->second = modTime;

					// Null out cached function pointers — do NOT Release(), the module owns them
					// and removeModule below will free them.
					auto clear = [](asIScriptFunction*& f) { f = nullptr; };
					clear(scriptComponent.initFunc);
					clear(scriptComponent.updateFunc);
					clear(scriptComponent.destroyFunc);
					clear(scriptComponent.onPlayerInteractionFunc);
					clear(scriptComponent.onKillEventFunc);
					clear(scriptComponent.onUseEventFunc);
					clear(scriptComponent.onLogicEventActivate);
					clear(scriptComponent.onPlayerCollideFunc);
					clear(scriptComponent.npcUpdateFunc);

					// Reset has* flags so removed functions don't leave stale true values
					scriptComponent.hasInit = scriptComponent.hasUpdate = scriptComponent.hasDestroy = false;
					scriptComponent.hasOnInteraction = scriptComponent.hasOnKillEventFunc = false;
					scriptComponent.hasOnUseEventFunc = scriptComponent.hasOnLogicEventActivate = false;
					scriptComponent.hasOnPlayerCollide = scriptComponent.hasNpcUpdate = false;

					ScriptManager::Get()->removeModule(scriptComponent.module);
					ScriptManager::Get()->compile(scriptComponent);

					if (SCRIPT_EDIT_CONTINUE_REINIT && scriptComponent.hasInit)
					{
						ScriptManager::Get()->execute(
							scriptComponent, scriptComponent.initFunc, descriptorComponent.id);
					}

					spdlog::info("Hot-reloaded script: {}", scriptComponent.script);
				}
			}
		}
	}
}

void ScriptSystem::compile(ScriptComponent &script)
{
	// TODO: Need proper unique module implementation and checking if a module already exists
	script.module = Utility::FilenameFromPath(_asset_asc(script.script));

	ScriptManager::Get()->compile(script);
}

void ScriptSystem::execute(ScriptComponent &script, unsigned int id)
{
	ScriptManager::Get()->execute(
		script, script.onUseEventFunc, id);
}

//void ScriptSystem::exportScript(const std::string& path, const std::string& module)
//{
//    ScriptManager::Get()->exportScriptByteCode(path, module);
//}
//
//void ScriptSystem::importScript(const std::string& path, const std::string& module)
//{
//    ScriptManager::Get()->importScriptByteCode(path, module);
//}
