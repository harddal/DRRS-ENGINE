#include <fstream>

#include <cereal/archives/xml.hpp>
#include <spdlog/spdlog.h>

#include "Editor/SceneInteractionManager.h"
#include "Editor/Interface/EditorInterface.h"
#include "Engine/Brush/BrushManager.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputMap.h"
#include "Editor/EditorViewport.h"
#include "Engine/Renderer/RenderManager.h"
#include "Engine/World/WorldManager.h"
#include "Engine/Prop/PropManager.h"
#include "Game/Components/LogicComponent.h"
#include "Game/Components/TriggerZoneComponent.h"
#include "Game/LogicLinks.h"

using namespace irr;
using namespace core;
using namespace scene;
using namespace video;

#define _selected_entity (m_selectedEntities.empty() ? (_entity_null_value) : (m_selectedEntities.at(0)))

unsigned int g_currentEntity = _entity_null_value, g_undoEntity = _entity_null_value, g_undoCount = 0, g_currentMesh =
	             _entity_null_value, g_currentPrefab = _entity_null_value;
std::string g_currentScene;
extern std::string g_currentSelectedTexture = "null";
extern std::string g_textureBrowserRequestID = "";
unsigned int g_currentSelectedObjectType = static_cast<unsigned int>(SELECTED_OBJECT_TYPE::NONE);

static vector3df constrainAngleVector3(vector3df v)
{
	v.X = fmod(v.X, 360);
	if (v.X < 0)
		v.X += 360;

	v.Y = fmod(v.Y, 360);
	if (v.Y < 0)
		v.Y += 360;

	v.Z = fmod(v.Z, 360);
	if (v.Z < 0)
		v.Z += 360;

	return v;
}

void SceneInteractionManager::init()
{
	try
	{
		std::ifstream ifs_editor("config/editor.xml");
		cereal::XMLInputArchive editor_config(ifs_editor);

		editor_config(m_configuration);
	}
	catch (cereal::Exception& ex)
	{
		spdlog::warn("Failed to load editor configuration: {}, default values used", ex.what());

		m_configuration = EditorConfiguration();

		std::ofstream ofs_editor("config/editor.xml");
		cereal::XMLOutputArchive editor_config(ofs_editor);

		editor_config(m_configuration);
	}

	m_selectNewSpawnedEntity = false;
	m_clipboardSize = 0;

	m_gizmoWasUsing = false;
	m_rotationMatrixCached = false;
	m_transformUndoStack.clear();
	m_transformRedoStack.clear();

	m_isWidgetDrawn = false;
	m_useSnap = m_configuration.useSnap;

	m_snap[0] = m_configuration.snapX;
	m_snap[1] = m_configuration.snapY;
	m_snap[2] = m_configuration.snapZ;
	m_snapAngle = m_configuration.snapAngle;

	m_selectedWidgetType = "Translate";
	m_selectedTransType = "World";

	m_widgetType = ImTransformControl::TRANSLATE;
	m_widgetCoordSet = ImTransformControl::WORLD;

	m_selectedEntities.clear();
	std::vector<entityid>().swap(m_selectedEntities);

	m_selectedPropId = UINT32_MAX;
	m_selectedBrushIds.clear();
	m_currentSelectedObject = static_cast<unsigned int>(SELECTED_OBJECT_TYPE::NONE);

	m_brushTool.init(this);

	cancelLinkPick();
}

void SceneInteractionManager::completeLinkPick(entityid pickedId)
{
	auto field = m_linkPickField;
	auto hostId = m_linkPickHost;
	auto hostBrushId = m_linkPickBrushId;

	// One-shot: the mode ends no matter how the pick resolves
	cancelLinkPick();

	auto& picked = WorldManager::Get()->managerSystem()->getEntityByID(pickedId);
	if (!picked.isValid())
		return;

	const std::string& name = picked.getComponent<DescriptorComponent>().name;

	// Brush-hosted pick: the host lives in BrushManager, not the ECS
	if (field == LinkPickField::BRUSH_RECEIVER)
	{
		if (BrushManager::Get())
		{
			if (Brush* brush = BrushManager::Get()->getBrush(hostBrushId))
				LogicLinks::appendUniqueName(brush->receiver, name);
		}
		return;
	}

	auto& host = WorldManager::Get()->managerSystem()->getEntityByID(hostId);
	if (!host.isValid())
		return;

	switch (field)
	{
	case LinkPickField::LOGIC_RECEIVER:
		if (host.hasComponent<LogicComponent>())
			LogicLinks::appendUniqueName(host.getComponent<LogicComponent>().receiver, name);
		break;

	case LinkPickField::ZONE_DETECT_ENTITY:
		if (host.hasComponent<TriggerZoneComponent>())
			host.getComponent<TriggerZoneComponent>().entity = name;
		break;

	case LinkPickField::ZONE_TRIGGERED_ENTITY:
		if (host.hasComponent<TriggerZoneComponent>())
		{
			auto& zone = host.getComponent<TriggerZoneComponent>();
			// "null" is the component's empty sentinel — replace it, don't append to it
			if (zone.triggered_entity == "null")
				zone.triggered_entity.clear();
			LogicLinks::appendUniqueName(zone.triggered_entity, name);
		}
		break;

	default:
		break;
	}
}

void SceneInteractionManager::update(float dt)
{
	m_painter.update(dt);
    m_texturePainter.update(dt);
    if (!m_painter.isActive() && !m_texturePainter.active)
        m_brushTool.update(dt);

	if (m_selectNewSpawnedEntity)
	{
		setSelectedEntity(WorldManager::Get()->managerSystem()->getMostRecentEntityID());

		m_selectNewSpawnedEntity = false;
	}

	// Abort a link pick if its host disappeared (deleted, scene change)
	if (isLinkPicking())
	{
		bool hostGone;
		if (m_linkPickField == LinkPickField::BRUSH_RECEIVER)
			hostGone = !BrushManager::Get() || !BrushManager::Get()->getBrush(m_linkPickBrushId);
		else
			hostGone = !WorldManager::Get()->managerSystem()->getEntityByID(m_linkPickHost).isValid();
		if (hostGone)
			cancelLinkPick();
	}

	static bool escPressed = false;
	if (InputManager::Get()->getKeyPressOnce(KEYBOARD_KEY::KEY_ESCAPE, &escPressed))
	{
		if (isLinkPicking())
		{
			// Esc only cancels the pick; the host entity stays selected
			cancelLinkPick();
		}
		else if (m_brushTool.handleEscape())
		{
			// Esc consumed by the brush tool (cleared clip points or exited
			// the sub-mode); selection survives
		}
		else
		{
			//g_currentEntity = _selected_entity;
			clearSelectedEntities();
			m_selectedPropId = UINT32_MAX;
			m_selectedBrushIds.clear();
			//g_currentMesh = _mesh_null_value;
			m_currentSelectedObject = static_cast<unsigned int>(SELECTED_OBJECT_TYPE::NONE);
		}
	}

	static bool mouseClick = false;
	if (!m_painter.isActive() && !m_brushTool.isActive() &&
		InputManager::Get()->getMousePressOnce(0, &mouseClick) && InputManager::Get()->isKeyPressed(
		KEYBOARD_KEY::KEY_LSHIFT))
	{
		// BUG: If entity is deleted, selection will not work due to a bug in the transform control
		// This avoids the bug with a small drawback: don't hold shift and use the transform control at the same time
		//if (!ImTransformControl::IsOver() && !ImTransformControl::IsUsing()) {

		auto node = RenderManager::Get()->getNodeFromRay(EditorViewport::rayFromMouse(nullptr));
		if (node)
		{
			// Link pick mode consumes the click: write the picked entity's name
			// into the pending field and leave the normal selection untouched
			if (isLinkPicking())
			{
				if (node->getID() < _entity_null_value)
				{
					completeLinkPick(node->getID());
				}
				return;
			}

			// Check props first (they have no numeric ID in the ECS range)
			if (PropManager::Get())
			{
				StaticProp* hitProp = PropManager::Get()->getPropFromNode(node);
				if (hitProp)
				{
					setSelectedProp(hitProp->id);
					return;
				}
			}

			// Then brush chunks: the node identifies the chunk, an exact
			// plane-set raycast identifies which member brush was clicked
			if (BrushManager::Get() && BrushManager::Get()->getChunkFromNode(node))
			{
				auto* smgr = RenderManager::Get()->sceneManager();
				line3d<f32> ray = EditorViewport::rayFromMouse(smgr->getActiveCamera());

				vector3df dir = ray.end - ray.start;
				const float len = dir.getLength();
				if (len > 0.001f)
				{
					dir /= len;
					BrushManager::BrushRayHit hit;
					if (BrushManager::Get()->raycastBrushes(ray.start, dir, len, hit))
					{
						if (InputManager::Get()->isKeyPressed(KEYBOARD_KEY::KEY_LCONTROL) && isBrushSelected())
						{
							toggleBrushInSelection(hit.brushId);
						}
						else
						{
							setSelectedBrush(hit.brushId);
						}
					}
				}
				return;
			}

			if (node->getID() < _entity_null_value)
			{
				entityid eid = node->getID();

				// Clicking an entity clears prop/brush selection
				m_selectedPropId = UINT32_MAX;
				m_selectedBrushIds.clear();

				if (InputManager::Get()->isKeyPressed(KEYBOARD_KEY::KEY_LCONTROL))
				{
					// CTRL held: toggle entity in/out of multi-selection
					if (isEntityInSelection(eid))
					{
						removeFromSelection(eid);
					}
					else
					{
						m_selectedEntities.emplace_back(eid);
						g_currentEntity = eid;
						g_currentSelectedObjectType = m_currentSelectedObject = static_cast<unsigned int>(
							SELECTED_OBJECT_TYPE::ENTITY);
					}
				}
				else
				{
					// No CTRL: replace selection with single entity
					setSelectedEntity(eid);
					g_currentEntity = _selected_entity;
					g_currentSelectedObjectType = m_currentSelectedObject = static_cast<unsigned int>(
						SELECTED_OBJECT_TYPE::ENTITY);
				}
			}
		}

		//}
	}
}

void SceneInteractionManager::draw()
{
	m_isWidgetDrawn = false;

	// DEBUG TEMP
	m_useSnap = m_configuration.useSnap;
	m_snap[0] = m_configuration.snapX;
	m_snap[1] = m_configuration.snapY;
	m_snap[2] = m_configuration.snapZ;
	m_snapAngle = m_configuration.snapAngle;

	// --- Prop gizmo (independent of the entity selection state) ---
	if (m_selectedPropId != UINT32_MAX && PropManager::Get())
	{
		StaticProp* prop = PropManager::Get()->getProp(m_selectedPropId);
		if (prop && prop->node)
		{
			// Draw bounding box overlay
			RenderManager::Get()->renderBox3DOverlay(
				prop->node->getTransformedBoundingBox(),
				irr::video::SColor(255, 100, 220, 100));

			// Gizmo — translate/rotate/scale exactly like single-entity path
			irr::core::matrix4 transform;
			switch (m_widgetType)
			{
			case ImTransformControl::TRANSLATE:
				transform.setTranslation(prop->position);
				break;
			case ImTransformControl::ROTATE:
				if (m_rotationMatrixCached && m_gizmoWasUsing) {
					transform = m_cachedRotationMatrix;
				} else {
					transform.setTranslation(prop->position);
					transform.setRotationDegrees(prop->rotation);
				}
				break;
			case ImTransformControl::SCALE:
				transform.setTranslation(prop->position);
				transform.setScale(prop->scale);
				break;
			default:
				break;
			}

			ImTransformControl::SetDrawlist();
			EditorViewport::setGizmoRect();
			ImTransformControl::Manipulate(
				RenderManager::Get()->sceneManager()->getActiveCamera()->getViewMatrix().pointer(),
				RenderManager::Get()->sceneManager()->getActiveCamera()->getProjectionMatrix().pointer(),
				m_widgetType, m_widgetCoordSet,
				transform.pointer(), nullptr, activeSnap());

			if (ImTransformControl::IsUsing())
			{
				switch (m_widgetType)
				{
				case ImTransformControl::TRANSLATE:
					prop->position = transform.getTranslation();
					break;
				case ImTransformControl::ROTATE:
					m_cachedRotationMatrix = transform;
					m_rotationMatrixCached = true;
					prop->rotation = transform.getRotationDegrees();
					break;
				case ImTransformControl::SCALE:
					prop->scale = transform.getScale();
					break;
				default:
					break;
				}
				PropManager::Get()->applyTransform(m_selectedPropId);
			}
			if (!ImTransformControl::IsUsing())
				m_rotationMatrixCached = false;

			m_isWidgetDrawn = true;
		}
		else
		{
			// Prop was removed externally
			m_selectedPropId = UINT32_MAX;
		}
	}

	switch (static_cast<SELECTED_OBJECT_TYPE>(m_currentSelectedObject))
	{
	case SELECTED_OBJECT_TYPE::NONE:
	{
	}
	break;
	case SELECTED_OBJECT_TYPE::ENTITY:
	{
		if (m_selectedEntities.empty()) break;

		if (m_selectedEntities.size() == 1)
		{
			// --- Single entity ---
			if (_selected_entity < _entity_null_value)
			{
				if (WorldManager::Get()->managerSystem()->getEntityByID(_selected_entity).isValid())
				{
					auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(_selected_entity);

					auto& selectedTransform = entity.getComponent<TransformComponent>();

					m_isWidgetDrawn = true;

					matrix4 transform;

					switch (m_widgetType)
					{
					case ImTransformControl::TRANSLATE:
						transform.setTranslation(selectedTransform.getPosition());
						break;
					case ImTransformControl::ROTATE:
						if (m_rotationMatrixCached && m_gizmoWasUsing) {
							transform = m_cachedRotationMatrix;
						} else {
							transform.setTranslation(selectedTransform.getPosition());
							transform.setRotationDegrees(selectedTransform.getRotation());
						}
						break;
					case ImTransformControl::SCALE:
						transform.setTranslation(selectedTransform.getPosition());
						transform.setScale(selectedTransform.getScale());
						break;
					case ImTransformControl::BOUNDS:
						break;
					default:
						break;
					}


					ImTransformControl::SetDrawlist();

					EditorViewport::setGizmoRect();
					ImTransformControl::Manipulate(
						RenderManager::Get()->sceneManager()->getActiveCamera()->getViewMatrix().pointer(),
						RenderManager::Get()->sceneManager()->getActiveCamera()->getProjectionMatrix().pointer(),
						m_widgetType, m_widgetCoordSet,
						transform.pointer(), nullptr, activeSnap());

					if (!m_gizmoWasUsing && ImTransformControl::IsUsing())
					{
						UndoEntry entry;
						entry.transforms.push_back({ _selected_entity,
							selectedTransform.getPosition(),
							selectedTransform.getRotation(),
							selectedTransform.getScale() });
						pushUndoEntry(std::move(entry));
					}

					if (ImTransformControl::IsUsing())
					{
						switch (m_widgetType)
						{
						case ImTransformControl::TRANSLATE:
							selectedTransform.setPosition(transform.getTranslation());
							break;
						case ImTransformControl::ROTATE:
							m_cachedRotationMatrix = transform;
							m_rotationMatrixCached = true;
							selectedTransform.setRotation(transform.getRotationDegrees());
							break;
						case ImTransformControl::SCALE:
							selectedTransform.setScale(transform.getScale());
							break;
						case ImTransformControl::BOUNDS:
							break;
						default:
							break;
						}

						WorldManager::Get()->renderSystem()->forceTransformUpdate();
					}
					if (!ImTransformControl::IsUsing())
						m_rotationMatrixCached = false;

					if (m_configuration.drawPointLightBounds)
					{
						if (entity.hasComponent<LightComponent>())
						{
							auto& light = entity.getComponent<LightComponent>();

							if (light.type == LIGHT_TYPE::LT_POINT)
							{
								if (entity.hasComponent<DebugMeshComponent>())
								{
									entity.getComponent<DebugMeshComponent>().node->setVisible(true);
								}
							}
						}
					}

					if (entity.hasComponent<MeshComponent>())
					{
						auto& mc = entity.getComponent<MeshComponent>();
						if (mc.node)
						{
							RenderManager::Get()->renderBox3DOverlay(
								mc.node->getTransformedBoundingBox(),
								SColor(255, 255, 140, 0));
						}
					}
				}
			}
		}
		else
		{
			// Draw light bounds for selected lights
			for (auto id : m_selectedEntities)
			{
				if (id < _entity_null_value)
				{
					auto& e = WorldManager::Get()->managerSystem()->getEntityByID(id);

					if (m_configuration.drawPointLightBounds)
					{
						if (e.hasComponent<LightComponent>())
						{
							auto& light = e.getComponent<LightComponent>();

							if (light.type == LIGHT_TYPE::LT_POINT)
							{
								if (e.hasComponent<DebugMeshComponent>())
								{
									e.getComponent<DebugMeshComponent>().node->setVisible(true);
								}
							}
						}
					}
				}
			}

			// Draw selection indicator for entities with meshes
			{
				for (auto id : m_selectedEntities)
				{
					if (id < _entity_null_value)
					{
						auto& e = WorldManager::Get()->managerSystem()->getEntityByID(id);

						if (e.hasComponent<MeshComponent>())
						{
							auto& m = e.getComponent<MeshComponent>();

							if (m.node)
							{
								RenderManager::Get()->renderBox3DOverlay(
									m.node->getTransformedBoundingBox(),
									SColor(255, 255, 140, 0));
							}
						}
					}
				}
			}

			// --- Multi-entity: gizmo at centroid, apply deltas to all ---
			int validCount = 0;
			vector3df centroid(0, 0, 0);
			entityid firstValidID = _entity_null_value;

			for (auto id : m_selectedEntities)
			{
				if (id < _entity_null_value)
				{
					auto& e = WorldManager::Get()->managerSystem()->getEntityByID(id);
					if (e.isValid())
					{
						centroid += e.getComponent<TransformComponent>().getPosition();
						if (validCount == 0) firstValidID = id;
						validCount++;
					}
				}
			}

			if (validCount == 0) break;
			centroid /= static_cast<float>(validCount);

			auto& firstTransform = WorldManager::Get()->managerSystem()->getEntityByID(firstValidID)
				.getComponent<TransformComponent>();

			m_isWidgetDrawn = true;

			matrix4 transform;
			vector3df prevPos  = centroid;
			vector3df prevRot  = firstTransform.getRotation();
			vector3df prevScale = firstTransform.getScale();

			switch (m_widgetType)
			{
			case ImTransformControl::TRANSLATE:
				transform.setTranslation(centroid);
				break;
			case ImTransformControl::ROTATE:
				transform.setTranslation(centroid);
				transform.setRotationDegrees(prevRot);
				break;
			case ImTransformControl::SCALE:
				transform.setTranslation(centroid);
				transform.setScale(prevScale);
				break;
			case ImTransformControl::BOUNDS:
				break;
			default:
				break;
			}


			ImTransformControl::SetDrawlist();
			EditorViewport::setGizmoRect();
			ImTransformControl::Manipulate(
				RenderManager::Get()->sceneManager()->getActiveCamera()->getViewMatrix().pointer(),
				RenderManager::Get()->sceneManager()->getActiveCamera()->getProjectionMatrix().pointer(),
				m_widgetType, m_widgetCoordSet,
				transform.pointer(), nullptr, activeSnap());

			if (!m_gizmoWasUsing && ImTransformControl::IsUsing())
			{
				UndoEntry entry;
				for (auto id : m_selectedEntities)
				{
					if (id < _entity_null_value)
					{
						auto& e = WorldManager::Get()->managerSystem()->getEntityByID(id);
						if (e.isValid() && e.hasComponent<TransformComponent>())
						{
							auto& tc = e.getComponent<TransformComponent>();
							entry.transforms.push_back({ id, tc.getPosition(), tc.getRotation(), tc.getScale() });
						}
					}
				}
				pushUndoEntry(std::move(entry));
			}

			if (ImTransformControl::IsUsing())
			{
				switch (m_widgetType)
				{
				case ImTransformControl::TRANSLATE:
				{
					vector3df delta = transform.getTranslation() - prevPos;
					for (auto id : m_selectedEntities)
					{
						if (id < _entity_null_value)
						{
							auto& e = WorldManager::Get()->managerSystem()->getEntityByID(id);
							if (e.isValid())
							{
								auto& tc = e.getComponent<TransformComponent>();
								tc.setPosition(tc.getPosition() + delta);
							}
						}
					}
					break;
				}
				case ImTransformControl::ROTATE:
				{
					vector3df deltaRot = transform.getRotationDegrees() - prevRot;
					for (auto id : m_selectedEntities)
					{
						if (id < _entity_null_value)
						{
							auto& e = WorldManager::Get()->managerSystem()->getEntityByID(id);
							if (e.isValid())
							{
								auto& tc = e.getComponent<TransformComponent>();
								tc.setRotation(constrainAngleVector3(tc.getRotation() + deltaRot));
							}
						}
					}
					break;
				}
				case ImTransformControl::SCALE:
				{
					vector3df newScale = transform.getScale();
					vector3df ratio(
						prevScale.X > 0.0001f ? newScale.X / prevScale.X : 1.0f,
						prevScale.Y > 0.0001f ? newScale.Y / prevScale.Y : 1.0f,
						prevScale.Z > 0.0001f ? newScale.Z / prevScale.Z : 1.0f);
					for (auto id : m_selectedEntities)
					{
						if (id < _entity_null_value)
						{
							auto& e = WorldManager::Get()->managerSystem()->getEntityByID(id);
							if (e.isValid())
							{
								auto& tc = e.getComponent<TransformComponent>();
								vector3df s = tc.getScale();
								tc.setScale(vector3df(s.X * ratio.X, s.Y * ratio.Y, s.Z * ratio.Z));
							}
						}
					}
					break;
				}
				default:
					break;
				}

				WorldManager::Get()->renderSystem()->forceTransformUpdate();
			}
		}
	}
	break;
	case SELECTED_OBJECT_TYPE::MESH:
	{
		//    if (g_currentMesh < _mesh_null_value) {
		//        auto mesh = _world->getStaticMeshNode(g_currentMesh);

		//        m_isWidgetDrawn = true;

		//        matrix4 transform;
		//        //transform.setTranslation(selectedTransform.getPosition());
		//        //transform.setRotationDegrees(selectedTransform.getRotation());
		//        //transform.setScale(selectedTransform.getScale());

		//        switch (m_widgetType) {
		//        case ImTransformControl::TRANSLATE:
		//            transform.setTranslation(mesh->getPosition());
		//            break;
		//        case ImTransformControl::ROTATE:
		//            transform.setTranslation(mesh->getPosition());
		//            transform.setRotationDegrees(mesh->getRotation());
		//            break;
		//        case ImTransformControl::SCALE:
		//            transform.setTranslation(mesh->getPosition());
		//            transform.setScale(mesh->getScale());
		//            break;
		//        case ImTransformControl::BOUNDS:
		//            break;
		//        default:
		//            break;
		//        }

		//        ImGuiIO& io = ImGui::GetIO();

		//        ImTransformControl::SetDrawlist();

		//        ImTransformControl::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);
		//        ImTransformControl::Manipulate(
		//            _render->sceneManager()->getActiveCamera()->getViewMatrix().pointer(),
		//            _render->sceneManager()->getActiveCamera()->getProjectionMatrix().pointer(), m_widgetType, m_widgetCoordSet,
		//            transform.pointer(), nullptr, m_useSnap ? &m_snap[0] : nullptr);

		//        if (ImTransformControl::IsUsing()) {
		//            switch (m_widgetType) {
		//            case ImTransformControl::TRANSLATE:
		//                mesh->setPosition(transform.getTranslation());
		//                break;
		//            case ImTransformControl::ROTATE:
		//                mesh->setRotation(transform.getRotationDegrees());
		//                break;
		//            case ImTransformControl::SCALE:
		//                mesh->setScale(transform.getScale());
		//                break;
		//            case ImTransformControl::BOUNDS:
		//                break;
		//            default:
		//                break;
		//            }

		//            _world->renderSystem()->forceTransformUpdate();
		//        }
		//    }
	}
	break;
	}
	m_gizmoWasUsing = ImTransformControl::IsUsing();

	m_brushTool.draw();

	if (m_painter.isActive())
		m_painter.drawBrush();

    if (m_texturePainter.active)
        m_texturePainter.drawBrush();
}

void SceneInteractionManager::destroy()
{
}

void SceneInteractionManager::deleteEntity()
{
	if (m_selectedEntities.empty()) return;

	for (auto id : m_selectedEntities)
	{
		if (id < _entity_null_value)
			WorldManager::Get()->killEntityByID(id);
	}
	clearSelectedEntities();
	g_currentEntity = _entity_null_value;
}

void SceneInteractionManager::deleteProp()
{
	if (m_selectedPropId == UINT32_MAX || !PropManager::Get()) return;
	PropManager::Get()->removeProp(m_selectedPropId);
	m_selectedPropId = UINT32_MAX;
}

void SceneInteractionManager::cutProp()
{
	if (m_selectedPropId == UINT32_MAX || !PropManager::Get()) return;
	StaticProp* prop = PropManager::Get()->getProp(m_selectedPropId);
	if (!prop) return;
	m_propClipboard      = *prop;
	m_propClipboardValid = true;
	m_lastClipboardKind  = ClipboardKind::PROP;
	PropManager::Get()->removeProp(m_selectedPropId);
	m_selectedPropId = UINT32_MAX;
}

void SceneInteractionManager::copyProp()
{
	if (m_selectedPropId == UINT32_MAX || !PropManager::Get()) return;
	StaticProp* prop = PropManager::Get()->getProp(m_selectedPropId);
	if (!prop) return;
	m_propClipboard      = *prop;
	m_propClipboardValid = true;
	m_lastClipboardKind  = ClipboardKind::PROP;
}

void SceneInteractionManager::pasteProp()
{
	if (!m_propClipboardValid || !PropManager::Get()) return;
	StaticProp copy  = m_propClipboard;
	copy.id          = 0;   // addProp assigns a fresh id
	uint32_t newId   = PropManager::Get()->addProp(copy);
	setSelectedProp(newId);
}

void SceneInteractionManager::cutEntity()
{
	if (m_selectedEntities.empty()) return;

	m_clipboardSize = 0;
	for (auto id : m_selectedEntities)
	{
		if (id < _entity_null_value)
		{
			auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(id);
			if (entity.isValid())
			{
				std::string file = std::string(CLIPBOARD_ENTITY_FILENAME) + std::to_string(m_clipboardSize);
				WorldManager::Get()->exportEntity(entity, file, true);
				m_clipboardSize++;
			}
		}
	}
	if (m_clipboardSize > 0)
		m_lastClipboardKind = ClipboardKind::ENTITY;

	for (auto id : m_selectedEntities)
	{
		if (id < _entity_null_value)
			WorldManager::Get()->killEntityByID(id);
	}
	clearSelectedEntities();
	g_currentEntity = _entity_null_value;
}

void SceneInteractionManager::copyEntity()
{
	if (m_selectedEntities.empty()) return;

	m_clipboardSize = 0;
	for (auto id : m_selectedEntities)
	{
		if (id < _entity_null_value)
		{
			auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(id);
			if (entity.isValid())
			{
				std::string file = std::string(CLIPBOARD_ENTITY_FILENAME) + std::to_string(m_clipboardSize);
				WorldManager::Get()->exportEntity(entity, file, true);
				m_clipboardSize++;
			}
		}
	}
	if (m_clipboardSize > 0)
		m_lastClipboardKind = ClipboardKind::ENTITY;
}

void SceneInteractionManager::pasteEntity()
{
	for (int i = 0; i < m_clipboardSize; i++)
	{
		std::string file = std::string(CLIPBOARD_ENTITY_FILENAME) + std::to_string(i);
		WorldManager::Get()->spawnEntity(file, std::string(), true);
	}
}

void SceneInteractionManager::setTransformWidgetMode(TRANSFORM_WIDGET_MODE mode)
{
	switch (mode)
	{
	case TRANSFORM_WIDGET_MODE::TRANSLATE:
		m_widgetType = ImTransformControl::TRANSLATE;
		m_selectedWidgetType = "Translate";
		break;
	case TRANSFORM_WIDGET_MODE::ROTATE:
		m_widgetType = ImTransformControl::ROTATE;
		m_selectedWidgetType = "Rotate";
		break;
	case TRANSFORM_WIDGET_MODE::SCALE:
		m_widgetType = ImTransformControl::SCALE;
		m_selectedWidgetType = "Scale";
		break;
	case TRANSFORM_WIDGET_MODE::LOCAL:
		m_widgetCoordSet = ImTransformControl::LOCAL;
		m_selectedTransType = "Local";
		break;
	case TRANSFORM_WIDGET_MODE::WORLD:
		m_widgetCoordSet = ImTransformControl::WORLD;
		m_selectedTransType = "World";
		break;
	}
}

void SceneInteractionManager::saveConfiguration(EditorConfiguration& configuration)
{
	std::ofstream ofs_editor("config/editor.xml");
	cereal::XMLOutputArchive editor_config(ofs_editor);

	m_configuration = configuration;
	editor_config(configuration);
}

void SceneInteractionManager::pushUndoEntry(UndoEntry entry)
{
	if (entry.empty()) return;

	if (m_transformUndoStack.size() >= static_cast<size_t>(k_maxUndoDepth))
		m_transformUndoStack.erase(m_transformUndoStack.begin());
	m_transformUndoStack.push_back(std::move(entry));
	m_transformRedoStack.clear();
}

namespace
{
	// Capture the CURRENT state of everything a snapshot touches (for the
	// opposite stack), then restore the snapshot.  Shared by undo and redo.
	UndoEntry captureCurrentState(const UndoEntry& snapshot)
	{
		UndoEntry current;

		for (auto& snap : snapshot.transforms)
		{
			auto& e = WorldManager::Get()->managerSystem()->getEntityByID(snap.id);
			if (e.isValid() && e.hasComponent<TransformComponent>())
			{
				auto& tc = e.getComponent<TransformComponent>();
				current.transforms.push_back({ snap.id, tc.getPosition(), tc.getRotation(), tc.getScale() });
			}
		}

		if (BrushManager::Get())
		{
			for (auto& snap : snapshot.brushes)
			{
				BrushSnapshot cur;
				cur.id = snap.id;
				Brush* brush = BrushManager::Get()->getBrush(snap.id);
				cur.existed = (brush != nullptr);
				if (brush)
					cur.data = *brush;
				current.brushes.push_back(std::move(cur));
			}
		}

		return current;
	}

	void restoreSnapshot(const UndoEntry& snapshot)
	{
		for (auto& snap : snapshot.transforms)
		{
			auto& e = WorldManager::Get()->managerSystem()->getEntityByID(snap.id);
			if (e.isValid() && e.hasComponent<TransformComponent>())
			{
				auto& tc = e.getComponent<TransformComponent>();
				tc.setPosition(snap.position);
				tc.setRotation(snap.rotation);
				tc.setScale(snap.scale);
			}
		}
		if (!snapshot.transforms.empty())
			WorldManager::Get()->renderSystem()->forceTransformUpdate();

		if (BrushManager::Get())
		{
			for (auto& snap : snapshot.brushes)
			{
				if (snap.existed)
					BrushManager::Get()->restoreBrush(snap.data);
				else
					BrushManager::Get()->removeBrush(snap.id);
			}
		}
	}
}

void SceneInteractionManager::undoTransform()
{
	if (m_transformUndoStack.empty()) return;

	UndoEntry snapshot = std::move(m_transformUndoStack.back());
	m_transformUndoStack.pop_back();

	UndoEntry redoEntry = captureCurrentState(snapshot);
	if (!redoEntry.empty())
	{
		if (m_transformRedoStack.size() >= static_cast<size_t>(k_maxUndoDepth))
			m_transformRedoStack.erase(m_transformRedoStack.begin());
		m_transformRedoStack.push_back(std::move(redoEntry));
	}

	restoreSnapshot(snapshot);
}

void SceneInteractionManager::redoTransform()
{
	if (m_transformRedoStack.empty()) return;

	UndoEntry snapshot = std::move(m_transformRedoStack.back());
	m_transformRedoStack.pop_back();

	UndoEntry undoEntry = captureCurrentState(snapshot);
	if (!undoEntry.empty())
	{
		if (m_transformUndoStack.size() >= static_cast<size_t>(k_maxUndoDepth))
			m_transformUndoStack.erase(m_transformUndoStack.begin());
		m_transformUndoStack.push_back(std::move(undoEntry));
	}

	restoreSnapshot(snapshot);
}

void SceneInteractionManager::deleteSelectedBrushes()
{
	if (m_selectedBrushIds.empty() || !BrushManager::Get())
		return;

	UndoEntry entry;
	for (uint32_t id : m_selectedBrushIds)
	{
		Brush* brush = BrushManager::Get()->getBrush(id);
		if (!brush)
			continue;
		BrushSnapshot snap;
		snap.id = id;
		snap.existed = true;
		snap.data = *brush;
		entry.brushes.push_back(std::move(snap));
	}
	pushUndoEntry(std::move(entry));

	for (uint32_t id : m_selectedBrushIds)
		BrushManager::Get()->removeBrush(id);
	m_selectedBrushIds.clear();
}

// Matches the auto-generated "Brush <id>" pattern so paste can regenerate the
// name for the fresh id; user-authored names are kept verbatim.
static bool isDefaultBrushName(const std::string& name)
{
	const char* prefix = "Brush ";
	const size_t prefixLen = 6;
	if (name.size() <= prefixLen || name.compare(0, prefixLen, prefix) != 0)
		return false;
	for (size_t i = prefixLen; i < name.size(); ++i)
		if (name[i] < '0' || name[i] > '9')
			return false;
	return true;
}

void SceneInteractionManager::copyBrushes()
{
	if (m_selectedBrushIds.empty() || !BrushManager::Get())
		return;

	m_brushClipboard.clear();
	for (uint32_t id : m_selectedBrushIds)
	{
		if (Brush* brush = BrushManager::Get()->getBrush(id))
			m_brushClipboard.push_back(*brush);
	}
	if (m_brushClipboard.empty())
		return;

	m_brushClipboardFromCut = false;
	m_brushPasteCount = 0;
	m_lastClipboardKind = ClipboardKind::BRUSH;
}

void SceneInteractionManager::cutBrushes()
{
	if (m_selectedBrushIds.empty())
		return;

	copyBrushes();
	if (m_brushClipboard.empty())
		return;

	m_brushClipboardFromCut = true;
	deleteSelectedBrushes();
}

void SceneInteractionManager::pasteBrushes()
{
	if (m_brushClipboard.empty() || !BrushManager::Get())
		return;

	// A cut pastes back in place; a copy starts one step out.  Consecutive
	// pastes keep stepping so repeated Ctrl+V never stacks coincident brushes.
	const float step = m_configuration.useSnap ? m_configuration.snapX : 1.0f;
	const float d = step * (m_brushPasteCount + (m_brushClipboardFromCut ? 0 : 1));
	m_brushPasteCount++;

	matrix4 T;
	T.setTranslation(vector3df(d, 0.0f, d));

	UndoEntry entry;
	std::vector<uint32_t> newIds;
	for (const Brush& src : m_brushClipboard)
	{
		Brush clone = src;
		for (size_t i = 0; i < clone.faces.size(); ++i)
			clone.faces[i] = BrushTool::transformFace(src.faces[i], T);
		// The clipboard copy carries the source's derived verts/bounds;
		// addBrush only rebuilds when geometryValid is false, and chunk
		// assignment needs post-offset bounds.
		clone.geometryValid = false;
		if (isDefaultBrushName(clone.name))
			clone.name.clear();

		const uint32_t newId = BrushManager::Get()->addBrush(std::move(clone));
		if (newId == 0)
			continue;

		BrushSnapshot snap;
		snap.id = newId;
		snap.existed = false;
		entry.brushes.push_back(std::move(snap));
		newIds.push_back(newId);
	}
	if (newIds.empty())
		return;

	pushUndoEntry(std::move(entry));

	// Paste replaces the selection, so drop any modal sub-tool first — a
	// mid-clip/face operation must not reference the swapped-out selection.
	m_brushTool.setMode(BrushToolMode::OFF);
	setSelectedBrush(newIds[0]);
	for (size_t i = 1; i < newIds.size(); ++i)
		toggleBrushInSelection(newIds[i]);
}
