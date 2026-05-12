#pragma once

#include <array>
#include <string>
#include <unordered_map>

enum E_MANAGED_MATERIAL
{
    MAT_INVALID,
    MAT_EARTH,
    MAT_GRAVEL,
	MAT_WATER,
    MAT_STONE,
    MAT_METAL,
    MAT_GLASS,
    MAT_CARPET,
    MAT_WOOD
};

const std::array<std::string, 9> g_managedMaterialName =
{
    "invalid",
    "earth",
    "gravel",
    "water",
    "stone",
    "metal",
    "glass",
    "carpet",
    "wood"
};

class MaterialBuilder
{
public:
    MaterialBuilder() = default;

	void buildMaterialTable();

	std::string        getMaterialName(E_MANAGED_MATERIAL material) const;
	E_MANAGED_MATERIAL getMaterialFromTexture(const std::string& texture) const;
    std::string        getMaterialNameFromTexture(const std::string& texture) const;

private:
	std::unordered_map<std::string, E_MANAGED_MATERIAL> m_materialMap;
};
