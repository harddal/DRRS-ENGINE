// ENGINE FORK #6 - application-supplied skeletal pose.
// Not part of upstream Irrlicht 1.8.5. See Include/irrlicht/PATCHES.md.

#ifndef __S_JOINT_POSE_H_INCLUDED__
#define __S_JOINT_POSE_H_INCLUDED__

#include "vector3d.h"
#include "quaternion.h"

namespace irr
{
namespace scene
{

	//! One joint's LOCAL transform, in exactly the representation CSkinnedMesh
	//! already stores internally (SJoint::Animatedposition / Animatedrotation /
	//! Animatedscale).
	/** Deliberately TRS with a real quaternion rather than a matrix4: an
	application that blends poses must slerp rotations, and a matrix cannot be
	slerped. It also avoids the Euler round-trip that EJUOR_CONTROL forces
	through IBoneSceneNode::setRotation(), which is ambiguous at pitch +/-90.

	Lives in its own header so IAnimatedMeshSceneNode.h can name the type
	without including ISkinnedMesh.h (and with it SSkinMeshBuffer/S3DVertex). */
	struct SJointPose
	{
		SJointPose() : Position(0.f,0.f,0.f), Scale(1.f,1.f,1.f) {}

		core::vector3df  Position;
		core::quaternion Rotation;   // default-constructs to identity
		core::vector3df  Scale;
	};

} // end namespace scene
} // end namespace irr

#endif
