#pragma once

#include "Engine/World/Components/ScriptComponent.h"

#include "angelscript.h"

#include "Engine/Script/Bindings.h"

class ScriptManager
{
public:
    ScriptManager();
    ~ScriptManager();

	static void destroy() {
		delete s_Instance;
		s_Instance = nullptr;
	}

    int execute(std::string module, std::string function, int entid = 0xFFFF);
    // TODO: Change the ScriptComponent parameter to accept a module name string instead
    int execute(ScriptComponent script, asIScriptFunction* function, int entid = 0xFFFF);

    // For a script function declared to return bool, e.g. canUse. execute()
    // above always returns 0 and discards the value, so a predicate hook needs
    // its own path. 'fallback' is returned when the call does not complete, so
    // a broken script fails OPEN rather than making an item permanently unusable.
    bool executeBool(ScriptComponent script, asIScriptFunction* function, int entid = 0xFFFF, bool fallback = true);

    int compile(std::string filename, std::string module);
    int compile(ScriptComponent& script);

    int removeModule(std::string module) const { return m_engine->DiscardModule(module.c_str()); }

    asIScriptEngine* getEngine() { return m_engine; }

    static ScriptManager* Get() { return s_Instance; }

private:
    static ScriptManager* s_Instance;

    static void MessageCallback(const asSMessageInfo* msg, void* param);

    void ConfigureEngine(asIScriptEngine* engine);

    static void LineCallback(asIScriptContext* ctx, unsigned long* timeOut) { if (*timeOut < 1000/*timeGetTime()*/) { ctx->Abort(); } };

    asIScriptEngine* m_engine;
    asIScriptContext* m_executionContext;
};
