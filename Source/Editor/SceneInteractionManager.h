#pragma once

#include <algorithm>
#include <memory>
#include <cstdint>

#include <irrlicht.h>

#include "anax/Entity.hpp"

#include "Engine/World/Components/TransformComponent.h"
#include "Engine/Interface/ImTransformControl.h"
#include "Engine/Prop/PropManager.h"
#include "Engine/Brush/BrushData.h"
#include "Editor/VegetationPainter.h"
#include "Editor/TexturePainter.h"
#include "Editor/BrushTool.h"

#define CLIPBOARD_ENTITY_FILENAME "temp/-cent"

extern unsigned int g_currentEntity;
extern unsigned int g_currentMesh;
extern unsigned int g_currentPrefab;
extern std::string g_currentScene;
extern std::string g_currentSelectedTexture;  // full "content/texture/....ext" path chosen in the texture browser, or "null"
extern std::string g_textureBrowserRequestID; // identifies which call site is awaiting a browser result, "" when none pending
extern unsigned int g_currentSelectedObjectType;

enum class SELECTED_OBJECT_TYPE
{
    NONE,
    ENTITY,
    MESH,
    PREFAB
};

// Which clipboard the most recent cut/copy filled.  Ctrl+V dispatches on this
// ("last copied wins") instead of a fixed prop>entity priority, so a stale
// clipboard of one kind can never shadow a fresher copy of another.
enum class ClipboardKind
{
    NONE,
    ENTITY,
    PROP,
    BRUSH
};

// Which name-string field a LINK pick writes into (see completeLinkPick)
enum class LinkPickField
{
    NONE,
    LOGIC_RECEIVER,        // LogicComponent::receiver (comma list, append)
    ZONE_DETECT_ENTITY,    // TriggerZoneComponent::entity (single, replace)
    ZONE_TRIGGERED_ENTITY, // TriggerZoneComponent::triggered_entity (comma list, append)
    BRUSH_RECEIVER         // Brush::receiver (comma list, append; host is a brush id)
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

// Pre-operation state of one brush.  existed == false means the brush was
// created by the operation, so undoing it deletes the brush.
struct BrushSnapshot
{
	uint32_t id = 0;
	bool     existed = true;
	Brush    data;
};

// One undo step.  A single user action may snapshot entity transforms and
// brush states together (single Ctrl+Z timeline — no parallel stacks).
struct UndoEntry
{
	std::vector<TransformSnapshot> transforms;
	std::vector<BrushSnapshot>     brushes;

	bool empty() const { return transforms.empty() && brushes.empty(); }
};

class SceneInteractionManager
{
public:
	void init();
	void update(float dt = 0.0f);
	void draw();
	void destroy();

	VegetationPainter& getPainter()       { return m_painter; }
    TexturePainter&    getTexturePainter() { return m_texturePainter; }
    BrushTool&         getBrushTool()      { return m_brushTool; }

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
        m_linkPickBrushId = 0;
        m_linkPickField = field;
    }

    // Brush-hosted variant: brush ids are a separate namespace from entity ids
    void beginBrushLinkPick(uint32_t brushId)
    {
        m_linkPickHost = _entity_null_value;
        m_linkPickBrushId = brushId;
        m_linkPickField = LinkPickField::BRUSH_RECEIVER;
    }

    void cancelLinkPick()
    {
        m_linkPickHost = _entity_null_value;
        m_linkPickBrushId = 0;
        m_linkPickField = LinkPickField::NONE;
    }

    bool isLinkPicking() const { return m_linkPickField != LinkPickField::NONE; }
    LinkPickField linkPickField() const { return m_linkPickField; }
    entityid linkPickHost() const { return m_linkPickHost; }
    uint32_t linkPickBrushId() const { return m_linkPickBrushId; }

    void completeLinkPick(entityid pickedId);

    void deleteEntity();
    void cutEntity();
    void copyEntity();
    void pasteEntity();

    // Prop selection
    void setSelectedProp(uint32_t id)
    {
        m_selectedPropId = id;
        // Deselect any entities/brushes when a prop is selected
        clearSelectedEntities();
        m_selectedBrushIds.clear();
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

    // Brush selection (mutually exclusive with entity/prop selection)
    void setSelectedBrush(uint32_t id)
    {
        m_selectedBrushIds.clear();
        m_selectedBrushIds.push_back(id);
        clearSelectedEntities();
        m_selectedPropId = UINT32_MAX;
        g_currentEntity = _entity_null_value;
        g_currentSelectedObjectType = m_currentSelectedObject =
            static_cast<unsigned int>(SELECTED_OBJECT_TYPE::NONE);
    }

    void toggleBrushInSelection(uint32_t id)
    {
        for (auto it = m_selectedBrushIds.begin(); it != m_selectedBrushIds.end(); ++it)
        {
            if (*it == id)
            {
                m_selectedBrushIds.erase(it);
                return;
            }
        }
        m_selectedBrushIds.push_back(id);
    }

    void clearSelectedBrushes() { m_selectedBrushIds.clear(); }
    const std::vector<uint32_t>& getSelectedBrushes() const { return m_selectedBrushIds; }
    bool isBrushSelected() const { return !m_selectedBrushIds.empty(); }

    bool isBrushInSelection(uint32_t id) const
    {
        for (auto b : m_selectedBrushIds)
            if (b == id) return true;
        return false;
    }

    void deleteSelectedBrushes();

    // Brush clipboard: independent value copies, so the contents survive level
    // switches and never reference source brush ids.
    void cutBrushes();
    void copyBrushes();
    void pasteBrushes();

    bool hasBrushClipboard() const { return !m_brushClipboard.empty(); }
    ClipboardKind lastClipboardKind() const { return m_lastClipboardKind; }

    // Push one undo step (caps depth, clears the redo stack).  Used by the
    // gizmo grab paths and by BrushTool for brush operations.
    void pushUndoEntry(UndoEntry entry);

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
    BrushTool         m_brushTool;
    std::vector<entityid> m_selectedEntities;
    uint32_t              m_selectedPropId = UINT32_MAX;
    std::vector<uint32_t> m_selectedBrushIds;

    LinkPickField m_linkPickField = LinkPickField::NONE;
    entityid      m_linkPickHost = _entity_null_value;
    uint32_t      m_linkPickBrushId = 0;        // BRUSH_RECEIVER host

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

    std::vector<Brush> m_brushClipboard;
    bool m_brushClipboardFromCut = false;  // cut→paste restores in place (no offset)
    int  m_brushPasteCount = 0;            // consecutive pastes step the offset so copies don't stack
    ClipboardKind m_lastClipboardKind = ClipboardKind::NONE;

    entityid m_selectedEntity;

    std::string m_selectedWidgetType, m_selectedTransType;

    ImTransformControl::OPERATION m_widgetType;
    ImTransformControl::MODE m_widgetCoordSet;

    unsigned int m_currentSelectedObject;

	EditorConfiguration m_configuration;

};
