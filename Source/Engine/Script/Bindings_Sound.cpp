#include "Bindings.h"

#include "Engine/Engine.h"

static void AS_PlayEntitySound(int e, std::string sound)
{
		auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);

		if (!entity.isValid())
		{
			return;
		}

		if (entity.hasComponent<SoundComponent>())
		{
			entity.getComponent<SoundComponent>().play(sound);
		}
}

static void AS_PlaySoundAtPosition(irr::core::vector3df pos, std::string file)
{
	SoundManager::Get()->sound()->play3D(file.c_str(), pos);
}

void ScriptBindings::RegisterSound(asIScriptEngine* engine)
{
	engine->SetDefaultNamespace("sound");
	{
		engine->RegisterGlobalFunction("void play(int entityId, string sound)", asFUNCTION(AS_PlayEntitySound), asCALL_CDECL);
		engine->RegisterGlobalFunction("void playAt(vector3d position, string sound)", asFUNCTION(AS_PlaySoundAtPosition), asCALL_CDECL);
	}

	engine->SetDefaultNamespace("");
}