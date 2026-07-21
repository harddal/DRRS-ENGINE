#include "WorldManager.h"

#include <string>
#include <fstream>
#include <unordered_set>

#include "Engine/Renderer/IrrAssimp/IrrAssimpImport.h"

#include <spdlog/spdlog.h>
#include <tinyxml2.h>
#include <cereal/archives/xml.hpp>

#include "Utility/Utility.h"

#include "Game/GameState.h"

using namespace anax;
using namespace cereal;
using namespace std;
using namespace tinyxml2;

// Load a named component node from the archive into an entity component.
// Only call this when the node is known to exist (from the TinyXML pre-scan).
template <class Archive, class Component>
static void loadComponent(Archive& archive, const char* name, anax::Entity& entity,
                          const std::string& entityName)
{
    spdlog::debug("  [{}] loading component: {}", entityName, name);
    archive.setNextName(name);
    archive.startNode();
    entity.addComponent<Component>();
    archive(entity.getComponent<Component>());
    archive.finishNode();
}

entityid WorldManager::deserializeEntity(const string& file, entityid id, bool use_saved_transform, const string& name,
                                         const irr::core::vector3df& position, const irr::core::vector3df& rotation,
                                         const irr::core::vector3df& scale)
{
    // --- Pass 1: cheap TinyXML scan to find which nodes/components are present ---
    // This avoids using exceptions for optional-component detection in Pass 2.
    struct EntityInfo { unordered_set<string> components; };
    vector<EntityInfo> entityInfos;
    bool hasScene  = false;
    bool hasPrefab = false;

    {
        XMLDocument xml;
        if (xml.LoadFile(file.c_str()) != XML_NO_ERROR)
        {
            spdlog::warn("Failed to load entity \'" + file + "\' in WorldManager::deserializeEntity()");
            return _entity_null_value;
        }

        auto* root = xml.FirstChild()->NextSibling(); // <cereal>
        for (auto* node = root->FirstChildElement(); node; node = node->NextSiblingElement())
        {
            string val = node->Value();
            if      (val == "scene")  { hasScene  = true; }
            else if (val == "prefab") { hasPrefab = true; }
            else if (val == "entity")
            {
                EntityInfo info;
                for (auto* comp = node->FirstChildElement(); comp; comp = comp->NextSiblingElement())
                    info.components.insert(comp->Value());
                entityInfos.push_back(move(info));
            }
        }
    }

    // --- Pass 2: Cereal data loading, only entering nodes we know exist ---
    ifstream stream(file);
    if (!stream.is_open())
    {
        spdlog::warn("Failed to open entity \'" + file + "\' in WorldManager::deserializeEntity()");
        return _entity_null_value;
    }

    Entity entity;

    try
    {
        XMLInputArchive archive(stream);

        if (hasScene)
        {
            archive.setNextName("scene");
            archive.startNode();
            archive(m_currentSceneDescriptor);
            archive.finishNode();
        }

        if (hasPrefab) // TODO: Placeholder (NOIMP)
        {
            archive.setNextName("prefab");
            archive.startNode();
            archive.finishNode();
        }

        spdlog::debug("Pre-scan '{}': {} entity/entities, hasScene={}", file, entityInfos.size(), hasScene);

        for (size_t entityIdx = 0; entityIdx < entityInfos.size(); ++entityIdx)
        {
            auto& info = entityInfos[entityIdx];
            auto has = [&](const char* n) { return info.components.count(n) > 0; };

            std::string entityName = file + "[" + std::to_string(entityIdx) + "]";
            bool entityActivated = false;
            bool entityNodeStarted = false;

            spdlog::debug("Deserializing entity {}/{} from '{}'", entityIdx + 1, entityInfos.size(), file);

            try
            {
            archive.setNextName("entity");
            archive.startNode();
            entityNodeStarted = true;

            entity = m_gameWorld.createEntity();

            // --- DescriptorComponent ---
            if (has("descriptor"))
            {
                loadComponent<XMLInputArchive, DescriptorComponent>(archive, "descriptor", entity, entityName);

                auto& desc = entity.getComponent<DescriptorComponent>();
                entityName = desc.name; // use real name for subsequent log messages
                if (id < _entity_null_value)
                {
                    desc.id = id;
                }
                else
                {
                    bool flag = true;
                    for (auto i = 0U; i < _entity_null_value; i++)
                    {
                        if (!m_entityIDArray[i])
                        {
                            desc.id = i;
                            m_entityIDArray[i] = true;
                            flag = false;
                            break;
                        }
                    }
                    if (flag)
                        spdlog::warn("World reports entity count exceeded entity_null_value " +
                                     std::to_string(_entity_null_value));
                }

                if (!name.empty())
                    desc.name = name;
            }

            // --- TransformComponent ---
            if (has("transform"))
            {
                loadComponent<XMLInputArchive, TransformComponent>(archive, "transform", entity, entityName);

                if (!use_saved_transform)
                {
                    auto& tc = entity.getComponent<TransformComponent>();
                    tc.setPosition(position);
                    tc.setRotation(rotation);
                    tc.setScale(scale);
                    tc.initialPosition = position;
                    tc.initialRotation = rotation;
                    tc.initialScale    = scale;
                }
            }

            if (has("billboard"))
                loadComponent<XMLInputArchive, BillboardSpriteComponent>(archive, "billboard", entity, entityName);

            if (has("camera"))
                loadComponent<XMLInputArchive, CameraComponent>(archive, "camera", entity, entityName);

            if (has("cct"))
                loadComponent<XMLInputArchive, CCTComponent>(archive, "cct", entity, entityName);

            if (has("debugmesh"))
                loadComponent<XMLInputArchive, DebugMeshComponent>(archive, "debugmesh", entity, entityName);

            if (has("debugsprite"))
                loadComponent<XMLInputArchive, DebugSpriteComponent>(archive, "debugsprite", entity, entityName);

            if (has("light"))
                loadComponent<XMLInputArchive, LightComponent>(archive, "light", entity, entityName);

            // --- MeshComponent (with .anim sidecar loading) ---
            if (has("mesh"))
            {
                loadComponent<XMLInputArchive, MeshComponent>(archive, "mesh", entity, entityName);

                auto& mc = entity.getComponent<MeshComponent>();
                if (mc.isAnimated)
                {
                    XMLDocument anim_xml;
                    const string animFilePath =
                        mc.mesh.substr(0, mc.mesh.find_last_of('.') + 1) + "anim";

                    if (anim_xml.LoadFile(animFilePath.c_str()) != XML_NO_ERROR)
                    {
                        generateAnimFile(mc.mesh);
                        anim_xml.LoadFile(animFilePath.c_str());
                    }

                    if (anim_xml.FirstChild())
                    {
                        auto* anim_root  = anim_xml.FirstChild()->NextSibling();
                        auto* anim_value = anim_root->FirstChildElement();
                        for (; anim_value; anim_value = anim_value->NextSiblingElement())
                        {
                            if (string(anim_value->Name()) == "fps")
                            {
                                mc.fps = static_cast<unsigned int>(atoi(anim_value->GetText()));
                            }
                            else if (string(anim_value->Name()) == "animationList")
                            {
                                for (auto* sub = anim_value->FirstChildElement(); sub; sub = sub->NextSiblingElement())
                                {
                                    string frames   = sub->GetText();
                                    string animname = sub->Attribute("name");
                                    auto   loop     = Utility::ProcessBoolStatement(string(sub->Attribute("loop")));
                                    mc.animationList.push_back(sAnimationData(
                                        animname,
                                        stoi(frames.substr(0, frames.find_first_of(','))),
                                        stoi(frames.substr(frames.find_first_of(',') + 1)),
                                        loop));
                                }
                            }
                        }
                        anim_xml.Clear();
                    }
                }
            }

            if (has("navagent"))
                loadComponent<XMLInputArchive, NavAgentComponent>(archive, "navagent", entity, entityName);

            if (has("tween"))
                loadComponent<XMLInputArchive, TweenComponent>(archive, "tween", entity, entityName);

            if (has("physics"))
                loadComponent<XMLInputArchive, PhysicsComponent>(archive, "physics", entity, entityName);

            if (has("render"))
                loadComponent<XMLInputArchive, RenderComponent>(archive, "render", entity, entityName);

            if (has("script"))
                loadComponent<XMLInputArchive, ScriptComponent>(archive, "script", entity, entityName);

            if (has("soundlistener"))
                loadComponent<XMLInputArchive, SoundListenerComponent>(archive, "soundlistener", entity, entityName);

            if (has("sound"))
                loadComponent<XMLInputArchive, SoundComponent>(archive, "sound", entity, entityName);

            if (has("particle"))
                loadComponent<XMLInputArchive, ParticleComponent>(archive, "particle", entity, entityName);

            if (has("skybox"))
                loadComponent<XMLInputArchive, SkyboxComponent>(archive, "skybox", entity, entityName);

            // --- Game-specific components ---
            GameState::deserializeComponent(entity, archive, info.components, entityName);

            archive.finishNode(); // </entity>

            spdlog::debug("  [{}] done", entityName);
            entityActivated = true;
            entity.activate();
            m_gameWorld.refresh();

            } // end per-entity try
            catch (const cereal::Exception& e)
            {
                spdlog::warn("Cereal exception on entity {}/{} ('{}') in '{}': {}",
                    entityIdx + 1, entityInfos.size(), entityName, file, e.what());
                if (!entityActivated && entityNodeStarted)
                    try { archive.finishNode(); } catch (...) {}
            }
            catch (const std::exception& e)
            {
                spdlog::warn("Exception on entity {}/{} ('{}') in '{}': {}",
                    entityIdx + 1, entityInfos.size(), entityName, file, e.what());
                if (!entityActivated && entityNodeStarted)
                    try { archive.finishNode(); } catch (...) {}
            }

            if (!entityActivated)
            {
                spdlog::error("Entity {}/{} ('{}') in '{}' was not activated — components present: [{}]",
                    entityIdx + 1, entityInfos.size(), entityName, file, [&](){
                        std::string s;
                        for (auto& c : info.components) s += c + " ";
                        return s;
                    }());
            }
        }
    }
    catch (const cereal::Exception& e)
    {
        spdlog::error("Cereal exception in archive for '{}' (outside entity loop, check scene/prefab nodes): {}", file, e.what());
    }
    catch (const std::exception& e)
    {
        spdlog::error("Exception in archive for '{}' (outside entity loop): {}", file, e.what());
    }

    if (entity.isValid())
    {
        if (entity.hasComponent<DescriptorComponent>())
            return entity.getComponent<DescriptorComponent>().id;

        spdlog::warn("'{}': last entity has no descriptor component", file);
        return _entity_null_value;
    }

    // No entity was ever assigned — likely entityInfos was empty or an exception hit before createEntity()
    spdlog::warn("'{}': no valid entity after load (pre-scan found {} entity/entities, hasScene={})",
                 file, entityInfos.size(), hasScene);
    return _entity_null_value;
}
