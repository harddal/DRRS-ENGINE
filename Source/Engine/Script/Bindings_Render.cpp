#include "Bindings.h"

#include "Engine/Engine.h"

#include "ScriptExceptions.h"

// CAMERA
irr::core::vector3df AS_Get_Camera_Position()
{
    return RenderManager::Get()->sceneManager()->getActiveCamera()->getAbsoluteTransformation().getTranslation();
}
irr::core::vector3df AS_Get_Camera_Rotation()
{
	return RenderManager::Get()->sceneManager()->getActiveCamera()->getRotation();
}
irr::core::vector3df AS_Get_Camera_Look_Dir()
{
    return Math::GetDirectionVector(RenderManager::Get()->sceneManager()->getActiveCamera()->getRotation(), false);
}
irr::core::vector3df AS_Get_Camera_Look_Dir_Norm()
{
	return Math::GetDirectionVector(RenderManager::Get()->sceneManager()->getActiveCamera()->getRotation(), true);
}

// ANIMATION
void AS_SetFrame_id(entityid id, float frame)
{
	WorldManager::Get()->renderSystem()->setFrame(id, frame);
}
void AS_SetFrameLoop_id(entityid id, float begin, float end)
{
	WorldManager::Get()->renderSystem()->setFrameLoop(id, begin, end);
}
void AS_PlayAnimation(entityid id, std::string animation)
{
	WorldManager::Get()->renderSystem()->playAnimation(id, animation);
}
void AS_LoopAnimation(entityid id, std::string animation)
{
	WorldManager::Get()->renderSystem()->loopAnimation(id, animation);
}

bool AS_IsAnimating(entityid e)
{
	auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);

	if (!entity.isValid())
	{
		return false;
	}

	if (entity.hasComponent<MeshComponent>()) 
	{
		if (entity.getComponent<MeshComponent>().animation_call_back) 
		{
			return entity.getComponent<MeshComponent>().animation_call_back->hasAnimationEnded();
		}

		return false;
	}

	return false;
}

static std::string AS_LastPlayedAnim(entityid e)
{
	auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);

	if (!entity.isValid())
	{
		return std::string();
	}

	if (entity.hasComponent<MeshComponent>()) 
	{
		return entity.getComponent<MeshComponent>().lastPlayedAnimation.name;
	}

	return std::string();
}

// RENDER
void AS_SetEntityRenderVisible(std::string e, bool b)
{
	auto& entity = WorldManager::Get()->managerSystem()->getEntityByName(e);
	
	if (!entity.isValid())
	{
		return;
	}

	if (entity.hasComponent<RenderComponent>())
	{
		WorldManager::Get()->managerSystem()->getEntityByName(e).getComponent<RenderComponent>().isVisible = b;
	}
}

void AS_SetEntityRenderVisibleID(entityid e, bool b)
{
	auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);

	if (!entity.isValid())
	{
		return;
	}
	
	if (entity.hasComponent<RenderComponent>())
	{
		WorldManager::Get()->managerSystem()->getEntityByID(e).getComponent<RenderComponent>().isVisible = b;
	}
}

bool AS_GetEntityRenderVisibleID(entityid e)
{
	auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);

	if (!entity.isValid())
	{
		return false;
	}
	
	if (entity.hasComponent<RenderComponent>())
	{
		return WorldManager::Get()->managerSystem()->getEntityByID(e).getComponent<RenderComponent>().isVisible;
	}

	return false;
}

irr::core::vector3df AS_GetBoundingBoxExtent(entityid e)
{
	auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);

	if (!entity.isValid())
	{
		return irr::core::vector3df();
	}
	
	if (entity.hasComponent<MeshComponent>()) 
	{
		return entity.getComponent<MeshComponent>().node->getTransformedBoundingBox().getExtent();
	}

	return irr::core::vector3df();
}

void AS_SwapTexture(entityid e, int tid)
{
	WorldManager::Get()->renderSystem()->swapTexture(e, tid);
}

void AS_SetVisibleLightComp(entityid e, bool b)
{
	auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);

	if (!entity.isValid())
	{
		return;
	}

	if (entity.hasComponent<LightComponent>()) 
	{
		entity.getComponent<LightComponent>().visible = b;
	}
}

bool AS_IsVisibleLightComp(entityid e)
{
	auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(e);

	if (!entity.isValid())
	{
		return false;
	}

	if (entity.hasComponent<LightComponent>())
	{
		return entity.getComponent<LightComponent>().visible;
	}

	return false;
}

// RAYCAST
RaycastResultData g_raycastData;
irr::core::vector3df g_raycastStartPoint;

bool AS_Raycast(irr::core::vector3df start, irr::core::vector3df end)
{
	g_raycastData = RenderManager::Get()->raycastWorldPosition(start, end, true);
	g_raycastStartPoint = start;
	return g_raycastData.hit;
}

bool AS_Raycast_Camera()
{
	if (RenderManager::Get()->sceneManager()->getActiveCamera())
	{
		g_raycastData = RenderManager::Get()->raycastWorldPosition(
			RenderManager::Get()->sceneManager()->getActiveCamera()->getPosition(), RenderManager::Get()->sceneManager()->getActiveCamera()->getTarget(), true);
		
		g_raycastStartPoint = RenderManager::Get()->sceneManager()->getActiveCamera()->getPosition();
		
		return g_raycastData.hit;
	}

	return false;
}

bool AS_Raycast_Camera_Range(float range = 0.0f)
{
	if (RenderManager::Get()->sceneManager()->getActiveCamera())
	{
		g_raycastData = RenderManager::Get()->raycastWorldPosition(
			RenderManager::Get()->sceneManager()->getActiveCamera()->getPosition(), RenderManager::Get()->sceneManager()->getActiveCamera()->getTarget(), true);

		g_raycastStartPoint = RenderManager::Get()->sceneManager()->getActiveCamera()->getPosition();

		return g_raycastData.hit;
	}

	return false;
}

bool AS_Raycast_Last_Hit()
{
	return g_raycastData.hit;
}

irr::core::vector3df AS_Raycast_Last_Point()
{
	return g_raycastData.point;
}

int AS_Raycast_Last_EID()
{
	if (g_raycastData.node) {
		return g_raycastData.node->getID();
	}

	return 0xFFFF;
}

float AS_Raycast_Last_Length()
{
	return hypot(
		hypot(
			g_raycastStartPoint.X - g_raycastData.point.X,
			g_raycastStartPoint.Y - g_raycastData.point.Y),
		g_raycastStartPoint.Z - g_raycastData.point.Z);
}

static std::string as_getMaterial(irr::core::vector3df s, irr::core::vector3df e)
{
	return Engine::Get()->getMaterialBuilder().getMaterialName(Engine::Get()->getMaterialBuilder().getMaterialFromTexture(RenderManager::Get()->getMeshMaterialFromRay(s, e)));
}

// POST-PROCESS — per-frame accumulators for multi-source radiation/filmgrain
static float s_filmGrainAccum = 0.0f;
static float s_radiationAccum = 0.0f;

void ScriptBindings::ResetRenderAccumulators()
{
    s_filmGrainAccum = 0.0f;
    s_radiationAccum = 0.0f;
    auto* rm = RenderManager::Get();
    rm->filmGrainCallback()->strength  = 0.0f;
    rm->radiationCallback()->intensity = 0.0f;
    rm->setFilmGrainEnabled(false);
    rm->setRadiationEnabled(false);
}

static void AS_ContributeFilmGrainStrength(float v)
{
#undef max
    s_filmGrainAccum = std::max(s_filmGrainAccum, v);
    auto* rm = RenderManager::Get();
    rm->filmGrainCallback()->strength = s_filmGrainAccum;
    if (s_filmGrainAccum > 0.0f)
        rm->setFilmGrainEnabled(true);
}

static void AS_ContributeRadiationIntensity(float v)
{
#undef max
    s_radiationAccum = std::max(s_radiationAccum, v);
    auto* rm = RenderManager::Get();
    rm->radiationCallback()->intensity = s_radiationAccum;
    if (s_radiationAccum > 0.0f)
        rm->setRadiationEnabled(true);
}

static void  AS_SetBloomEnabled(bool b)       { RenderManager::Get()->setBloomEnabled(b); }
static void  AS_SetBloomThreshold(float v)    { RenderManager::Get()->bloomBrightCallback()->threshold = v; }
static void  AS_SetBloomStrength(float v)     { RenderManager::Get()->bloomCompositeCallback()->strength = v; }
static void  AS_SetTonemapEnabled(bool b)     { RenderManager::Get()->setTonemapEnabled(b); }
static void  AS_SetTonemapExposure(float v)   { RenderManager::Get()->tonemapCallback()->exposure = v; }
static void  AS_SetTonemapWhitePoint(float v) { RenderManager::Get()->tonemapCallback()->whitePoint = v; }
static void  AS_SetSharpenEnabled(bool b)     { RenderManager::Get()->setSharpenEnabled(b); }
static void  AS_SetSharpenStrength(float v)   { RenderManager::Get()->sharpenCallback()->strength = v; }
static void  AS_SetAutoExposure(bool b)       { RenderManager::Get()->setAutoExposure(b); }
static void  AS_SetPixelateEnabled(bool b)    { RenderManager::Get()->setPixelateEnabled(b); }
static void  AS_SetPixelateSize(float v)      { RenderManager::Get()->pixelateCallback()->pixelSize = v; }
static void  AS_SetFilmGrainEnabled(bool b)   { RenderManager::Get()->setFilmGrainEnabled(b); }
static void  AS_SetFilmGrainStrength(float v) { RenderManager::Get()->filmGrainCallback()->strength = v; }
static void  AS_SetRadiationEnabled(bool b)   { RenderManager::Get()->setRadiationEnabled(b); }
static void  AS_SetRadiationIntensity(float v){ RenderManager::Get()->radiationCallback()->intensity = v; }

static bool  AS_GetBloomEnabled()       { return RenderManager::Get()->getPostProcessPassEnabled("bloom_bright"); }
static float AS_GetBloomThreshold()     { return RenderManager::Get()->bloomBrightCallback()->threshold; }
static float AS_GetBloomStrength()      { return RenderManager::Get()->bloomCompositeCallback()->strength; }
static bool  AS_GetTonemapEnabled()     { return RenderManager::Get()->getPostProcessPassEnabled("tonemap"); }
static float AS_GetTonemapExposure()    { return RenderManager::Get()->tonemapCallback()->exposure; }
static float AS_GetTonemapWhitePoint()  { return RenderManager::Get()->tonemapCallback()->whitePoint; }
static bool  AS_GetSharpenEnabled()     { return RenderManager::Get()->getPostProcessPassEnabled("sharpen"); }
static float AS_GetSharpenStrength()    { return RenderManager::Get()->sharpenCallback()->strength; }
static bool  AS_GetAutoExposure()       { return RenderManager::Get()->getAutoExposure(); }
static bool  AS_GetPixelateEnabled()    { return RenderManager::Get()->getPostProcessPassEnabled("pixelate"); }
static float AS_GetPixelateSize()       { return RenderManager::Get()->pixelateCallback()->pixelSize; }
static bool  AS_GetFilmGrainEnabled()   { return RenderManager::Get()->getPostProcessPassEnabled("filmgrain"); }
static float AS_GetFilmGrainStrength()  { return RenderManager::Get()->filmGrainCallback()->strength; }
static bool  AS_GetRadiationEnabled()   { return RenderManager::Get()->getPostProcessPassEnabled("radiation"); }
static float AS_GetRadiationIntensity() { return RenderManager::Get()->radiationCallback()->intensity; }

struct PostProcessSnapshot
{
    bool  bloomEnabled;    float bloomThreshold; float bloomStrength;
    bool  tonemapEnabled;  float tonemapExposure; float tonemapWhitePoint;
    bool  sharpenEnabled;  float sharpenStrength;
    bool  autoExposure;
    bool  pixelateEnabled;  float pixelateSize;
    bool  filmGrainEnabled; float filmGrainStrength;
    bool  radiationEnabled; float radiationIntensity;
};
static PostProcessSnapshot g_postProcessDefaults;

static void AS_SavePostProcessDefaults()
{
    auto* rm = RenderManager::Get();
    g_postProcessDefaults = {
        rm->getPostProcessPassEnabled("bloom_bright"), rm->bloomBrightCallback()->threshold,    rm->bloomCompositeCallback()->strength,
        rm->getPostProcessPassEnabled("tonemap"),      rm->tonemapCallback()->exposure,          rm->tonemapCallback()->whitePoint,
        rm->getPostProcessPassEnabled("sharpen"),      rm->sharpenCallback()->strength,
        rm->getAutoExposure(),
        rm->getPostProcessPassEnabled("pixelate"),     rm->pixelateCallback()->pixelSize,
        rm->getPostProcessPassEnabled("filmgrain"),    rm->filmGrainCallback()->strength,
        rm->getPostProcessPassEnabled("radiation"),    rm->radiationCallback()->intensity
    };
}

static void AS_RestorePostProcessDefaults()
{
    auto* rm = RenderManager::Get();
    const auto& d = g_postProcessDefaults;
    rm->setBloomEnabled(d.bloomEnabled);       rm->bloomBrightCallback()->threshold    = d.bloomThreshold;
                                               rm->bloomCompositeCallback()->strength  = d.bloomStrength;
    rm->setTonemapEnabled(d.tonemapEnabled);   rm->tonemapCallback()->exposure         = d.tonemapExposure;
                                               rm->tonemapCallback()->whitePoint       = d.tonemapWhitePoint;
    rm->setSharpenEnabled(d.sharpenEnabled);   rm->sharpenCallback()->strength         = d.sharpenStrength;
    rm->setAutoExposure(d.autoExposure);
    rm->setPixelateEnabled(d.pixelateEnabled);   rm->pixelateCallback()->pixelSize        = d.pixelateSize;
    rm->setFilmGrainEnabled(d.filmGrainEnabled); rm->filmGrainCallback()->strength       = d.filmGrainStrength;
    rm->setRadiationEnabled(d.radiationEnabled); rm->radiationCallback()->intensity      = d.radiationIntensity;
}

void ScriptBindings::RegisterRender(asIScriptEngine* engine)
{
    engine->SetDefaultNamespace("render");
    {
        engine->RegisterGlobalFunction("vector3d getCameraPosition()", asFUNCTION(AS_Get_Camera_Position), asCALL_CDECL);
		engine->RegisterGlobalFunction("vector3d getCameraRotation()", asFUNCTION(AS_Get_Camera_Rotation), asCALL_CDECL);
        engine->RegisterGlobalFunction("vector3d getCameraLookDir()", asFUNCTION(AS_Get_Camera_Look_Dir), asCALL_CDECL);
        engine->RegisterGlobalFunction("vector3d getCameraLookDirNormalized()", asFUNCTION(AS_Get_Camera_Look_Dir_Norm), asCALL_CDECL);

		engine->RegisterGlobalFunction("void visible(string name, bool visible)", asFUNCTION(AS_SetEntityRenderVisible), asCALL_CDECL);
		engine->RegisterGlobalFunction("void visible(int entityId, bool visible)", asFUNCTION(AS_SetEntityRenderVisibleID), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool visible(int entityId)", asFUNCTION(AS_GetEntityRenderVisibleID), asCALL_CDECL);

		engine->RegisterGlobalFunction("vector3d getBoundingBox(int entityId)", asFUNCTION(AS_GetBoundingBoxExtent), asCALL_CDECL);

		engine->RegisterGlobalFunction("void swapTexture(int entityId, int textureId)", asFUNCTION(AS_SwapTexture), asCALL_CDECL);

		engine->RegisterGlobalFunction("void setLightComponentVisible(int entityId, bool visible)", asFUNCTION(AS_SetVisibleLightComp), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool isLightComponentVisible(int entityId)", asFUNCTION(AS_IsVisibleLightComp), asCALL_CDECL);

		engine->RegisterGlobalFunction("string getMaterial(vector3d start, vector3d end)", asFUNCTION(as_getMaterial), asCALL_CDECL);
    }

	engine->SetDefaultNamespace("animation");
	{
		engine->RegisterGlobalFunction("void set(int entityId, float frame)", asFUNCTION(AS_SetFrame_id), asCALL_CDECL);
		engine->RegisterGlobalFunction("void play(int entityId, string animation)", asFUNCTION(AS_PlayAnimation), asCALL_CDECL);
		engine->RegisterGlobalFunction("void loop(int entityId, float begin, float end)", asFUNCTION(AS_SetFrameLoop_id), asCALL_CDECL);
		engine->RegisterGlobalFunction("void loop(int entityId, string animation)", asFUNCTION(AS_LoopAnimation), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool ended(int entityId)", asFUNCTION(AS_IsAnimating), asCALL_CDECL);
		engine->RegisterGlobalFunction("string last(int entityId)", asFUNCTION(AS_LastPlayedAnim), asCALL_CDECL);
	}

	engine->SetDefaultNamespace("raycast");
	{
		engine->RegisterGlobalFunction("bool line(vector3d start, vector3d end)", asFUNCTION(AS_Raycast), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool camera()", asFUNCTION(AS_Raycast_Camera), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool camera(float range)", asFUNCTION(AS_Raycast_Camera_Range), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool test()", asFUNCTION(AS_Raycast_Last_Hit), asCALL_CDECL);
		engine->RegisterGlobalFunction("vector3d position()", asFUNCTION(AS_Raycast_Last_Point), asCALL_CDECL);
		engine->RegisterGlobalFunction("int id()", asFUNCTION(AS_Raycast_Last_EID), asCALL_CDECL);
		engine->RegisterGlobalFunction("float length()", asFUNCTION(AS_Raycast_Last_Length), asCALL_CDECL);
	}

	engine->SetDefaultNamespace("postprocess");
	{
		engine->RegisterGlobalFunction("void setBloomEnabled(bool enabled)",      asFUNCTION(AS_SetBloomEnabled),      asCALL_CDECL);
		engine->RegisterGlobalFunction("void setBloomThreshold(float value)",     asFUNCTION(AS_SetBloomThreshold),    asCALL_CDECL);
		engine->RegisterGlobalFunction("void setBloomStrength(float value)",      asFUNCTION(AS_SetBloomStrength),     asCALL_CDECL);
		engine->RegisterGlobalFunction("void setTonemapEnabled(bool enabled)",    asFUNCTION(AS_SetTonemapEnabled),    asCALL_CDECL);
		engine->RegisterGlobalFunction("void setTonemapExposure(float value)",    asFUNCTION(AS_SetTonemapExposure),   asCALL_CDECL);
		engine->RegisterGlobalFunction("void setTonemapWhitePoint(float value)",  asFUNCTION(AS_SetTonemapWhitePoint), asCALL_CDECL);
		engine->RegisterGlobalFunction("void setSharpenEnabled(bool enabled)",    asFUNCTION(AS_SetSharpenEnabled),    asCALL_CDECL);
		engine->RegisterGlobalFunction("void setSharpenStrength(float value)",    asFUNCTION(AS_SetSharpenStrength),   asCALL_CDECL);
		engine->RegisterGlobalFunction("void setAutoExposure(bool enabled)",      asFUNCTION(AS_SetAutoExposure),      asCALL_CDECL);
		engine->RegisterGlobalFunction("void setPixelateEnabled(bool enabled)",    asFUNCTION(AS_SetPixelateEnabled),    asCALL_CDECL);
		engine->RegisterGlobalFunction("void setPixelateSize(float value)",        asFUNCTION(AS_SetPixelateSize),       asCALL_CDECL);
		engine->RegisterGlobalFunction("void setFilmGrainEnabled(bool enabled)",    asFUNCTION(AS_SetFilmGrainEnabled),   asCALL_CDECL);
		engine->RegisterGlobalFunction("void setFilmGrainStrength(float value)",   asFUNCTION(AS_SetFilmGrainStrength),  asCALL_CDECL);
		engine->RegisterGlobalFunction("void setRadiationEnabled(bool enabled)",   asFUNCTION(AS_SetRadiationEnabled),   asCALL_CDECL);
		engine->RegisterGlobalFunction("void setRadiationIntensity(float value)",  asFUNCTION(AS_SetRadiationIntensity), asCALL_CDECL);

		engine->RegisterGlobalFunction("void contributeFilmGrainStrength(float value)",  asFUNCTION(AS_ContributeFilmGrainStrength),  asCALL_CDECL);
		engine->RegisterGlobalFunction("void contributeRadiationIntensity(float value)", asFUNCTION(AS_ContributeRadiationIntensity), asCALL_CDECL);

		engine->RegisterGlobalFunction("bool  getBloomEnabled()",      asFUNCTION(AS_GetBloomEnabled),      asCALL_CDECL);
		engine->RegisterGlobalFunction("float getBloomThreshold()",    asFUNCTION(AS_GetBloomThreshold),    asCALL_CDECL);
		engine->RegisterGlobalFunction("float getBloomStrength()",     asFUNCTION(AS_GetBloomStrength),     asCALL_CDECL);
		engine->RegisterGlobalFunction("bool  getTonemapEnabled()",    asFUNCTION(AS_GetTonemapEnabled),    asCALL_CDECL);
		engine->RegisterGlobalFunction("float getTonemapExposure()",   asFUNCTION(AS_GetTonemapExposure),   asCALL_CDECL);
		engine->RegisterGlobalFunction("float getTonemapWhitePoint()", asFUNCTION(AS_GetTonemapWhitePoint), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool  getSharpenEnabled()",    asFUNCTION(AS_GetSharpenEnabled),    asCALL_CDECL);
		engine->RegisterGlobalFunction("float getSharpenStrength()",   asFUNCTION(AS_GetSharpenStrength),   asCALL_CDECL);
		engine->RegisterGlobalFunction("bool  getAutoExposure()",      asFUNCTION(AS_GetAutoExposure),      asCALL_CDECL);
		engine->RegisterGlobalFunction("bool  getPixelateEnabled()",    asFUNCTION(AS_GetPixelateEnabled),    asCALL_CDECL);
		engine->RegisterGlobalFunction("float getPixelateSize()",       asFUNCTION(AS_GetPixelateSize),       asCALL_CDECL);
		engine->RegisterGlobalFunction("bool  getFilmGrainEnabled()",    asFUNCTION(AS_GetFilmGrainEnabled),   asCALL_CDECL);
		engine->RegisterGlobalFunction("float getFilmGrainStrength()",  asFUNCTION(AS_GetFilmGrainStrength),  asCALL_CDECL);
		engine->RegisterGlobalFunction("bool  getRadiationEnabled()",   asFUNCTION(AS_GetRadiationEnabled),   asCALL_CDECL);
		engine->RegisterGlobalFunction("float getRadiationIntensity()", asFUNCTION(AS_GetRadiationIntensity), asCALL_CDECL);

		engine->RegisterGlobalFunction("void saveDefaults()",    asFUNCTION(AS_SavePostProcessDefaults),    asCALL_CDECL);
		engine->RegisterGlobalFunction("void restoreDefaults()", asFUNCTION(AS_RestorePostProcessDefaults), asCALL_CDECL);
	}

    engine->SetDefaultNamespace("");
}