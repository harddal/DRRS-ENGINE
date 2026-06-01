#include "Game/Behavior/EntityBehavior.h"
#include <sstream>
#include <irrlicht.h>

std::string BehaviorProperty::toString() const
{
    switch (type)
    {
    case BehaviorPropType::INT:    return std::to_string(*static_cast<int*>(ptr));
    case BehaviorPropType::FLOAT:  return std::to_string(*static_cast<float*>(ptr));
    case BehaviorPropType::BOOL:   return *static_cast<bool*>(ptr) ? "1" : "0";
    case BehaviorPropType::STRING: return *static_cast<std::string*>(ptr);
    case BehaviorPropType::VECTOR3:
    {
        auto* v = static_cast<irr::core::vector3df*>(ptr);
        return std::to_string(v->X) + "," + std::to_string(v->Y) + "," + std::to_string(v->Z);
    }
    default: return "";
    }
}

void BehaviorProperty::applyFromString(const std::string& s)
{
    if (s.empty()) return;
    switch (type)
    {
    case BehaviorPropType::INT:    *static_cast<int*>(ptr)    = std::stoi(s); break;
    case BehaviorPropType::FLOAT:  *static_cast<float*>(ptr)  = std::stof(s); break;
    case BehaviorPropType::BOOL:   *static_cast<bool*>(ptr)   = (s == "1");   break;
    case BehaviorPropType::STRING: *static_cast<std::string*>(ptr) = s;       break;
    case BehaviorPropType::VECTOR3:
    {
        auto* v = static_cast<irr::core::vector3df*>(ptr);
        std::stringstream ss(s);
        std::string token;
        float vals[3] = { 0.f, 0.f, 0.f };
        int i = 0;
        while (std::getline(ss, token, ',') && i < 3)
            vals[i++] = std::stof(token);
        v->X = vals[0]; v->Y = vals[1]; v->Z = vals[2];
        break;
    }
    default: break;
    }
}
