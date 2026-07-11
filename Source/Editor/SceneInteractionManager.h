#pragma once

#include <algorithm>
#include <memory>
#include <cstdint>

#include <irrlicht.h>

#include "anax/Entity.hpp"

#include "Engine/World/Components/TransformComponent.h"
#include "Engine/Interface/ImTransformControl.h"
#include "Engine/Prop/PropManager.h"
#include "Editor/VegetationPainter.h"
#include "Editor/TexturePainter.h"

#define CLIPBOARD_ENTITY_FILENAME "temp/-cent"

extern unsigned int g_currentEntity;
extern unsigned int g_currentMesh;
extern unsigned int g_currentPrefab;
extern std::string g_currentScene;
extern std::string g_currentSelectedTexture;
extern unsigned int g_currentSelectedObjectType;

enum class SELECTED_OBJECT_TYPE
{
    NONE,
    ENTITY,
    MESH,
    PREFAB
};

// Which name-string field a LINK pick writes into (see completeLinkPick)
enum class LinkPickField
{
    NONE,
    LOGIC_RECEIVER,        // LogicComponent::receiver (comma list, append)
    ZONE_DETECT_ENTITY,    // TriggerZoneComponent::entity (single, replace)
    ZONE_TRIGGERED_ENTITY  // TriggerZoneComponent::triggered_entity (comma list, append)
};

enum class TRANSFORM_WIDGET_MODE
{
    TRANSLATE,
    ROTATE,
    SCALE,
    LOCAL,
    WORLD
};

struct EditorConfiguration
{
	bool
		drawPointLightBounds,
		useSnap;

	float snapX, snapY, snapZ;

	template <class Archive>
	void serialize(Archive& archive)
	{
		archive(
			CEREAL_NVP(drawPointLightBounds),
			CEREAL_NVP(useSnap),
			CEREAL_NVP(snapX),
			CEREAL_NVP(snapY),
			CEREAL_NVP(snapZ));
	}

	EditorConfiguration() :
		drawPointLightBounds(false),
		useSnap(false),
		snapX(0.25f),
		snapY(0.25f),
		snapZ(0.25f)
	{}
};

struct TransformSnapshot
{
	entityid id;
	irr::core::vector3df position, rotation, scale;
};
using UndoEntry = std::vector<TransformSnapshot>;

class SceneInteractionManager
{
public:
	void init();
	void update(float dt = 0.0f);
	void draw();
	void destroy();

	VegetationPainter& getPainter()       { return m_painter; }
    TexturePainter&    getTexturePainter() { return m_texturePainter; }

	void setSelectedEntity(unsigned int ent)
	{
        m_selectedEntities.clear();
        std::vector<entityid>().swap(m_selectedEntities);
	    m_selectedEntities.emplace_back(ent);
		g_currentEntity = ent;
	}

    void addAnotherSelectedEntity(unsigned int ent)
    {
        for (auto id : m_selectedEntities)
            if (id == ent) return;
        m_selectedEntities.emplace_back(ent);
    }

    void clearSelectedEntities()
    {
        m_selectedEntities.clear();
        std::vector<entityid>().swap(m_selectedEntities);
    }

	unsigned int getSelectedEntity()
	{
	    if (!m_selectedEntities.empty())
            return m_selectedEntities.at(0);

        return _entity_null_value;
	}

    const std::vector<entityid>& getSelectedEntities() const { return m_selectedEntities; }

    bool isEntityInSelection(entityid eid) const
    {
        for (auto id : m_selectedEntities)
            if (id == eid) return true;
        return false;
    }

    void removeFromSelection(entityid eid)
    {
        m_selectedEntities.erase(
            std::remove(m_selectedEntities.begin(), m_selectedEntities.end(), eid),
            m_selectedEntities.end());
        if (!m_selectedEntities.empty())
            g_currentEntity = m_selectedEntities.at(0);
        else
            g_currentEntity = _entity_null_value;
    }

    // One-shot link pick: next entity click (viewport or hierarchy) writes its
    // name into the host entity's field, then the mode clears itself.
    void beginLinkPick(entityid host, LinkPickField field)
    {
        m_linkPickHost = host;
        m_linkPickField = field;
    }

    void cancelLinkPick()
    {
        m_linkPickHost = _entity_null_value;
        m_linkPickField = LinkPickField::NONE;
    }

    bool isLinkPicking() const { return m_linkPickField != LinkPickField::NONE; }
    LinkPickField linkPickField() const { return m_linkPickField; }
    entityid linkPickHost() const { return m_linkPickHost; }

    void completeLinkPick(entityid pickedId);

    void deleteEntity();
    void cutEntity();
    void copyEntity();
    void pasteEntity();

    // Prop selection
    void setSelectedProp(uint32_t id)
    {
        m_selectedPropId = id;
        // Deselect any entities when a prop is selected
        clearSelectedEntities();
        g_currentEntity = _entity_null_value;
        g_currentSelectedObjectType = m_currentSelectedObject =
            static_cast<unsigned int>(SELECTED_OBJECT_TYPE::NONE);
    }
    void clearSelectedProp() { m_selectedPropId = UINT32_MAX; }
    uint32_t getSelectedProp() const { return m_selectedPropId; }

    void deleteProp();
    void cutProp();
    void copyProp();
    void pasteProp();

    bool isPropSelected()    const { return m_selectedPropId != UINT32_MAX; }
    bool hasPropClipboard()  const { return m_propClipboardValid; }

    void undoTransform();
    void redoTransform();

    void setTransformWidgetMode(TRANSFORM_WIDGET_MODE mode);

    void useSnap(bool snap)
    {
		m_configuration.useSnap = m_useSnap = snap;
		saveConfiguration(m_configuration);
    }
    bool isSnap()
    {
        return m_configuration.useSnap;
    }

    void setSnap(float snap)
    {
		m_configuration.snapX = snap;
		m_configuration.snapY = snap;
		m_configuration.snapZ = snap;
		saveConfiguration(m_configuration);
    }
    float getSnapUnit()
    {
        return m_snap[0];
    }

    ImTransformControl::OPERATION getWidgetToolMode() { return m_widgetType; }
    ImTransformControl::MODE getWidgetCoordMode() { return m_widgetCoordSet; }
    std::string getWidgetToolModeStr() { return m_selectedWidgetType; }
    std::string getWidgetCoordModeStr() { return m_selectedTransType; }

    bool isEntitySelected() {
        if (!m_selectedEntities.empty()) {
            return true;
        }

        return false;
    }
    bool multipleEntitiesSelected() { return m_selectedEntities.size() > 1; }

    unsigned int getCurrentSelectedObject()
    {
        return m_currentSelectedObject;
    }

	EditorConfiguration getConfiguration() const { return m_configuration; }
	void saveConfiguration(EditorConfiguration& configuration);

	void selectNewSpawnedEntityNextFrame() { m_selectNewSpawnedEntity = true; }

private:
    VegetationPainter m_painter;
    TexturePainter    m_texturePainter;
    std::vector<entityid> m_selectedEntities;
    uint32_t              m_selectedPropId = UINT32_MAX;

    LinkPickField m_linkPickField = LinkPickField::NONE;
    entityid      m_linkPickHost = _entity_null_value;

    static constexpr int k_maxUndoDepth = 50;
    bool m_gizmoWasUsing;
    irr::core::matrix4 m_cachedRotationMatrix;
    bool m_rotationMatrixCached;
    std::vector<UndoEntry> m_transformUndoStack;
    std::vector<UndoEntry> m_transformRedoStack;

    bool m_isWidgetDrawn, m_useSnap, m_selectNewSpawnedEntity;

    float m_snap[3];

    int m_clipboardSize = 0;

    bool      m_propClipboardValid = false;
    StaticProp m_propClipboard;

    entityid m_selectedEntity;

    std::string m_selectedWidgetType, m_selectedTransType;

    ImTransformControl::OPERATION m_widgetType;
    ImTransformControl::MODE m_widgetCoordSet;

    unsigned int m_currentSelectedObject;

	EditorConfiguration m_configuration;

};
