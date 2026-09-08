#include "Engine/Input/InputManager.h"

#include "Engine/Input/InputMap.h"
#include "Utility/Utility.h"

#include <fstream>

#include <Windows.h>
#include <spdlog/spdlog.h>
#include <IMGUI/imgui.h>

#include "Engine/Engine.h"

InputManager* InputManager::s_Instance = nullptr;

std::array<const char*, KEY_KEYCOUNT + 2> KEYBOARD_KEY_STRING =
{
	"NONE",
	"A",
	"B",
	"C",
	"D",
	"E",
	"F",
	"G",
	"H",
	"I",
	"J",
	"K",
	"L",
	"M",
	"N",
	"O",
	"P",
	"Q",
	"R",
	"S",
	"T",
	"U",
	"V",
	"W",
	"X",
	"Y",
	"Z",
	"0",
	"1",
	"2",
	"3",
	"4",
	"5",
	"6",
	"7",
	"8",
	"9",
	"ESCAPE",
	"LCONTROL",
	"LSHIFT",
	"LALT",
	"LSYSTEM",
	"RCONTROL",
	"RSHIFT",
	"RALT",
	"RSYSTEM",
	"MENU",
	"LBRACKET",
	"RBRACKET",
	"SEMICOLON",
	"COMMA",
	"PERIOD",
	"QUOTE",
	"SLASH",
	"BACKSLASH",
	"TILDE",
	"EQUAL",
	"DASH",
	"SPACE",
	"RETURN",
	"BACKSPACE",
	"TAB",
	"PAGEUP",
	"PAGEDOWN",
	"END",
	"HOME",
	"INSERT",
	"DELETE",
	"ADD",
	"SUBTRACT",
	"MULTIPLY",
	"DIVIDE",
	"LEFT",
	"RIGHT",
	"UP",
	"DOWN",
	"NUMPAD0",
	"NUMPAD1",
	"NUMPAD2",
	"NUMPAD3",
	"NUMPAD4",
	"NUMPAD5",
	"NUMPAD6",
	"NUMPAD7",
	"NUMPAD8",
	"NUMPAD9",
	"F1",
	"F2",
	"F3",
	"F4",
	"F5",
	"F6",
	"F7",
	"F8",
	"F9",
	"F10",
	"F11",
	"F12",
	"F13",
	"F14",
	"F15",
	"PAUSE",
	"KEYCOUNT"
};

namespace
{
	// Is any window of THIS PROCESS the foreground window?
	//
	// Not device()->isWindowFocused(): that is GetFocus() == the main Irrlicht HWND, so
	// it goes false the moment a torn-off ImGui panel is clicked, and all key/button
	// polling below would stop dead while the app is plainly still in use.
	//
	// Deliberately a pure focus test. The "don't act on keys while typing" guard is a
	// separate mechanism — Engine::update() passes !io.WantTextInput into
	// InputManager::update(), which drives m_canProcessInput — and folding it in here
	// would also swallow mouse buttons whenever a text field happened to be focused.
	bool anyAppWindowFocused()
	{
		const HWND foreground = ::GetForegroundWindow();
		if (!foreground)
			return false;

		DWORD pid = 0;
		::GetWindowThreadProcessId(foreground, &pid);
		return pid == ::GetCurrentProcessId();
	}
}

InputManager::InputManager() :
	m_canProcessInput(true), m_blockMouseInput(false), m_wheelDelta(0.f), m_frameWheelDelta(0.f),
	m_xSensitivity(0.f), m_ySensitivity(0.f),
	m_fixedMousePosition(0, 0)
{
	if (s_Instance)
	{
		Utility::Error("Pointer to class \'InputManager\' is invalid");
	}
	s_Instance = this;

	try
	{
		std::ifstream ifs_input("config/input.xml");
		cereal::XMLInputArchive input_config(ifs_input);

		input_config(m_configuration);
	}
	catch (cereal::Exception& ex)
	{
		spdlog::warn("Failed to load input configuration: {}, default values used", ex.what());

		m_configuration = InputConfiguration();

		std::ofstream ofs_input("config/input.xml");
		cereal::XMLOutputArchive input_config(ofs_input);

		input_config(m_configuration);
	}

	m_keyActionPairList.emplace_back(sKeyActionPair("forward", m_configuration.key_forward));
	m_keyActionPairList.emplace_back(sKeyActionPair("backward", m_configuration.key_backward));
	m_keyActionPairList.emplace_back(sKeyActionPair("strafel", m_configuration.key_strafel));
	m_keyActionPairList.emplace_back(sKeyActionPair("strafer", m_configuration.key_strafer));
	m_keyActionPairList.emplace_back(sKeyActionPair("jump", m_configuration.key_jump));
	m_keyActionPairList.emplace_back(sKeyActionPair("crouch", m_configuration.key_crouch));
	m_keyActionPairList.emplace_back(sKeyActionPair("sprint", m_configuration.key_sprint));
	m_keyActionPairList.emplace_back(sKeyActionPair("use", m_configuration.key_use));
	m_keyActionPairList.emplace_back(sKeyActionPair("reload", m_configuration.key_reload));
	m_keyActionPairList.emplace_back(sKeyActionPair("firemode", m_configuration.key_firemode));
	m_keyActionPairList.emplace_back(sKeyActionPair("inventory", m_configuration.key_inventory));
	m_keyActionPairList.emplace_back(sKeyActionPair("primary", m_configuration.key_primary));
	m_keyActionPairList.emplace_back(sKeyActionPair("secondary", m_configuration.key_secondary));

	m_xSensitivity = m_configuration.xsens;
	m_ySensitivity = m_configuration.ysens;
}


void InputManager::update(bool processInput)
{
	m_frameWheelDelta = m_wheelDelta;
	m_wheelDelta = 0.f;

	// centerMouse() feeds this to SetCursorPos, which takes DESKTOP coordinates, but
	// the configuration holds the window's CLIENT size. Treating one as the other only
	// happened to work while the window sat at the top-left of the primary monitor;
	// convert properly so it is right on any monitor and at any window position.
	{
		POINT center = {
			static_cast<LONG>(RenderManager::Get()->getConfiguration().width  / 2),
			static_cast<LONG>(RenderManager::Get()->getConfiguration().height / 2)
		};

		if (HWND hwnd = static_cast<HWND>(RenderManager::Get()->mainWindowHandle()))
			::ClientToScreen(hwnd, &center);

		m_fixedMousePosition = irr::core::vector2df(
			static_cast<float>(center.x), static_cast<float>(center.y));
	}

	if (!m_rawInputAttempted)
		registerRawMouseInput(RenderManager::Get()->mainWindowHandle());

	// Mouse-look cursor policy.
	//
	// "Engaged" means the OS cursor is parked on the anchor and clipped there, so it
	// can neither wander out of the window nor drift over an ImGui panel. Motion
	// itself comes from WM_INPUT (see getMouseDelta), so nothing is warped per frame.
	//
	// Demand is inferred from getMouseDelta() having been called rather than from an
	// explicit begin/end pair: update() and the game logic that reads the delta run in
	// the same fixed step, so a consumer that stops looking — player locked, inventory
	// open, console open, state change, editor RMB released — releases the cursor on
	// the next step with no bookkeeping at the call site, and no path can leave it
	// clipped forever.
	if (m_rawInputRegistered)
	{
		// Gated on the MAIN window specifically, not anyAppWindowFocused(): raw input
		// is registered against that window without RIDEV_INPUTSINK, so when one of
		// the editor's torn-off tool windows holds focus no WM_INPUT arrives. Pinning
		// the cursor there would trap it against a camera that cannot turn.
		const bool foreground =
			::GetForegroundWindow() == static_cast<HWND>(RenderManager::Get()->mainWindowHandle());

		const bool wantMouseLook = m_mouseLookDemanded && foreground;
		m_mouseLookDemanded = false;

		if (wantMouseLook)
		{
			engageMouseLook();
		}
		else
		{
			// Drop whatever arrived while nobody was looking, so resuming a look does
			// not apply a single accumulated burst of motion.
			m_rawMouseAccum = irr::core::vector2df(0, 0);
			releaseMouseLook();
		}
	}

	m_canProcessInput = processInput;

	if (m_canProcessInput)
	{
		for (auto& pair : m_keyActionPairList)
		{
			pair.pressed = isKeyPressed(KEYBOARD_KEY(pair.getKey()));
			pair.released = getKeyRelease(KEYBOARD_KEY(pair.getKey()), &pair.toggle);
		}
	}
}

bool InputManager::isActionPressed(std::string action)
{
	if (m_canProcessInput)
	{
		for (auto& pair : m_keyActionPairList)
		{
			if (pair.getAction() == action)
			{
				return pair.pressed;
			}
		}
	}

	return false;
}

bool InputManager::isActionReleased(std::string action)
{
	if (m_canProcessInput)
	{
		for (auto& pair : m_keyActionPairList)
		{
			if (pair.getAction() == action)
			{
				return pair.released;
			}
		}
	}

	return false;
}

bool InputManager::isKeyPressed(int key, bool ignore_process_flag)
{
	auto keycode = 0;

	switch (key)
	{
	default: keycode = 0;
		break;
	case KEY_A: keycode = 'A';
		break;
	case KEY_B: keycode = 'B';
		break;
	case KEY_C: keycode = 'C';
		break;
	case KEY_D: keycode = 'D';
		break;
	case KEY_E: keycode = 'E';
		break;
	case KEY_F: keycode = 'F';
		break;
	case KEY_G: keycode = 'G';
		break;
	case KEY_H: keycode = 'H';
		break;
	case KEY_I: keycode = 'I';
		break;
	case KEY_J: keycode = 'J';
		break;
	case KEY_K: keycode = 'K';
		break;
	case KEY_L: keycode = 'L';
		break;
	case KEY_M: keycode = 'M';
		break;
	case KEY_N: keycode = 'N';
		break;
	case KEY_O: keycode = 'O';
		break;
	case KEY_P: keycode = 'P';
		break;
	case KEY_Q: keycode = 'Q';
		break;
	case KEY_R: keycode = 'R';
		break;
	case KEY_S: keycode = 'S';
		break;
	case KEY_T: keycode = 'T';
		break;
	case KEY_U: keycode = 'U';
		break;
	case KEY_V: keycode = 'V';
		break;
	case KEY_W: keycode = 'W';
		break;
	case KEY_X: keycode = 'X';
		break;
	case KEY_Y: keycode = 'Y';
		break;
	case KEY_Z: keycode = 'Z';
		break;
	case KEY_NUM0: keycode = '0';
		break;
	case KEY_NUM1: keycode = '1';
		break;
	case KEY_NUM2: keycode = '2';
		break;
	case KEY_NUM3: keycode = '3';
		break;
	case KEY_NUM4: keycode = '4';
		break;
	case KEY_NUM5: keycode = '5';
		break;
	case KEY_NUM6: keycode = '6';
		break;
	case KEY_NUM7: keycode = '7';
		break;
	case KEY_NUM8: keycode = '8';
		break;
	case KEY_NUM9: keycode = '9';
		break;
	case KEY_ESCAPE: keycode = VK_ESCAPE;
		break;
	case KEY_LCONTROL: keycode = VK_LCONTROL;
		break;
	case KEY_LSHIFT: keycode = VK_LSHIFT;
		break;
	case KEY_LALT: keycode = VK_LMENU;
		break;
	case KEY_LSYSTEM: keycode = VK_LWIN;
		break;
	case KEY_RCONTROL: keycode = VK_RCONTROL;
		break;
	case KEY_RSHIFT: keycode = VK_RSHIFT;
		break;
	case KEY_RALT: keycode = VK_RMENU;
		break;
	case KEY_RSYSTEM: keycode = VK_RWIN;
		break;
	case KEY_MENU: keycode = VK_APPS;
		break;
	case KEY_LBRACKET: keycode = VK_OEM_4;
		break;
	case KEY_RBRACKET: keycode = VK_OEM_6;
		break;
	case KEY_SEMICOLON: keycode = VK_OEM_1;
		break;
	case KEY_COMMA: keycode = VK_OEM_COMMA;
		break;
	case KEY_PERIOD: keycode = VK_OEM_PERIOD;
		break;
	case KEY_QUOTE: keycode = VK_OEM_7;
		break;
	case KEY_SLASH: keycode = VK_OEM_2;
		break;
	case KEY_BACKSLASH: keycode = VK_OEM_5;
		break;
	case KEY_TILDE: keycode = VK_OEM_3;
		break;
	case KEY_EQUAL: keycode = VK_OEM_PLUS;
		break;
	case KEY_DASH: keycode = VK_OEM_MINUS;
		break;
	case KEY_SPACE: keycode = VK_SPACE;
		break;
	case KEY_RETURN: keycode = VK_RETURN;
		break;
	case KEY_BACKSPACE: keycode = VK_BACK;
		break;
	case KEY_TAB: keycode = VK_TAB;
		break;
	case KEY_PAGEUP: keycode = VK_PRIOR;
		break;
	case KEY_PAGEDOWN: keycode = VK_NEXT;
		break;
	case KEY_END: keycode = VK_END;
		break;
	case KEY_HOME: keycode = VK_HOME;
		break;
	case KEY_INSERT: keycode = VK_INSERT;
		break;
	case KEY_DELETE: keycode = VK_DELETE;
		break;
	case KEY_ADD: keycode = VK_ADD;
		break;
	case KEY_SUBTRACT: keycode = VK_SUBTRACT;
		break;
	case KEY_MULTIPLY: keycode = VK_MULTIPLY;
		break;
	case KEY_DIVIDE: keycode = VK_DIVIDE;
		break;
	case KEY_LEFT: keycode = VK_LEFT;
		break;
	case KEY_RIGHT: keycode = VK_RIGHT;
		break;
	case KEY_UP: keycode = VK_UP;
		break;
	case KEY_DOWN: keycode = VK_DOWN;
		break;
	case KEY_NUMPAD0: keycode = VK_NUMPAD0;
		break;
	case KEY_NUMPAD1: keycode = VK_NUMPAD1;
		break;
	case KEY_NUMPAD2: keycode = VK_NUMPAD2;
		break;
	case KEY_NUMPAD3: keycode = VK_NUMPAD3;
		break;
	case KEY_NUMPAD4: keycode = VK_NUMPAD4;
		break;
	case KEY_NUMPAD5: keycode = VK_NUMPAD5;
		break;
	case KEY_NUMPAD6: keycode = VK_NUMPAD6;
		break;
	case KEY_NUMPAD7: keycode = VK_NUMPAD7;
		break;
	case KEY_NUMPAD8: keycode = VK_NUMPAD8;
		break;
	case KEY_NUMPAD9: keycode = VK_NUMPAD9;
		break;
	case KEY_F1: keycode = VK_F1;
		break;
	case KEY_F2: keycode = VK_F2;
		break;
	case KEY_F3: keycode = VK_F3;
		break;
	case KEY_F4: keycode = VK_F4;
		break;
	case KEY_F5: keycode = VK_F5;
		break;
	case KEY_F6: keycode = VK_F6;
		break;
	case KEY_F7: keycode = VK_F7;
		break;
	case KEY_F8: keycode = VK_F8;
		break;
	case KEY_F9: keycode = VK_F9;
		break;
	case KEY_F10: keycode = VK_F10;
		break;
	case KEY_F11: keycode = VK_F11;
		break;
	case KEY_F12: keycode = VK_F12;
		break;
	case KEY_F13: keycode = VK_F13;
		break;
	case KEY_F14: keycode = VK_F14;
		break;
	case KEY_F15: keycode = VK_F15;
		break;
	case KEY_PAUSE: keycode = VK_PAUSE;
		break;
	case KEY_MOUSE0: keycode = 0xFFF1;
		break;
	case KEY_MOUSE1: keycode = 0xFFF2;
		break;
	case KEY_MOUSE_MID: keycode = 0xFFF3;
		break;
	case KEY_MOUSE_X1: keycode = 0xFFF4;
		break;
	case KEY_MOUSE_X2: keycode = 0xFFF5;
		break;
	}

	if (anyAppWindowFocused())
	{
		switch (keycode)
		{
		case 0xFFF1:
			return m_canProcessInput || ignore_process_flag ? isMouseButtonPressed(0) : false;
		case 0xFFF2:
			return m_canProcessInput || ignore_process_flag ? isMouseButtonPressed(1) : false;
		case 0xFFF3:
			return m_canProcessInput || ignore_process_flag ? isMouseButtonPressed(2) : false;
		case 0xFFF4:
			return m_canProcessInput || ignore_process_flag ? isMouseButtonPressed(3) : false;
		case 0xFFF5:
			return m_canProcessInput || ignore_process_flag ? isMouseButtonPressed(4) : false;
		default:
			return m_canProcessInput || ignore_process_flag ? (GetAsyncKeyState(keycode) & 0x8000) > 0 : false;
		}
	}
	else
		return false;
}


bool InputManager::getKeyPressOnce(int key, bool* state, bool ignore_process_flag)
{
	if (m_canProcessInput || ignore_process_flag)
	{
		if (isKeyPressed(KEYBOARD_KEY(key), ignore_process_flag) && !*state)
		{
			*state = true;
			return true;
		}

		if (!isKeyPressed(KEYBOARD_KEY(key), ignore_process_flag) && *state)
		{
			*state = false;
			return false;
		}
	}

	return false;
}

bool InputManager::getKeyRelease(int key, bool* state, bool ignore_process_flag)
{
	if (m_canProcessInput || ignore_process_flag)
	{
		if (isKeyPressed(KEYBOARD_KEY(key), ignore_process_flag))
		{
			*state = true;
			return false;
		}

		if (!isKeyPressed(KEYBOARD_KEY(key), ignore_process_flag) && *state)
		{
			*state = false;
			return true;
		}
	}

	return false;
}

bool InputManager::getMousePressOnce(int button, bool* state, bool ignore_process_flag)
{
	if (m_canProcessInput || ignore_process_flag)
	{
		if (isMouseButtonPressed(MOUSE_BUTTON(button), ignore_process_flag) && !*state)
		{
			*state = true;
			return true;
		}

		if (!isMouseButtonPressed(MOUSE_BUTTON(button), ignore_process_flag) && *state)
		{
			*state = false;
			return false;
		}
	}

	return false;
}

bool InputManager::getMouseRelease(int button, bool* state, bool ignore_process_flag)
{
	if (m_canProcessInput || ignore_process_flag)
	{
		if (isMouseButtonPressed(MOUSE_BUTTON(button), ignore_process_flag))
		{
			*state = true;
			return false;
		}

		if (!isMouseButtonPressed(MOUSE_BUTTON(button), ignore_process_flag) && *state)
		{
			*state = false;
			return true;
		}
	}

	return false;
}

#undef MB_RIGHT
bool InputManager::isMouseButtonPressed(int button, bool ignore_process_flag)
{
	auto button_code = 0;

	switch (button)
	{
	case MB_LEFT: button_code = GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON;
		break;
	case MB_RIGHT: button_code = GetSystemMetrics(SM_SWAPBUTTON) ? VK_LBUTTON : VK_RBUTTON;
		break;
	case MB_MIDDLE: button_code = VK_MBUTTON;
		break;
	case MB_XBUTTON1: button_code = VK_XBUTTON1;
		break;
	case MB_XBUTTON2: button_code = VK_XBUTTON2;
		break;
	default: button_code = 0;
		break;
	}

	if (m_blockMouseInput && !ignore_process_flag)
	{
		return false;
	}

	// An in-game debug/UI panel (F2 viewmodel tuner, console, stats, inventory) is
	// under the cursor -> let ImGui have the click, don't fire the weapon. Gamemode
	// only: in the editor WantCaptureMouse is permanently true under the fullscreen
	// host window (see Editor/VegetationPainter.cpp), which would kill scene mouse input.
	if (!ignore_process_flag
		&& Engine::Get()->isGameMode()
		&& ImGui::GetIO().WantCaptureMouse)
	{
		return false;
	}

	if (anyAppWindowFocused())
	{
		return (m_canProcessInput || ignore_process_flag) ? (GetAsyncKeyState(button_code) & 0x8000) != 0 : false;
	}
	else
		return false;
}

// ---------------------------------------------------------------------------
// Relative mouse look
//
// This used to be "read GetCursorPos, subtract it from the window centre, then
// SetCursorPos back to the centre". That measures motion only as long as WE own
// the cursor position. Anything that asserts an ABSOLUTE position every packet
// wins the race and the recentring never lands: Sunshine/Moonlight with
// "optimize mouse for remote desktop" enabled, an RDP session, a tablet, some
// VM guest tools. The cursor then sits at a fixed offset from the centre and the
// same non-zero delta is read every single frame -- pitch runs to its clamp and
// yaw spins forever.
//
// Raw input reports what the device did instead of where the cursor ended up, so
// nothing has to be warped per frame. That also stops the game fighting drag
// tools (Win+Shift+S was unusable while the game had focus) and removes the
// SetCursorPos-per-frame cost.
//
// Absolute-position devices still report absolute values through WM_INPUT, so
// onRawMouseInput() differences successive positions for those. Both paths end
// up in the same accumulator.
// ---------------------------------------------------------------------------

void InputManager::registerRawMouseInput(void* hwnd)
{
	if (m_rawInputAttempted || !hwnd)
		return;

	m_rawInputAttempted = true;

	RAWINPUTDEVICE rid = {};
	rid.usUsagePage = 0x01;   // HID_USAGE_PAGE_GENERIC
	rid.usUsage     = 0x02;   // HID_USAGE_GENERIC_MOUSE
	rid.dwFlags     = 0;      // deliberately not RIDEV_INPUTSINK: only look while foreground
	rid.hwndTarget  = static_cast<HWND>(hwnd);

	if (::RegisterRawInputDevices(&rid, 1, sizeof(rid)))
	{
		m_rawInputRegistered = true;
	}
	else
	{
		spdlog::error("InputManager: RegisterRawInputDevices failed (error {}) — "
		              "falling back to warp-to-centre mouse look", ::GetLastError());
	}
}

void InputManager::onRawMouseInput(void* rawInputHandle)
{
	RAWINPUT ri;
	UINT     size = sizeof(ri);

	if (::GetRawInputData(static_cast<HRAWINPUT>(rawInputHandle), RID_INPUT,
	                      &ri, &size, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1))
		return;

	if (ri.header.dwType != RIM_TYPEMOUSE)
		return;

	const RAWMOUSE& mouse = ri.data.mouse;

	float dx = 0.f;
	float dy = 0.f;

	// MOUSE_MOVE_RELATIVE is 0, so the ABSOLUTE bit is the one worth testing.
	if (mouse.usFlags & MOUSE_MOVE_ABSOLUTE)
	{
		// A position normalised to 0..65535 across the screen, not a movement.
		// Differencing successive reports is the only way to recover motion.
		const bool virtualDesktop = (mouse.usFlags & MOUSE_VIRTUAL_DESKTOP) != 0;

		const int width  = ::GetSystemMetrics(virtualDesktop ? SM_CXVIRTUALSCREEN : SM_CXSCREEN);
		const int height = ::GetSystemMetrics(virtualDesktop ? SM_CYVIRTUALSCREEN : SM_CYSCREEN);

		const long absX = static_cast<long>((mouse.lLastX / 65535.0f) * width);
		const long absY = static_cast<long>((mouse.lLastY / 65535.0f) * height);

		if (m_hasAbsoluteBaseline)
		{
			dx = static_cast<float>(absX - m_lastAbsX);
			dy = static_cast<float>(absY - m_lastAbsY);
		}

		m_lastAbsX = absX;
		m_lastAbsY = absY;
		m_hasAbsoluteBaseline = true;

		// NOTE: the baseline is deliberately NOT cleared when a relative packet
		// arrives. An absolute device carries its own pointer position, which a
		// local mouse moving relatively does not disturb.
	}
	else
	{
		dx = static_cast<float>(mouse.lLastX);
		dy = static_cast<float>(mouse.lLastY);
	}

	// Sign flip: every caller was written against the old (centre - cursor) delta,
	// which is NEGATIVE when the mouse moves right/down. Matching it here keeps the
	// existing sensitivity values and the `-=` accumulation at each call site valid.
	m_rawMouseAccum.X -= dx;
	m_rawMouseAccum.Y -= dy;
}

void InputManager::onFocusChanged(bool focused)
{
	// Drop what is queued and re-baseline. A stream or remote session keeps moving
	// its pointer while we are not looking, so the first absolute report after focus
	// returns would otherwise difference against a stale position and snap the camera.
	m_rawMouseAccum       = irr::core::vector2df(0, 0);
	m_hasAbsoluteBaseline = false;
	m_mouseLookDemanded   = false;

	if (!focused)
		releaseMouseLook();
}

void InputManager::engageMouseLook()
{
	const irr::core::vector2df anchor = m_hasCustomAnchor ? m_mouseLookAnchor : m_fixedMousePosition;

	if (!m_mouseLookEngaged)
	{
		// Park it once, on entry. Everything that reads the cursor position during a
		// look — ImGui hit-testing, the WantCaptureMouse guard in isMouseButtonPressed
		// — then sees exactly what the old scheme gave it: a stationary cursor sitting
		// on the anchor. Without this the hidden cursor would wander over a HUD panel
		// and silently swallow the fire button.
		setMousePosition(anchor);
		m_mouseLookEngaged = true;
	}

	// Reassert every step: the clip rectangle is a global, shared resource and the
	// system drops it whenever another window takes the foreground.
	const LONG x = irr::core::round32(anchor.X);
	const LONG y = irr::core::round32(anchor.Y);

	RECT pin = { x, y, x + 1, y + 1 };
	::ClipCursor(&pin);
}

void InputManager::releaseMouseLook()
{
	if (!m_mouseLookEngaged)
		return;

	m_mouseLookEngaged = false;
	m_hasCustomAnchor  = false;

	::ClipCursor(nullptr);
}

irr::core::vector2df InputManager::getMouseDelta(bool ignore_process_flag)
{
	if (!m_canProcessInput && !ignore_process_flag)
		return irr::core::vector2df(0, 0);

	// Fallback for the (never yet observed) case where raw input could not be
	// registered: the old warp-to-centre measurement. Wrong under an absolute-position
	// input stream, but better than a camera that cannot turn at all.
	if (!m_rawInputRegistered)
	{
		const auto delta = m_fixedMousePosition - getMousePosition();
		centerMouse();
		return delta;
	}

	m_mouseLookDemanded = true;

	const auto delta = m_rawMouseAccum;
	m_rawMouseAccum = irr::core::vector2df(0, 0);

	return delta;
}

irr::core::vector2df InputManager::getMousePosition()
{
	POINT point;
	GetCursorPos(&point);
	return irr::core::vector2df(static_cast<float>(point.x), static_cast<float>(point.y));
}

irr::core::vector2df InputManager::getMousePositionWindow(/*HWND window*/)
{
	return irr::core::vector2df();
}

void InputManager::setMousePosition(irr::core::vector2df position)
{
	// Round, don't truncate: a caller warping to a fractional pixel (e.g. a panel
	// centre) and then measuring delta against the actual post-warp position needs
	// SetCursorPos to land as close to that value as an integer allows, or the
	// truncated remainder reappears identically every frame as a constant bias.
	SetCursorPos(irr::core::round32(position.X), irr::core::round32(position.Y));
}

void InputManager::setMousePositionWindow(irr::core::vector2df position/*, HWND relativeTo*/)
{
	// TODO: Set mouse position in active window
}

void InputManager::centerMouse()
{
	setMousePosition(m_fixedMousePosition);
}

bool InputManager::canProcessInput(bool process)
{
	m_canProcessInput = process;
	return m_canProcessInput;
}

bool InputManager::blockMouseInput(bool process)
{
	m_blockMouseInput = process;
	return m_blockMouseInput;
}

void InputManager::saveConfiguration(InputConfiguration configuration)
{
	std::ofstream ofs_input("config/input.xml");
	cereal::XMLOutputArchive input_config(ofs_input);

	m_configuration = configuration;
	input_config(configuration);
}
