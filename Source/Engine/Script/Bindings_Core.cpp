#include "Bindings.h"

#include <string>

#include "Engine/Engine.h"
#include "Utility/Utility.h"

// DEBUG
static void AS_Debug_Warning(std::string& str) { Utility::Warning(str); }
static void AS_Debug_Error(std::string& str) { Utility::Error(str); }

// LOG
static void AS_Log_Info(std::string& str) { spdlog::info(str); }
static void AS_Log_Debug(std::string& str) { spdlog::debug(str); }
static void AS_Log_Warning(std::string& str) { spdlog::warn(str); }
static void AS_Log_Error(std::string& str) { spdlog::error(str); }

// MATH
static int   AS_Random_Int(irr::u32 n)                                          { return rand() % n + 1; }
static irr::core::vector3df AS_Normalize_Vector3(irr::core::vector3df& v)       { return v.normalize(); }
static int   AS_Math_Abs_I(int v)                                                { return std::abs(v); }
static float AS_Math_Min_F(float a, float b)                                     { return a < b ? a : b; }
static float AS_Math_Max_F(float a, float b)                                     { return a > b ? a : b; }
static int   AS_Math_Min_I(int a, int b)                                         { return a < b ? a : b; }
static int   AS_Math_Max_I(int a, int b)                                         { return a > b ? a : b; }
static float AS_Math_Clamp(float v, float lo, float hi)                          { return v < lo ? lo : (v > hi ? hi : v); }
static float AS_Math_Lerp(float a, float b, float t)                             { return a + (b - a) * t; }
static float AS_Math_Distance(irr::core::vector3df& a, irr::core::vector3df& b) { return a.getDistanceFrom(b); }
static float AS_Math_Sin(float x)                                                { return sinf(x); }
static float AS_Math_Cos(float x)                                                { return cosf(x); }
static float AS_Math_Sqrt(float x)                                               { return sqrtf(x); }
static float AS_Math_Atan2(float y, float x)                                     { return atan2f(y, x); }
static float AS_Math_Pow(float base, float exp)                                  { return powf(base, exp); }
static float AS_Math_Floor(float x)                                              { return floorf(x); }
static float AS_Math_Ceil(float x)                                               { return ceilf(x); }

// STRING
static std::string AS_String_To_String_Int(irr::u32& in) { return std::to_string(in); }
static std::string AS_String_To_String_Float(irr::f32& in) { return std::to_string(in); }
static int AS_Int_To_String(std::string& in) { return atoi(in.c_str()); }
static float AS_Float_To_String(std::string& in) { return static_cast<float>(atof(in.c_str())); }

// TIME
static irr::u32 AS_Get_Time_Current() { return Engine::Get()->getCurrentTime(); }
static irr::u32 AS_Get_Time_Delta() { return static_cast<irr::u32>(Engine::Get()->getDeltaTime()); }
static irr::f32 AS_Get_Time_Delta_Seconds() { return Engine::Get()->getDeltaTime() * 0.001f; }

void ScriptBindings::RegisterCore(asIScriptEngine* engine)
{
    engine->SetDefaultNamespace("debug");
    {
        engine->RegisterGlobalFunction("void warning(string &in msg)", asFUNCTION(AS_Debug_Warning), asCALL_CDECL);
        engine->RegisterGlobalFunction("void error(string &in msg)", asFUNCTION(AS_Debug_Error), asCALL_CDECL);
    }

    engine->SetDefaultNamespace("log");
    {
        engine->RegisterGlobalFunction("void info(string &in msg)", asFUNCTION(AS_Log_Info), asCALL_CDECL);
        engine->RegisterGlobalFunction("void debug(string &in msg)", asFUNCTION(AS_Log_Debug), asCALL_CDECL);
        engine->RegisterGlobalFunction("void warning(string &in msg)", asFUNCTION(AS_Log_Warning), asCALL_CDECL);
        engine->RegisterGlobalFunction("void error(string &in msg)", asFUNCTION(AS_Log_Error), asCALL_CDECL);
    }

    engine->SetDefaultNamespace("math");
    {
        engine->RegisterGlobalFunction("vector3d normalize(vector3d v)",              asFUNCTION(AS_Normalize_Vector3), asCALL_CDECL);
        engine->RegisterGlobalFunction("float distance(vector3d a, vector3d b)",      asFUNCTION(AS_Math_Distance),     asCALL_CDECL);
        engine->RegisterGlobalFunction("int random(int max)",                         asFUNCTION(AS_Random_Int),        asCALL_CDECL);
        engine->RegisterGlobalFunction("int abs(int v)",                              asFUNCTION(AS_Math_Abs_I),        asCALL_CDECL);
        engine->RegisterGlobalFunction("float min(float a, float b)",                 asFUNCTION(AS_Math_Min_F),        asCALL_CDECL);
        engine->RegisterGlobalFunction("float max(float a, float b)",                 asFUNCTION(AS_Math_Max_F),        asCALL_CDECL);
        engine->RegisterGlobalFunction("int min(int a, int b)",                       asFUNCTION(AS_Math_Min_I),        asCALL_CDECL);
        engine->RegisterGlobalFunction("int max(int a, int b)",                       asFUNCTION(AS_Math_Max_I),        asCALL_CDECL);
        engine->RegisterGlobalFunction("float clamp(float v, float lo, float hi)",   asFUNCTION(AS_Math_Clamp),        asCALL_CDECL);
        engine->RegisterGlobalFunction("float lerp(float a, float b, float t)",      asFUNCTION(AS_Math_Lerp),         asCALL_CDECL);
        engine->RegisterGlobalFunction("float sin(float x)",                          asFUNCTION(AS_Math_Sin),          asCALL_CDECL);
        engine->RegisterGlobalFunction("float cos(float x)",                          asFUNCTION(AS_Math_Cos),          asCALL_CDECL);
        engine->RegisterGlobalFunction("float sqrt(float x)",                         asFUNCTION(AS_Math_Sqrt),         asCALL_CDECL);
        engine->RegisterGlobalFunction("float atan2(float y, float x)",               asFUNCTION(AS_Math_Atan2),        asCALL_CDECL);
        engine->RegisterGlobalFunction("float pow(float base, float exp)",            asFUNCTION(AS_Math_Pow),          asCALL_CDECL);
        engine->RegisterGlobalFunction("float floor(float x)",                        asFUNCTION(AS_Math_Floor),        asCALL_CDECL);
        engine->RegisterGlobalFunction("float ceil(float x)",                         asFUNCTION(AS_Math_Ceil),         asCALL_CDECL);
    }

    engine->SetDefaultNamespace("string");
    {
        engine->RegisterGlobalFunction("string to_string(int &in value)",   asFUNCTION(AS_String_To_String_Int),   asCALL_CDECL);
        engine->RegisterGlobalFunction("string to_string(float &in value)", asFUNCTION(AS_String_To_String_Float), asCALL_CDECL);
        engine->RegisterGlobalFunction("int to_int(string &in str)",        asFUNCTION(AS_Int_To_String),          asCALL_CDECL);
        engine->RegisterGlobalFunction("float to_float(string &in str)",    asFUNCTION(AS_Float_To_String),        asCALL_CDECL);
    }

    engine->SetDefaultNamespace("time");
    {
        engine->RegisterGlobalFunction("int get()",      asFUNCTION(AS_Get_Time_Current), asCALL_CDECL);
        engine->RegisterGlobalFunction("int getDelta()", asFUNCTION(AS_Get_Time_Delta),   asCALL_CDECL);
		engine->RegisterGlobalFunction("float getDeltaSeconds()", asFUNCTION(AS_Get_Time_Delta_Seconds), asCALL_CDECL);
    }

    engine->SetDefaultNamespace("");
}
