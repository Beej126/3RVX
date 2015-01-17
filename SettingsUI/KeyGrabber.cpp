#include "KeyGrabber.h"

#include <string>

KeyGrabber *KeyGrabber::instance = NULL;

KeyGrabber *KeyGrabber::Instance() {
    if (instance == NULL) {
        instance = new KeyGrabber();
    }

    return instance;
}

bool KeyGrabber::Hook() {
    _mouseHook = SetWindowsHookEx(WH_MOUSE_LL,
        LowLevelMouseProc, NULL, NULL);

    _keyHook = SetWindowsHookEx(WH_KEYBOARD_LL,
        LowLevelKeyboardProc, NULL, NULL);

    return _mouseHook && _keyHook;
}

bool KeyGrabber::Unhook() {
    BOOL unMouse = UnhookWindowsHookEx(_mouseHook);
    BOOL unKey = UnhookWindowsHookEx(_keyHook);
    return unMouse && unKey;
}

void KeyGrabber::Grab(HWND hwnd) {
    _updateHwnd = hwnd;
    Hook();
}

bool KeyGrabber::IsModifier(DWORD vk) {
    switch (vk) {
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_LWIN:
    case VK_RWIN:
        return true;
    }
    return false;
}

int KeyGrabber::Modifiers() {
    int mods = 0;
    mods += (GetAsyncKeyState(VK_MENU) & 0x8000) << 1;
    mods += (GetAsyncKeyState(VK_CONTROL) & 0x8000) << 2;
    mods += (GetAsyncKeyState(VK_SHIFT) & 0x8000) << 3;
    mods += (GetAsyncKeyState(VK_LWIN) & 0x8000) << 4;
    mods += (GetAsyncKeyState(VK_RWIN) & 0x8000) << 4;
    return mods;
}

std::wstring KeyGrabber::ModString(int modifiers) {
    std::wstring str = L"";
    if (modifiers & HKM_MOD_ALT) {
        str += VKToString(VK_MENU) + L" + ";
    }
    if (modifiers & HKM_MOD_CTRL) {
        str += VKToString(VK_CONTROL) + L" + ";
    }
    if (modifiers & HKM_MOD_SHF) {
        str += VKToString(VK_SHIFT) + L" + ";
    }
    if (modifiers & HKM_MOD_WIN) {
        str += L"Win + ";
    }

    return str;
}

std::wstring KeyGrabber::VKToString(unsigned int vk, bool extendedKey) {
    int extended = extendedKey ? 0x1 : 0x0;

    unsigned int scanCode = MapVirtualKey(vk, MAPVK_VK_TO_VSC);
    scanCode = scanCode << 16;
    scanCode |= extended << 24;
    wchar_t buf[256] = {};
    GetKeyNameText(scanCode, buf, 256);
    return std::wstring(buf);
}

LRESULT CALLBACK
KeyGrabber::KeyProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode < 0) {
        return CallNextHookEx(NULL, nCode, wParam, lParam);
    }

    if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
        KBDLLHOOKSTRUCT *kbInfo = (KBDLLHOOKSTRUCT *) lParam;

        DWORD vk = kbInfo->vkCode;
        if (IsModifier(kbInfo->vkCode)) {
            /* Ignore modifier keys */
            return CallNextHookEx(NULL, nCode, wParam, lParam);
        }

        /* GetKeyNameText expects the following:
         * 16-23: scan code
         *    24: extended key flag
         *    25: 'do not care' bit (don't distinguish between L/R keys) */
        BOOL dontCare = TRUE;
        LONG newlParam;
        newlParam = (kbInfo->scanCode << 16);
        newlParam |= (kbInfo->flags & 0x1) << 24;
        newlParam |= dontCare << 25;
        if (kbInfo->vkCode == VK_RSHIFT) {
            /* For some reason, the right shift key ends up having its extended
             * key flag set and then prints the wrong thing. This doesn't matter
             * here, but we'll fix it in case we need this info later. */
            newlParam ^= 0x1000000;
        }

        wchar_t buf[256] = {};
        GetKeyNameText(newlParam, buf, 256);
        int mods = HotkeyManager::ModifiersAsync();
        std::wstring modStr = ModString(mods) + buf;

        if (vk == VK_ESCAPE && mods == 0) {
            /* Pass escape through to let the user cancel the operation */
            return CallNextHookEx(NULL, nCode, wParam, lParam);
        }

        SetWindowText(_updateHwnd, modStr.c_str());

        /* Prevent other applications from receiving this event */
        return (LRESULT) 1;
    }

    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

LRESULT CALLBACK
KeyGrabber::MouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode < 0) {
        return CallNextHookEx(NULL, nCode, wParam, lParam);
    }

    unsigned int key = 0;
    std::wstring keyStr;

    switch (wParam) {
    case WM_LBUTTONDOWN:
        key = VK_LBUTTON;
        break;

    case WM_RBUTTONDOWN:
        key = VK_RBUTTON;
        break;

    case WM_MBUTTONDOWN:
        key = VK_MBUTTON;
        break;

    case WM_XBUTTONDOWN: {
        MSLLHOOKSTRUCT *msInfo = (MSLLHOOKSTRUCT *) lParam;
        int x = HIWORD(msInfo->mouseData);
        if (x == 1) {
            key = HKM_MOUSE_XB1;
        } else if (x == 2) {
            key = HKM_MOUSE_XB2;
        }
        break;
    }

    case WM_MOUSEWHEEL:
        short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        if (zDelta > 0) {
            key = HKM_MOUSE_WHUP;
        } else if (zDelta < 0) {
            key = HKM_MOUSE_WHDN;
        }

        break;
    }

    if (key > 0) {
        int mods = HotkeyManager::ModifiersAsync();
        std::wstring modStr = ModString(mods);
        SetWindowText(_updateHwnd, modStr.c_str());
    }

    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

LRESULT CALLBACK
KeyGrabber::LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    return KeyGrabber::instance->MouseProc(nCode, wParam, lParam);
}

LRESULT CALLBACK
KeyGrabber::LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    return KeyGrabber::instance->KeyProc(nCode, wParam, lParam);
}