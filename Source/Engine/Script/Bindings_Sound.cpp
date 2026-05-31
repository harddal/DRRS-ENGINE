#include "Bindings.h"

#include "Engine/Engine.h"
#include "Engine/Sound/GeigerEffect.h"

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

static void AS_PlayEntitySoundByIndex(int e, int index)
{
    auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);

    if (!entity.isValid()) return;
    if (!entity.hasComponent<SoundComponent>()) return;

    auto& sounds = entity.getComponent<SoundComponent>().sounds;
    if (index >= 0 && index < (int)sounds.size())
        sounds[index].play = true;
}

static void AS_PlaySoundAtPosition(irr::core::vector3df pos, std::string file)
{
	SoundManager::Get()->sound()->play3D(file.c_str(), pos);
}

static int AS_Play2D(std::string file, float volume, bool loop)
{
	return (int)SoundManager::Get()->sound()->play2D(file.c_str(), loop, 0, volume).handle();
}

static int AS_Play3D(irr::core::vector3df pos, std::string file, float volume, bool loop)
{
	return (int)SoundManager::Get()->sound()->play3D(file.c_str(), pos, loop, false, true, 0, volume).handle();
}

static void AS_StopSound(int handle)
{
	SoundManager::Get()->sound()->stopHandle((SoLoud::handle)handle);
}

// Per-frame Geiger strength accumulator (max across all radiation sources).
static float s_geigerAccum = 0.0f;

static void AS_GeigerSetEnabled(bool b)   { GeigerEffect::Get()->setEnabled(b); }
static void AS_GeigerSetStrength(float s) { GeigerEffect::Get()->setStrength(s); }
static void AS_GeigerSetGain(float g)     { GeigerEffect::Get()->setGain(g); }
static void AS_GeigerUpdate(float dt)     { GeigerEffect::Get()->update(dt); }

static void AS_ContributeGeigerStrength(float s)
{
#undef max
    s_geigerAccum = std::max(s_geigerAccum, s);
    GeigerEffect::Get()->setStrength(s_geigerAccum);
}

void ScriptBindings::ResetSoundAccumulators()
{
    s_geigerAccum = 0.0f;
    GeigerEffect::Get()->setStrength(0.0f);
}

void ScriptBindings::BeginSoundSession()
{
    s_geigerAccum = 0.0f;
    GeigerEffect::Get()->resetSession();
}

void ScriptBindings::RegisterSound(asIScriptEngine* engine)
{
	engine->SetDefaultNamespace("sound");
	{
		engine->RegisterGlobalFunction("void play(int entityId, string sound)", asFUNCTION(AS_PlayEntitySound), asCALL_CDECL);
		engine->RegisterGlobalFunction("void playIndex(int entityId, int index)", asFUNCTION(AS_PlayEntitySoundByIndex), asCALL_CDECL);
		engine->RegisterGlobalFunction("void playAt(vector3d position, string sound)", asFUNCTION(AS_PlaySoundAtPosition), asCALL_CDECL);
		engine->RegisterGlobalFunction("int play2d(string file, float volume = 1.0f, bool loop = false)", asFUNCTION(AS_Play2D), asCALL_CDECL);
		engine->RegisterGlobalFunction("int play3d(vector3d position, string file, float volume = 1.0f, bool loop = false)", asFUNCTION(AS_Play3D), asCALL_CDECL);
		engine->RegisterGlobalFunction("void stop(int handle)", asFUNCTION(AS_StopSound), asCALL_CDECL);

		engine->RegisterGlobalFunction("void setGeigerEnabled(bool enabled)", asFUNCTION(AS_GeigerSetEnabled), asCALL_CDECL);
		engine->RegisterGlobalFunction("void setGeigerStrength(float strength)", asFUNCTION(AS_GeigerSetStrength), asCALL_CDECL);
		engine->RegisterGlobalFunction("void setGeigerGain(float gain)", asFUNCTION(AS_GeigerSetGain), asCALL_CDECL);
		engine->RegisterGlobalFunction("void contributeGeigerStrength(float strength)", asFUNCTION(AS_ContributeGeigerStrength), asCALL_CDECL);
		engine->RegisterGlobalFunction("void updateGeiger(float dt)", asFUNCTION(AS_GeigerUpdate), asCALL_CDECL);
	}

	engine->SetDefaultNamespace("");
}