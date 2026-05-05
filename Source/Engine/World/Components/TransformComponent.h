#pragma once

#include <string>

#include "anax/Component.hpp"
#include "cereal/cereal.hpp"
#include "cereal/types/string.hpp"
#include "cereal/types/vector.hpp"
#include "irrlicht.h"

#include "DescriptorComponent.h"

#include "Types.h"

struct TransformComponent : anax::Component
{
    TransformComponent() :
        isChild(false), isParent(false), inheritScale(false),
        position(irr::core::vector3df(0, 0, 0)), 
        rotation(irr::core::vector3df(0, 0, 0)), 
        scale(   irr::core::vector3df(1, 1, 1)),
        position_local(irr::core::vector3df(0, 0, 0)),
        rotation_local(irr::core::vector3df(0, 0, 0)),
        scale_local(irr::core::vector3df(1, 1, 1)),
		offset(  irr::core::vector3df(0, 0, 0)),
        initialPosition(irr::core::vector3df(0, 0, 0)),
        initialRotation(irr::core::vector3df(0, 0, 0)),
        initialScale(   irr::core::vector3df(1, 1, 1)),
        node(nullptr), parent(_entity_null_value) {}

    bool isChild, isParent, inheritScale;

    irr::core::vector3df position, rotation, scale, offset;

    irr::core::vector3df position_local, rotation_local, scale_local;

    irr::core::vector3df initialPosition, initialRotation, initialScale;

    irr::scene::ISceneNode* node;

	entityid parent;
	std::vector<entityid> children;

	std::string parent_name;
	std::vector<std::string> children_names;

    template <class Archive>
    void save(Archive& archive) const
    {
        archive(
            CEREAL_NVP_("position.X", getPosition().X), CEREAL_NVP_("position.Y", getPosition().Y), CEREAL_NVP_("position.Z", getPosition().Z),
            CEREAL_NVP_("rotation.X", getRotation().X), CEREAL_NVP_("rotation.Y", getRotation().Y), CEREAL_NVP_("rotation.Z", getRotation().Z),
            CEREAL_NVP_("scale.X", getScale().X), CEREAL_NVP_("scale.Y", getScale().Y), CEREAL_NVP_("scale.Z", getScale().Z),
            CEREAL_NVP_("initialposition.X", initialPosition.X), CEREAL_NVP_("initialposition.Y", initialPosition.Y), CEREAL_NVP_("initialposition.Z", initialPosition.Z),
            CEREAL_NVP_("initialrotation.X", initialRotation.X), CEREAL_NVP_("initialrotation.Y", initialRotation.Y), CEREAL_NVP_("initialrotation.Z", initialRotation.Z),
            CEREAL_NVP_("initialscale.X", initialScale.X), CEREAL_NVP_("initialscale.Y", initialScale.Y), CEREAL_NVP_("initialscale.Z", initialScale.Z));
        archive(
            CEREAL_NVP_("isParent", isParent), CEREAL_NVP_("isChild", isChild),
            CEREAL_NVP_("parent_name", parent_name), CEREAL_NVP_("children_names", children_names));
    }

    template <class Archive>
    void load(Archive& archive)
    {
        float px, py, pz, rx, ry, rz, sx, sy, sz;
        archive(
            CEREAL_NVP_("position.X", px), CEREAL_NVP_("position.Y", py), CEREAL_NVP_("position.Z", pz),
            CEREAL_NVP_("rotation.X", rx), CEREAL_NVP_("rotation.Y", ry), CEREAL_NVP_("rotation.Z", rz),
            CEREAL_NVP_("scale.X", sx), CEREAL_NVP_("scale.Y", sy), CEREAL_NVP_("scale.Z", sz));
        // initialposition/rotation/scale were added later; default to loaded values for old files.
        float ipx=px, ipy=py, ipz=pz, irx=rx, iry=ry, irz=rz, isx=sx, isy=sy, isz=sz;
        try {
            archive(
                CEREAL_NVP_("initialposition.X", ipx), CEREAL_NVP_("initialposition.Y", ipy), CEREAL_NVP_("initialposition.Z", ipz),
                CEREAL_NVP_("initialrotation.X", irx), CEREAL_NVP_("initialrotation.Y", iry), CEREAL_NVP_("initialrotation.Z", irz),
                CEREAL_NVP_("initialscale.X", isx), CEREAL_NVP_("initialscale.Y", isy), CEREAL_NVP_("initialscale.Z", isz));
        } catch (...) {}
        // isParent/isChild/parent_name/children_names were not serialized by the old code (bug);
        // wrap in try-catch so old scene files without these fields still load.
        try {
            archive(
                CEREAL_NVP_("isParent", isParent), CEREAL_NVP_("isChild", isChild),
                CEREAL_NVP_("parent_name", parent_name), CEREAL_NVP_("children_names", children_names));
        } catch (...) {}

        position        = irr::core::vector3df(px,  py,  pz);
        rotation        = irr::core::vector3df(rx,  ry,  rz);
        scale           = irr::core::vector3df(sx,  sy,  sz);
        initialPosition = irr::core::vector3df(ipx, ipy, ipz);
        initialRotation = irr::core::vector3df(irx, iry, irz);
        initialScale    = irr::core::vector3df(isx, isy, isz);
        // node is null at load time; setters null-check, so this just sets the deprecated members
        setPosition(position);
        setRotation(rotation);
        setScale(scale);
    }

    irr::core::vector3df getPosition() const
    {
        return node ? node->getPosition() : irr::core::vector3df();
    }
    irr::core::vector3df getInitialPosition() const
    {
        return initialPosition;
    }
    void setPosition(const irr::core::vector3df& v) /*const*/
    {
        // *** DEPRECATED ***
        position = v;

        if (node) {
            node->setPosition(v);
        }
    }
    irr::core::vector3df getRotation() const
    {
        return node ? node->getRotation() : irr::core::vector3df();
    }
    irr::core::vector3df getInitialRotation() const
    {
        return initialRotation;
    }
    void setRotation(const irr::core::vector3df& v) /*const*/
    {
        // *** DEPRECATED ***
        rotation = v;

        if (node) {
            node->setRotation(v);
        }
    }
    irr::core::vector3df getScale() const
    {
        return node ? node->getScale() : irr::core::vector3df();
    }
    irr::core::vector3df getInitialScale() const
    {
        return initialScale;
    }
    void setScale(const irr::core::vector3df& v) /*const*/
    {
        // *** DEPRECATED ***
        scale = v;

        if (node) {
            node->setScale(v);
        }
    }

	void addChild(irr::scene::ISceneNode* child_node);
	void removeChild(irr::scene::ISceneNode* child_node);

	void resetTransform()
    {
		setPosition(irr::core::vector3df(0.0f, 0.0f, 0.0f));
		setRotation(irr::core::vector3df(0.0f, 0.0f, 0.0f));
		setScale(irr::core::vector3df(1.0f, 1.0f, 1.0f));
    }

	void populateParentChildNamesForExport();
};
