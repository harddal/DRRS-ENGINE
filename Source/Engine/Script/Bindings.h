#pragma once

#include "angelscript.h"

class ScriptBindings
{
public:
    static void RegisterBindings(asIScriptEngine* engine)
    {
        RegisterVector2d(engine);
        RegisterVector3d(engine);

        RegisterCore(engine);
        RegisterEntity(engine);
        RegisterInputManager(engine);
        RegisterPhysX(engine);
        RegisterRender(engine);
		RegisterSound(engine);
		RegisterGame(engine);
		RegisterTween(engine);
		RegisterNavigation(engine);
    }

    // Called at the top of ScriptSystem::update() each frame to reset per-frame
    // accumulators for multi-source effects (radiation, film grain, Geiger).
    static void ResetRenderAccumulators();
    static void ResetSoundAccumulators();

    // Called from killAllEntities() on session boundaries to reset refcounts
    // that track how many entities have enabled the Geiger voice.
    static void BeginSoundSession();

private:
    static void RegisterVector2d(asIScriptEngine* engine);
    static void RegisterVector3d(asIScriptEngine* engine);

    static void RegisterCore(asIScriptEngine* engine);
    static void RegisterInputManager(asIScriptEngine* engine);
    static void RegisterEntity(asIScriptEngine* engine);
    static void RegisterPhysX(asIScriptEngine* engine);
    static void RegisterRender(asIScriptEngine* engine);
	static void RegisterSound(asIScriptEngine* engine);
	static void RegisterGame(asIScriptEngine* engine);
	static void RegisterTween(asIScriptEngine* engine);
	static void RegisterNavigation(asIScriptEngine* engine);
};
