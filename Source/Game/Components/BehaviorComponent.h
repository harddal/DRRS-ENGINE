#pragma once
#include <memory>
#include <string>
#include <sstream>
#include <anax/Component.hpp>
#include <cereal/cereal.hpp>
#include <cereal/types/string.hpp>
#include "Game/Behavior/EntityBehavior.h"

struct BehaviorComponent : anax::Component
{
    std::string behaviorType;
    std::unique_ptr<EntityBehavior> behavior;

    template <class Archive>
    void save(Archive& archive) const
    {
        archive(CEREAL_NVP(behaviorType));

        std::string props;
        if (behavior)
        {
            for (auto& p : const_cast<EntityBehavior*>(behavior.get())->getProperties())
                props += p.name + "=" + p.toString() + "|";
            if (!props.empty()) props.pop_back();
        }
        else
        {
            props = m_serializedProps;
        }
        archive(cereal::make_nvp("properties", props));
    }

    template <class Archive>
    void load(Archive& archive)
    {
        archive(CEREAL_NVP(behaviorType));
        // Only suppress "node not found" — other exceptions must propagate so the
        // archive's node stack stays balanced (a swallowed post-startNode exception
        // leaves an orphaned node and corrupts all subsequent entity loads).
        try { archive(cereal::make_nvp("properties", m_serializedProps)); }
        catch (const cereal::Exception&) {}
    }

    void applyPropertiesToBehavior()
    {
        if (!behavior || m_serializedProps.empty()) return;
        for (auto& p : behavior->getProperties())
        {
            auto it = m_serializedProps.find(p.name + "=");
            if (it == std::string::npos) continue;
            auto valStart = it + p.name.size() + 1;
            auto valEnd   = m_serializedProps.find('|', valStart);
            p.applyFromString(m_serializedProps.substr(valStart,
                valEnd == std::string::npos ? std::string::npos : valEnd - valStart));
        }
    }

    void syncPropertyToMap(const BehaviorProperty& p)
    {
        // Rebuild the full serialized string so the change is persisted on next save
        if (!behavior) return;
        std::string props;
        for (auto& prop : behavior->getProperties())
            props += prop.name + "=" + prop.toString() + "|";
        if (!props.empty()) props.pop_back();
        m_serializedProps = props;
    }

    BehaviorComponent() {}

private:
    std::string m_serializedProps;
};
