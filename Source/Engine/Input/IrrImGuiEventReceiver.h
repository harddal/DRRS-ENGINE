#pragma once

#include "irrlicht.h"
#include <IMGUI/imgui.h>

// Translates Irrlicht SEvent input into ImGui IO events.
// Used as the IrrlichtDevice's event receiver so ImGui receives keyboard/mouse input.
// Always returns false so events continue propagating through Irrlicht's internal chain.
class IrrImGuiEventReceiver : public irr::IEventReceiver
{
public:
    bool OnEvent(const irr::SEvent& event) override
    {
        ImGuiIO& io = ImGui::GetIO();

        if (event.EventType == irr::EET_MOUSE_INPUT_EVENT)
        {
            const auto& m = event.MouseInput;
            switch (m.Event)
            {
            case irr::EMIE_MOUSE_MOVED:
                io.AddMousePosEvent(static_cast<float>(m.X), static_cast<float>(m.Y));
                break;
            case irr::EMIE_LMOUSE_PRESSED_DOWN: io.AddMouseButtonEvent(0, true);  break;
            case irr::EMIE_LMOUSE_LEFT_UP:      io.AddMouseButtonEvent(0, false); break;
            case irr::EMIE_RMOUSE_PRESSED_DOWN: io.AddMouseButtonEvent(1, true);  break;
            case irr::EMIE_RMOUSE_LEFT_UP:      io.AddMouseButtonEvent(1, false); break;
            case irr::EMIE_MMOUSE_PRESSED_DOWN: io.AddMouseButtonEvent(2, true);  break;
            case irr::EMIE_MMOUSE_LEFT_UP:      io.AddMouseButtonEvent(2, false); break;
            case irr::EMIE_MOUSE_WHEEL:
                io.AddMouseWheelEvent(0.0f, m.Wheel);
                break;
            default: break;
            }
        }
        else if (event.EventType == irr::EET_KEY_INPUT_EVENT)
        {
            const auto& k = event.KeyInput;
            ImGuiKey imguiKey = irrKeyToImGuiKey(k.Key);

            if (imguiKey != ImGuiKey_None)
                io.AddKeyEvent(imguiKey, k.PressedDown);

            io.AddKeyEvent(ImGuiMod_Ctrl,  k.Control);
            io.AddKeyEvent(ImGuiMod_Shift, k.Shift);

            // Track Alt via explicit key events since SKeyInput has no Alt field
            if (k.Key == irr::KEY_LMENU || k.Key == irr::KEY_RMENU)
                io.AddKeyEvent(ImGuiMod_Alt, k.PressedDown);

            if (k.PressedDown && k.Char != 0)
                io.AddInputCharacterUTF16(static_cast<ImWchar16>(k.Char));
        }

        return false;
    }

private:
    static ImGuiKey irrKeyToImGuiKey(irr::EKEY_CODE key)
    {
        // Irrlicht EKEY_CODE values match Windows Virtual Key codes
        switch (key)
        {
        case irr::KEY_BACK:      return ImGuiKey_Backspace;
        case irr::KEY_TAB:       return ImGuiKey_Tab;
        case irr::KEY_RETURN:    return ImGuiKey_Enter;
        case irr::KEY_ESCAPE:    return ImGuiKey_Escape;
        case irr::KEY_SPACE:     return ImGuiKey_Space;
        case irr::KEY_PRIOR:     return ImGuiKey_PageUp;
        case irr::KEY_NEXT:      return ImGuiKey_PageDown;
        case irr::KEY_END:       return ImGuiKey_End;
        case irr::KEY_HOME:      return ImGuiKey_Home;
        case irr::KEY_LEFT:      return ImGuiKey_LeftArrow;
        case irr::KEY_UP:        return ImGuiKey_UpArrow;
        case irr::KEY_RIGHT:     return ImGuiKey_RightArrow;
        case irr::KEY_DOWN:      return ImGuiKey_DownArrow;
        case irr::KEY_INSERT:    return ImGuiKey_Insert;
        case irr::KEY_DELETE:    return ImGuiKey_Delete;
        case irr::KEY_LWIN:      return ImGuiKey_LeftSuper;
        case irr::KEY_RWIN:      return ImGuiKey_RightSuper;
        case irr::KEY_KEY_0:     return ImGuiKey_0;
        case irr::KEY_KEY_1:     return ImGuiKey_1;
        case irr::KEY_KEY_2:     return ImGuiKey_2;
        case irr::KEY_KEY_3:     return ImGuiKey_3;
        case irr::KEY_KEY_4:     return ImGuiKey_4;
        case irr::KEY_KEY_5:     return ImGuiKey_5;
        case irr::KEY_KEY_6:     return ImGuiKey_6;
        case irr::KEY_KEY_7:     return ImGuiKey_7;
        case irr::KEY_KEY_8:     return ImGuiKey_8;
        case irr::KEY_KEY_9:     return ImGuiKey_9;
        case irr::KEY_KEY_A:     return ImGuiKey_A;
        case irr::KEY_KEY_B:     return ImGuiKey_B;
        case irr::KEY_KEY_C:     return ImGuiKey_C;
        case irr::KEY_KEY_D:     return ImGuiKey_D;
        case irr::KEY_KEY_E:     return ImGuiKey_E;
        case irr::KEY_KEY_F:     return ImGuiKey_F;
        case irr::KEY_KEY_G:     return ImGuiKey_G;
        case irr::KEY_KEY_H:     return ImGuiKey_H;
        case irr::KEY_KEY_I:     return ImGuiKey_I;
        case irr::KEY_KEY_J:     return ImGuiKey_J;
        case irr::KEY_KEY_K:     return ImGuiKey_K;
        case irr::KEY_KEY_L:     return ImGuiKey_L;
        case irr::KEY_KEY_M:     return ImGuiKey_M;
        case irr::KEY_KEY_N:     return ImGuiKey_N;
        case irr::KEY_KEY_O:     return ImGuiKey_O;
        case irr::KEY_KEY_P:     return ImGuiKey_P;
        case irr::KEY_KEY_Q:     return ImGuiKey_Q;
        case irr::KEY_KEY_R:     return ImGuiKey_R;
        case irr::KEY_KEY_S:     return ImGuiKey_S;
        case irr::KEY_KEY_T:     return ImGuiKey_T;
        case irr::KEY_KEY_U:     return ImGuiKey_U;
        case irr::KEY_KEY_V:     return ImGuiKey_V;
        case irr::KEY_KEY_W:     return ImGuiKey_W;
        case irr::KEY_KEY_X:     return ImGuiKey_X;
        case irr::KEY_KEY_Y:     return ImGuiKey_Y;
        case irr::KEY_KEY_Z:     return ImGuiKey_Z;
        case irr::KEY_NUMPAD0:   return ImGuiKey_Keypad0;
        case irr::KEY_NUMPAD1:   return ImGuiKey_Keypad1;
        case irr::KEY_NUMPAD2:   return ImGuiKey_Keypad2;
        case irr::KEY_NUMPAD3:   return ImGuiKey_Keypad3;
        case irr::KEY_NUMPAD4:   return ImGuiKey_Keypad4;
        case irr::KEY_NUMPAD5:   return ImGuiKey_Keypad5;
        case irr::KEY_NUMPAD6:   return ImGuiKey_Keypad6;
        case irr::KEY_NUMPAD7:   return ImGuiKey_Keypad7;
        case irr::KEY_NUMPAD8:   return ImGuiKey_Keypad8;
        case irr::KEY_NUMPAD9:   return ImGuiKey_Keypad9;
        case irr::KEY_MULTIPLY:  return ImGuiKey_KeypadMultiply;
        case irr::KEY_ADD:       return ImGuiKey_KeypadAdd;
        case irr::KEY_SUBTRACT:  return ImGuiKey_KeypadSubtract;
        case irr::KEY_DECIMAL:   return ImGuiKey_KeypadDecimal;
        case irr::KEY_DIVIDE:    return ImGuiKey_KeypadDivide;
        case irr::KEY_F1:        return ImGuiKey_F1;
        case irr::KEY_F2:        return ImGuiKey_F2;
        case irr::KEY_F3:        return ImGuiKey_F3;
        case irr::KEY_F4:        return ImGuiKey_F4;
        case irr::KEY_F5:        return ImGuiKey_F5;
        case irr::KEY_F6:        return ImGuiKey_F6;
        case irr::KEY_F7:        return ImGuiKey_F7;
        case irr::KEY_F8:        return ImGuiKey_F8;
        case irr::KEY_F9:        return ImGuiKey_F9;
        case irr::KEY_F10:       return ImGuiKey_F10;
        case irr::KEY_F11:       return ImGuiKey_F11;
        case irr::KEY_F12:       return ImGuiKey_F12;
        case irr::KEY_NUMLOCK:   return ImGuiKey_NumLock;
        case irr::KEY_SCROLL:    return ImGuiKey_ScrollLock;
        case irr::KEY_LSHIFT:    return ImGuiKey_LeftShift;
        case irr::KEY_RSHIFT:    return ImGuiKey_RightShift;
        case irr::KEY_LCONTROL:  return ImGuiKey_LeftCtrl;
        case irr::KEY_RCONTROL:  return ImGuiKey_RightCtrl;
        case irr::KEY_LMENU:     return ImGuiKey_LeftAlt;
        case irr::KEY_RMENU:     return ImGuiKey_RightAlt;
        case irr::KEY_CAPITAL:   return ImGuiKey_CapsLock;
        case irr::KEY_PAUSE:     return ImGuiKey_Pause;
        case irr::KEY_OEM_1:     return ImGuiKey_Semicolon;
        case irr::KEY_PLUS:      return ImGuiKey_Equal;
        case irr::KEY_COMMA:     return ImGuiKey_Comma;
        case irr::KEY_MINUS:     return ImGuiKey_Minus;
        case irr::KEY_PERIOD:    return ImGuiKey_Period;
        case irr::KEY_OEM_2:     return ImGuiKey_Slash;
        case irr::KEY_OEM_3:     return ImGuiKey_GraveAccent;
        case irr::KEY_OEM_4:     return ImGuiKey_LeftBracket;
        case irr::KEY_OEM_5:     return ImGuiKey_Backslash;
        case irr::KEY_OEM_6:     return ImGuiKey_RightBracket;
        case irr::KEY_OEM_7:     return ImGuiKey_Apostrophe;
        default:                 return ImGuiKey_None;
        }
    }
};
