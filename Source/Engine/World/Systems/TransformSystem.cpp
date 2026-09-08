#include "TransformSystem.h"

#include "Engine/Engine.h"
#include "Utility/Utility.h"

using namespace irr::core;

void TransformComponent::addChild(irr::scene::ISceneNode* child_node)
{
	if (child_node)
	{
		auto &child_ent = WorldManager::Get()->managerSystem()->getEntityByID(child_node->getID());
		if (!child_ent.isValid())
		{
			spdlog::warn("Child entity is not valid in TransformComponent::addChild");
			return;
		}

		child_node->updateAbsolutePosition();

		node->addChild(child_node);
		children.push_back(child_node->getID());

		auto &child_tc = child_ent.getComponent<TransformComponent>();
		child_tc.parent  = node->getID();
		child_tc.isChild = true;

		isParent = true;
	}
}

void TransformComponent::removeChild(irr::scene::ISceneNode* child_node)
{
	if (child_node && children.size() > 0)
	{
		int iter = 0;
		bool found = false;

		for (auto i : children)
		{
			if (i == child_node->getID())
			{
				found = true;
				break;
			}

			iter++;
		}

		if (found)
		{
			children.erase(children.begin() + iter);
		}

		auto &child_tc = WorldManager::Get()->managerSystem()->getEntityByID(child_node->getID())
		                     .getComponent<TransformComponent>();
		child_tc.parent  = _entity_null_value;
		child_tc.isChild = false;

		node->removeChild(child_node);

		if (children.empty())
		{
			isParent = false;
		}
	}
}

void TransformSystem::onEntityAdded(anax::Entity& entity)
{
    auto& transform = entity.getComponent<TransformComponent>();

    transform.node =
        RenderManager::Get()->sceneManager()->addEmptySceneNode();

	transform.node->setID(entity.getComponent<DescriptorComponent>().id);

	transform.initialPosition = transform.position;
	transform.initialRotation = Math::ConstrainAngleVector3(transform.rotation);
	transform.initialScale    = transform.scale;
	
    transform.node->setPosition(transform.position);
    transform.node->setRotation(Math::ConstrainAngleVector3(transform.rotation));
    transform.node->setScale(transform.scale);

    // No re-parenting on load: isChild/parent are runtime-only now.  A parent
    // link cannot be restored from a file here anyway - onEntityAdded runs while
    // the archive is still being read, so a forward reference to a sibling later
    // in the file does not exist yet.  Persisting a hierarchy needs a name-based
    // link resolved in a post-load fixup pass once every entity exists; see
    // "To Do Lists/prefab_system_plan.md", section 3 (P2).
}


void TransformSystem::onEntityRemoved(anax::Entity& entity)
{
	auto &transform = entity.getComponent<TransformComponent>();

	for (auto id : transform.children)
	{
		auto &child_entity = WorldManager::Get()->managerSystem()->getEntityByID(id);
		if (child_entity.isValid())
		{
			transform.removeChild(child_entity.getComponent<TransformComponent>().node);
		}
	}

	transform.children.clear();

    RenderManager::Get()->sceneManager()->addToDeletionQueue(entity.getComponent<TransformComponent>().node);
}

void TransformSystem::update()
{
    auto& entities = getEntities();

    for (auto& entity : entities) 
    {
		auto& transform = entity.getComponent<TransformComponent>();

		// DEBUG: Fixes bug where rotations can lurch but may have unintended side effects
        transform.node->setRotation(Math::ConstrainAngleVector3(transform.rotation));

		transform.node->setPosition(transform.position);

        transform.node->updateAbsolutePosition();
    }
}
