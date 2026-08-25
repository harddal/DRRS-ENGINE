#pragma once

#include "irrlicht.h"
#include <IMGUI/imgui.h>
#include "Engine/Input/InputManager.h"

// Feeds ImGui the two things imgui_impl_win32 cannot get on its own, and nothing else.
//
// The Win32 backend now owns mouse position, buttons, keys, modifiers and focus for
// every window — including torn-off multi-viewport panels, which never produce
// Irrlicht events at all. Re-sending any of that from here would deliver it twice.
//
// What remains:
//  - Typed characters for the MAIN window. Irrlicht's message pump deliberately skips
//    TranslateMessage for its own window to protect its deadkey handling, so no WM_CHAR
//    is ever generated for it and the backend's character path cannot fire. Irrlicht
//    decodes the character itself into KeyInput.Char, which is what gets forwarded here.
//    (Torn-off panels are translated normally — see CIrrDeviceWin32::handleSystemMessages
//    and PATCHES.md — so their characters come from the backend and are not duplicated.)
//  - Wheel deltas for InputManager, which has no other source for them.
//
// Always returns false so events continue propagating through Irrlicht's internal chain.
class IrrImGuiEventReceiver : public irr::IEventReceiver
{
public:
    bool OnEvent(const irr::SEvent& event) override
    {
        if (event.EventType == irr::EET_MOUSE_INPUT_EVENT)
        {
            const auto& m = event.MouseInput;
            if (m.Event == irr::EMIE_MOUSE_WHEEL && InputManager::Get())
                InputManager::Get()->accumulateWheelDelta(m.Wheel);
        }
        else if (event.EventType == irr::EET_KEY_INPUT_EVENT)
        {
            const auto& k = event.KeyInput;
            if (k.PressedDown && k.Char != 0)
                ImGui::GetIO().AddInputCharacterUTF16(static_cast<ImWchar16>(k.Char));
        }

        return false;
    }

};
