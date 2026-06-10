#pragma once

#include <string>

#include "anax/Component.hpp"
#include "cereal/cereal.hpp"
#include "cereal/types/vector.hpp"
#include <angelscript.h>
#include <vector2d.h>
#include <vector3d.h>

#define _entity_script_init    "void init(int)"
#define _entity_script_update  "void update(int)"
#define _entity_script_destroy "void destroy(int)"

#define _entity_script_on_interaction    "void onInteraction(int)"
#define _entity_script_on_kill_event     "void onKill(int)"
#define _entity_script_on_use_event      "void onUse(int)"
#define _entity_script_on_logic_event    "void onLogicEvent(int)"
#define _entity_script_on_player_collide "void onPlayerCollide(int)"
#define _entity_script_npc_update        "void npcUpdate(int)"

enum class AS_DATA_TYPE
{
    INT,
    BOOL,
    FLOAT,
    STRING,
    VECTOR2,
    VECTOR3,
    NO_TYPE
};

struct ExposedScriptData
{
    unsigned int index;

    std::string data, declaration;

    AS_DATA_TYPE type;

    ExposedScriptData() : index(0), type(AS_DATA_TYPE::NO_TYPE) {}
    ExposedScriptData(unsigned int index, std::string declaration, AS_DATA_TYPE type, std::string data)
    {
        this->index = index;
        this->declaration = declaration;
        this->type = type;
        this->data = data;
    }
};

struct ScriptComponent : anax::Component
{
    std::string script;

    friend class ScriptManager;
    friend class ScriptSystem;
    friend class WorldManager;

    bool
        active,
        initialized,
    // DEPRECATED -- Check if script function is nullptr instead
        hasInit = false,
        hasUpdate = false,
        hasDestroy = false,
		hasOnInteraction = false,
		hasOnKillEventFunc = false,
        hasOnUseEventFunc = false,
        hasOnLogicEventActivate = false,
        hasOnPlayerCollide = false,
        hasNpcUpdate = false;

    std::string module;

    asIScriptFunction
		*initFunc,
        *updateFunc,
        *destroyFunc,
		*onPlayerInteractionFunc,
        *onKillEventFunc,
        *onUseEventFunc,
        *onLogicEventActivate,
        *onPlayerCollideFunc,
        *npcUpdateFunc;

    asIScriptContext* context;

    std::vector<std::pair<int, std::string>> globals;
    std::vector<std::pair<AS_DATA_TYPE, void*>> global_values;
    std::vector<int> exposed_global_indexes;

    std::vector<ExposedScriptData> script_data;

    template <class Archive>
    void save(Archive& archive) const
    {
        std::vector<std::string> data;

        for (auto i = 0U; i < globals.size(); i++) {
            std::string data_str;

            switch (global_values.at(i).first) {
            case AS_DATA_TYPE::INT:
            {
                auto *value = reinterpret_cast<int*>(global_values.at(i).second);
                data_str = std::to_string(*value);
                break;
            }
            case AS_DATA_TYPE::BOOL:
            {
                auto *value = reinterpret_cast<bool*>(global_values.at(i).second);
                data_str = std::to_string(*value);
                break;
            }
            case AS_DATA_TYPE::FLOAT:
            {
                auto *value = reinterpret_cast<float*>(global_values.at(i).second);
                data_str = std::to_string(*value);
                break;
            }
            case AS_DATA_TYPE::STRING:
            {
                auto *value = reinterpret_cast<std::string*>(global_values.at(i).second);
                data_str = *value;
                break;
            }
            case AS_DATA_TYPE::VECTOR2:
            {
                auto *value = reinterpret_cast<irr::core::vector2df*>(global_values.at(i).second);
                data_str = std::to_string(value->X) + "," + std::to_string(value->Y);
                break;
            }
            case AS_DATA_TYPE::VECTOR3:
            {
                auto *value = reinterpret_cast<irr::core::vector3df*>(global_values.at(i).second);
                data_str = std::to_string(value->X) + "," + std::to_string(value->Y) + "," + std::to_string(value->Z);
                break;
            }
            default:
                break;
            }

            // INDEX : TYPE : DATA
            data.emplace_back(
                std::to_string(globals.at(i).first) + ":" + std::to_string(static_cast<unsigned int>(global_values.at(i).first)) + ":" + data_str);
        }

        archive(CEREAL_NVP(script), CEREAL_NVP(data));
    }

    template <class Archive>
    void load(Archive& archive)
    {
        std::vector<std::string> data;
        archive(CEREAL_NVP(script), CEREAL_NVP(data));

        // Each entry is encoded as "INDEX:TYPE:DATA"
        for (const auto& entry : data)
        {
            size_t c1 = entry.find(':');
            size_t c2 = entry.find(':', c1 + 1);
            if (c1 == std::string::npos || c2 == std::string::npos) continue;
            script_data.emplace_back(ExposedScriptData(
                atoi(entry.substr(0, c1).c_str()),
                "",
                static_cast<AS_DATA_TYPE>(atoi(entry.substr(c1 + 1, c2 - c1 - 1).c_str())),
                entry.substr(c2 + 1)));
        }
    }

    ScriptComponent() : 
        active(false), initialized(false),
        initFunc(nullptr), updateFunc(nullptr), destroyFunc(nullptr), onPlayerInteractionFunc(nullptr), onKillEventFunc(nullptr), onUseEventFunc(nullptr), onLogicEventActivate(nullptr), onPlayerCollideFunc(nullptr), npcUpdateFunc(nullptr),
        context(nullptr) {}
};
