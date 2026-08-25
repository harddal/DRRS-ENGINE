#include "Editor/EditorCamera.h"

#include "Engine/Engine.h"

#include "Editor/EditorViewport.h"

EditorCamera::EditorCamera() : 
    m_offset(irr::core::vector3df(0.0f, 0.0f, 0.0f)),
    m_target(irr::core::vector3df(0.0f, 0.0f, 100.0f)),
    m_lookat(irr::core::vector3df(0.0f, 0.0f, 0.0f)),
    m_camera(nullptr), m_targetNode(nullptr) {}

void EditorCamera::init()
{
    m_camera = RenderManager::Get()->sceneManager()->addCameraSceneNode();
    RenderManager::Get()->sceneManager()->setActiveCamera(m_camera);

    m_camera->setNearValue(0.01f);
    m_camera->setFarValue(1000.0f);

    m_camera->setPosition(irr::core::vector3df(0.0f, 0.0f, 0.0f));
    m_camera->setRotation(irr::core::vector3df(0.0f, 0.0f, 0.0f));

    m_targetNode = RenderManager::Get()->sceneManager()->addEmptySceneNode();
    m_targetNode->setPosition(m_target);
    m_targetNode->setParent(m_camera);

    m_camera->setPosition(
        irr::core::vector3df(0.0f, 0.0f, 0.0f) + m_offset);
    m_camera->setRotation(irr::core::vector3df(0.0f, 0.0f, 0.0f));
}

namespace
{
	// Centre of the viewport panel in DESKTOP pixels.
	//
	// With multi-viewport enabled ImGui coordinates ARE desktop coordinates, so the
	// pane origin needs no conversion — and that is already the space InputManager
	// warps in (SetCursorPos). This used to derive a client->desktop offset from the
	// cursor and add it; doing so now would double-count the window origin, and since
	// this value is also the warp target the cursor would walk further off-screen
	// every frame of a look drag.
	irr::core::vector2df viewportCenterDesktop()
	{
		const auto& pane = EditorViewport::frozen();

		const auto center = irr::core::vector2df(
			pane.pos.x + pane.size.x * 0.5f,
			pane.pos.y + pane.size.y * 0.5f);

		// Round to a whole desktop pixel. setMousePosition() ultimately hands this to
		// SetCursorPos, which only accepts integers — leaving it fractional meant the
		// per-frame delta below (warpTarget - actual cursor pos) never reached zero: the
		// panel rect is static across a drag, so the same fractional remainder got
		// truncated away and re-appeared identically every frame, applying as a constant
		// one-directional rotation each tick instead of settling once the mouse stops.
		return irr::core::vector2df(
			static_cast<float>(irr::core::round32(center.X)),
			static_cast<float>(irr::core::round32(center.Y)));
	}
}

void EditorCamera::update()
{
    const float
        pi = 3.141592741f,
        pi_180 = 0.017453293f,
        sensitivity = 0.25f,
        maxXAngle = 82.0f,
        minXAngle = -82.0f,
        strafeOffset = 1.570796370f;

	float moveSpeed = 0.15f;

    auto cameraRotation = m_camera->getRotation();

	static bool center = false, right_mouse_release = false;
	static auto old_cursor_pos = irr::core::vector2df(0.0f, 0.0f);

	// A look drag may only BEGIN over the viewport panel, but once begun it runs until
	// the button is released. The warp below deliberately parks the cursor at the panel
	// centre every frame, so testing "is the cursor over the panel" mid-drag would be
	// unreliable — and the cursor is hidden anyway.
	bool lookHeld = InputManager::Get()->isMouseButtonPressed(1, true);
	if (lookHeld && !center && !EditorViewport::acceptsSceneInput())
		lookHeld = false;

    if (lookHeld)
    {
		// ImGui's Win32 backend re-applies the OS cursor from GetMouseCursor() every
		// frame — which is what gives dock splitters their resize cursors — so hiding
		// via Irrlicht alone is not enough to keep it hidden during a look drag.
		ImGui::SetMouseCursor(ImGuiMouseCursor_None);

    	if (!center)
    	{
			center = true;

			RenderManager::Get()->device()->getCursorControl()->setVisible(false);

			old_cursor_pos = InputManager::Get()->getMousePosition();

			InputManager::Get()->setMousePosition(viewportCenterDesktop());
    	}
		else
		{
			// Deliberately not InputManager::getMouseDelta(): that re-centres on the
			// WINDOW centre and measures its delta against that same point. Once the
			// viewport is a docked panel the window centre is usually outside it, which
			// both drags the cursor away from the view and biases every delta.
			const auto warpTarget = viewportCenterDesktop();
			const auto mouseDelta = warpTarget - InputManager::Get()->getMousePosition();
			InputManager::Get()->setMousePosition(warpTarget);

			cameraRotation.Y -= mouseDelta.X * sensitivity;
			cameraRotation.X -= mouseDelta.Y * sensitivity;

			if (cameraRotation.X > maxXAngle)
				cameraRotation.X = maxXAngle;
			else
				if (cameraRotation.X < minXAngle)
					cameraRotation.X = minXAngle;

			m_camera->setRotation(cameraRotation);
		}
    }
	else
	{
		center = false;
	}
	if (InputManager::Get()->getMouseRelease(1, &right_mouse_release, true))
	{
		InputManager::Get()->setMousePosition(old_cursor_pos);
		RenderManager::Get()->device()->getCursorControl()->setVisible(true);
	}

	// Movement follows the same rule as look: allowed while the cursor is over the 3D
	// view, or unconditionally once a look drag has the mouse captured. Without this,
	// WASD would keep flying the camera while the user works in a docked panel.
	const bool acceptMovement = lookHeld || EditorViewport::acceptsSceneInput();

    float
        move = 0.0f,
        strafe = 0.0f;

	if (acceptMovement)
	{
		if (InputManager::Get()->isActionPressed("sprint"))
			moveSpeed *= 2;

		if (InputManager::Get()->isActionPressed("forward"))
			move += moveSpeed;
		if (InputManager::Get()->isActionPressed("backward"))
			move -= moveSpeed;
		if (InputManager::Get()->isActionPressed("strafel"))
			strafe -= moveSpeed;
		if (InputManager::Get()->isActionPressed("strafer"))
			strafe += moveSpeed;

		if (InputManager::Get()->isActionPressed("jump"))
			m_camera->setPosition(m_camera->getAbsolutePosition() + irr::core::vector3df(0.0f, 0.35f, 0.0f));

		if (InputManager::Get()->isActionPressed("crouch"))
			m_camera->setPosition(m_camera->getAbsolutePosition() + irr::core::vector3df(0.0f, -0.35f, 0.0f));
	}

    auto moveDirection = cameraRotation.Y * pi_180;

    m_camera->setPosition(m_camera->getAbsolutePosition() + irr::core::vector3df(
        move * sin(moveDirection) + strafe * sin(moveDirection + strafeOffset),
        move * -sin(cameraRotation.X * pi_180),
        move * cos(moveDirection) + strafe * cos(moveDirection + strafeOffset)));

    m_camera->updateAbsolutePosition();
    m_targetNode->updateAbsolutePosition();

    m_camera->setTarget(m_targetNode->getAbsolutePosition());

    m_lookat = irr::core::vector3df(
        sin(deg2rad(cameraRotation.Y)) * cos(deg2rad(cameraRotation.X)),
        -sin(deg2rad(cameraRotation.X)),
        cos(deg2rad(cameraRotation.Y)) * cos(deg2rad(cameraRotation.X)));
}

void EditorCamera::destroy()
{
    RenderManager::Get()->sceneManager()->addToDeletionQueue(m_camera);
    RenderManager::Get()->sceneManager()->addToDeletionQueue(m_targetNode);
}

void EditorCamera::reset()
{
    RenderManager::Get()->sceneManager()->setActiveCamera(m_camera);
}

irr::core::vector3df EditorCamera::getLookAt() const
{
    return m_lookat;
}
irr::core::vector3df EditorCamera::getLookAtNormalized() const
{
    auto lan = m_lookat;
    return lan.normalize();
}