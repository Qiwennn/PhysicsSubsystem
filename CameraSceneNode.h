#ifndef __PYENGINE_2_0_CAMERA_SCENE_NODE_H__
#define __PYENGINE_2_0_CAMERA_SCENE_NODE_H__

// API Abstraction
#include "PrimeEngine/APIAbstraction/APIAbstractionDefines.h"

// Outer-Engine includes
#include <assert.h>

// Inter-Engine includes
#include "PrimeEngine/Render/IRenderer.h"
#include "PrimeEngine/MemoryManagement/Handle.h"
#include "PrimeEngine/PrimitiveTypes/PrimitiveTypes.h"
#include "../Events/Component.h"
#include "../Utils/Array/Array.h"
#include "PrimeEngine/Math/Plane.h"
#include "PrimeEngine/Math/CameraOps.h"

#include "SceneNode.h"

namespace PE {
namespace Components {

struct CameraSceneNode : public SceneNode
{
	PE_DECLARE_CLASS(CameraSceneNode);

	// Construction ------------------------------------------------------------
	CameraSceneNode(PE::GameContext &context, PE::MemoryArena arena, Handle hMyself);
	virtual ~CameraSceneNode() {}

	// Component ---------------------------------------------------------------
	virtual void addDefaultComponents();

	PE_DECLARE_IMPLEMENT_EVENT_HANDLER_WRAPPER(do_CALCULATE_TRANSFORMATIONS);
	virtual void do_CALCULATE_TRANSFORMATIONS(Events::Event *pEvt);

	// Data --------------------------------------------------------------------
	Matrix4x4 m_worldTransform2;
	Matrix4x4 m_worldToViewTransform;   // world -> view (camera space)
	Matrix4x4 m_worldToViewTransform2;
	Matrix4x4 m_viewToProjectedTransform; // view -> clip (projection)
	float m_near, m_far;

	Plane m_frustumPlanes[6];
};

}; // namespace Components
}; // namespace PE

#endif
