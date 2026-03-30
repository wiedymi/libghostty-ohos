#include <hilog/log.h>
#include <napi/native_api.h>
#include <ace/xcomponent/native_interface_xcomponent.h>
#include <ace/xcomponent/native_xcomponent_key_event.h>
#include <arkui/ui_input_event.h>
#include <inputmethod/inputmethod_attach_options_capi.h>
#include <inputmethod/inputmethod_controller_capi.h>
#include <inputmethod/inputmethod_cursor_info_capi.h>
#include <inputmethod/inputmethod_inputmethod_proxy_capi.h>
#include <inputmethod/inputmethod_text_config_capi.h>
#include <inputmethod/inputmethod_text_editor_proxy_capi.h>
#include <rawfile/raw_file_manager.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dlfcn.h>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>
#include "terminal/terminal.h"
#include "terminal/theme.h"
#include "renderer/renderer.h"
#include "renderer/native_drawing_renderer.h"

#undef LOG_TAG
#define LOG_TAG "libghostty_ohos"
#define LOG_DOMAIN 0x0001

namespace {
class TerminalHost;
using ExampleDriverWriteInputFn = bool (*)(const char*, size_t);

ExampleDriverWriteInputFn ResolveExampleDriverWriteInput()
{
    static std::mutex mutex;
    static ExampleDriverWriteInputFn cached = nullptr;
    if (cached != nullptr) {
        return cached;
    }

    std::lock_guard<std::mutex> lock(mutex);
    if (cached == nullptr) {
        void* symbol = dlsym(RTLD_DEFAULT, "ExampleDriverWriteInputUtf8");
        if (symbol != nullptr) {
            cached = reinterpret_cast<ExampleDriverWriteInputFn>(symbol);
        }
    }
    return cached;
}

std::mutex g_imeProxyHostsMutex;
std::unordered_map<InputMethod_TextEditorProxy*, TerminalHost*> g_imeProxyHosts;

TerminalHost* FindImeHost(InputMethod_TextEditorProxy* proxy)
{
    std::lock_guard<std::mutex> lock(g_imeProxyHostsMutex);
    auto it = g_imeProxyHosts.find(proxy);
    return it == g_imeProxyHosts.end() ? nullptr : it->second;
}

void RegisterImeHost(InputMethod_TextEditorProxy* proxy, TerminalHost* host)
{
    if (proxy == nullptr || host == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_imeProxyHostsMutex);
    g_imeProxyHosts[proxy] = host;
}

void UnregisterImeHost(InputMethod_TextEditorProxy* proxy)
{
    if (proxy == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_imeProxyHostsMutex);
    g_imeProxyHosts.erase(proxy);
}

bool IsNearOrigin(double left, double top)
{
    return left >= 0.0 && left < 64.0 && top >= 0.0 && top < 160.0;
}

constexpr uint64_t LONG_PRESS_MS = 500;
constexpr uint64_t MULTI_CLICK_MS = 400;
constexpr float MOVE_THRESHOLD = 20.0f;
constexpr auto CURSOR_BLINK_TICK = std::chrono::milliseconds(250);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_TAB =
    static_cast<OH_NativeXComponent_KeyCode>(15);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_1 =
    static_cast<OH_NativeXComponent_KeyCode>(2);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_2 =
    static_cast<OH_NativeXComponent_KeyCode>(3);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_3 =
    static_cast<OH_NativeXComponent_KeyCode>(4);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_4 =
    static_cast<OH_NativeXComponent_KeyCode>(5);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_5 =
    static_cast<OH_NativeXComponent_KeyCode>(6);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_6 =
    static_cast<OH_NativeXComponent_KeyCode>(7);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_7 =
    static_cast<OH_NativeXComponent_KeyCode>(8);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_8 =
    static_cast<OH_NativeXComponent_KeyCode>(9);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_9 =
    static_cast<OH_NativeXComponent_KeyCode>(10);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_0 =
    static_cast<OH_NativeXComponent_KeyCode>(11);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_MINUS =
    static_cast<OH_NativeXComponent_KeyCode>(12);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_EQUALS =
    static_cast<OH_NativeXComponent_KeyCode>(13);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_ENTER =
    static_cast<OH_NativeXComponent_KeyCode>(28);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_BACKSPACE =
    static_cast<OH_NativeXComponent_KeyCode>(14);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_Q =
    static_cast<OH_NativeXComponent_KeyCode>(16);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_W =
    static_cast<OH_NativeXComponent_KeyCode>(17);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_E =
    static_cast<OH_NativeXComponent_KeyCode>(18);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_R =
    static_cast<OH_NativeXComponent_KeyCode>(19);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_T =
    static_cast<OH_NativeXComponent_KeyCode>(20);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_Y =
    static_cast<OH_NativeXComponent_KeyCode>(21);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_U =
    static_cast<OH_NativeXComponent_KeyCode>(22);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_I =
    static_cast<OH_NativeXComponent_KeyCode>(23);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_O =
    static_cast<OH_NativeXComponent_KeyCode>(24);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_P =
    static_cast<OH_NativeXComponent_KeyCode>(25);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_LEFT_BRACE =
    static_cast<OH_NativeXComponent_KeyCode>(26);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_RIGHT_BRACE =
    static_cast<OH_NativeXComponent_KeyCode>(27);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_A =
    static_cast<OH_NativeXComponent_KeyCode>(30);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_S =
    static_cast<OH_NativeXComponent_KeyCode>(31);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_D =
    static_cast<OH_NativeXComponent_KeyCode>(32);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_F =
    static_cast<OH_NativeXComponent_KeyCode>(33);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_G =
    static_cast<OH_NativeXComponent_KeyCode>(34);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_H =
    static_cast<OH_NativeXComponent_KeyCode>(35);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_J =
    static_cast<OH_NativeXComponent_KeyCode>(36);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_K =
    static_cast<OH_NativeXComponent_KeyCode>(37);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_L =
    static_cast<OH_NativeXComponent_KeyCode>(38);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_SEMICOLON =
    static_cast<OH_NativeXComponent_KeyCode>(39);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_APOSTROPHE =
    static_cast<OH_NativeXComponent_KeyCode>(40);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_GRAVE =
    static_cast<OH_NativeXComponent_KeyCode>(41);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_BACKSLASH =
    static_cast<OH_NativeXComponent_KeyCode>(43);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_Z =
    static_cast<OH_NativeXComponent_KeyCode>(44);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_X =
    static_cast<OH_NativeXComponent_KeyCode>(45);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_C =
    static_cast<OH_NativeXComponent_KeyCode>(46);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_V =
    static_cast<OH_NativeXComponent_KeyCode>(47);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_B =
    static_cast<OH_NativeXComponent_KeyCode>(48);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_N =
    static_cast<OH_NativeXComponent_KeyCode>(49);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_M =
    static_cast<OH_NativeXComponent_KeyCode>(50);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_COMMA =
    static_cast<OH_NativeXComponent_KeyCode>(51);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_DOT =
    static_cast<OH_NativeXComponent_KeyCode>(52);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_SLASH =
    static_cast<OH_NativeXComponent_KeyCode>(53);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_SPACE =
    static_cast<OH_NativeXComponent_KeyCode>(57);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_UP =
    static_cast<OH_NativeXComponent_KeyCode>(103);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_LEFT =
    static_cast<OH_NativeXComponent_KeyCode>(105);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_RIGHT =
    static_cast<OH_NativeXComponent_KeyCode>(106);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_DOWN =
    static_cast<OH_NativeXComponent_KeyCode>(108);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_PAGE_UP =
    static_cast<OH_NativeXComponent_KeyCode>(104);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_PAGE_DOWN =
    static_cast<OH_NativeXComponent_KeyCode>(109);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_HOME =
    static_cast<OH_NativeXComponent_KeyCode>(102);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_END =
    static_cast<OH_NativeXComponent_KeyCode>(107);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_INSERT =
    static_cast<OH_NativeXComponent_KeyCode>(110);
constexpr OH_NativeXComponent_KeyCode LINUX_KEY_DELETE =
    static_cast<OH_NativeXComponent_KeyCode>(111);

std::u16string Utf8ToUtf16(const std::string& text)
{
    std::u16string result;
    result.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        uint32_t cp = 0;
        const unsigned char c0 = static_cast<unsigned char>(text[i]);
        size_t width = 1;
        if ((c0 & 0x80u) == 0) {
            cp = c0;
        } else if ((c0 & 0xE0u) == 0xC0u && i + 1 < text.size()) {
            cp = ((c0 & 0x1Fu) << 6) |
                (static_cast<unsigned char>(text[i + 1]) & 0x3Fu);
            width = 2;
        } else if ((c0 & 0xF0u) == 0xE0u && i + 2 < text.size()) {
            cp = ((c0 & 0x0Fu) << 12) |
                ((static_cast<unsigned char>(text[i + 1]) & 0x3Fu) << 6) |
                (static_cast<unsigned char>(text[i + 2]) & 0x3Fu);
            width = 3;
        } else if ((c0 & 0xF8u) == 0xF0u && i + 3 < text.size()) {
            cp = ((c0 & 0x07u) << 18) |
                ((static_cast<unsigned char>(text[i + 1]) & 0x3Fu) << 12) |
                ((static_cast<unsigned char>(text[i + 2]) & 0x3Fu) << 6) |
                (static_cast<unsigned char>(text[i + 3]) & 0x3Fu);
            width = 4;
        } else {
            cp = 0xFFFDu;
        }

        if (cp <= 0xFFFFu) {
            result.push_back(static_cast<char16_t>(cp));
        } else {
            cp -= 0x10000u;
            result.push_back(static_cast<char16_t>(0xD800u + ((cp >> 10) & 0x3FFu)));
            result.push_back(static_cast<char16_t>(0xDC00u + (cp & 0x3FFu)));
        }
        i += width;
    }
    return result;
}

std::string Utf16ToUtf8(const char16_t* text, size_t length)
{
    std::string result;
    if (text == nullptr || length == 0) {
        return result;
    }
    result.reserve(length * 3);
    for (size_t i = 0; i < length; ++i) {
        uint32_t cp = text[i];
        if (cp >= 0xD800u && cp <= 0xDBFFu && i + 1 < length) {
            const uint32_t low = text[i + 1];
            if (low >= 0xDC00u && low <= 0xDFFFu) {
                cp = 0x10000u + (((cp - 0xD800u) << 10) | (low - 0xDC00u));
                ++i;
            }
        }

        if (cp <= 0x7Fu) {
            result.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FFu) {
            result.push_back(static_cast<char>(0xC0u | ((cp >> 6) & 0x1Fu)));
            result.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else if (cp <= 0xFFFFu) {
            result.push_back(static_cast<char>(0xE0u | ((cp >> 12) & 0x0Fu)));
            result.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            result.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else {
            result.push_back(static_cast<char>(0xF0u | ((cp >> 18) & 0x07u)));
            result.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
            result.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            result.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        }
    }
    return result;
}

uint64_t getCurrentTimeMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

bool IsCtrlPressed(uint64_t modifiers)
{
    return (modifiers & ARKUI_MODIFIER_KEY_CTRL) != 0;
}

bool IsAltPressed(uint64_t modifiers)
{
    return (modifiers & ARKUI_MODIFIER_KEY_ALT) != 0;
}

bool IsShiftPressed(uint64_t modifiers)
{
    return (modifiers & ARKUI_MODIFIER_KEY_SHIFT) != 0;
}

int LinuxLetterOffset(OH_NativeXComponent_KeyCode code)
{
    switch (code) {
        case LINUX_KEY_A: return 0;
        case LINUX_KEY_B: return 1;
        case LINUX_KEY_C: return 2;
        case LINUX_KEY_D: return 3;
        case LINUX_KEY_E: return 4;
        case LINUX_KEY_F: return 5;
        case LINUX_KEY_G: return 6;
        case LINUX_KEY_H: return 7;
        case LINUX_KEY_I: return 8;
        case LINUX_KEY_J: return 9;
        case LINUX_KEY_K: return 10;
        case LINUX_KEY_L: return 11;
        case LINUX_KEY_M: return 12;
        case LINUX_KEY_N: return 13;
        case LINUX_KEY_O: return 14;
        case LINUX_KEY_P: return 15;
        case LINUX_KEY_Q: return 16;
        case LINUX_KEY_R: return 17;
        case LINUX_KEY_S: return 18;
        case LINUX_KEY_T: return 19;
        case LINUX_KEY_U: return 20;
        case LINUX_KEY_V: return 21;
        case LINUX_KEY_W: return 22;
        case LINUX_KEY_X: return 23;
        case LINUX_KEY_Y: return 24;
        case LINUX_KEY_Z: return 25;
        default:
            return -1;
    }
}

bool AppendPrintableKey(OH_NativeXComponent_KeyCode code, bool shift, bool capsLock, std::string& out)
{
    const int linuxLetter = LinuxLetterOffset(code);
    if (linuxLetter >= 0) {
        const bool upper = shift ^ capsLock;
        const char base = upper ? 'A' : 'a';
        out.push_back(static_cast<char>(base + linuxLetter));
        return true;
    }

    if (code >= KEY_A && code <= KEY_Z) {
        const bool upper = shift ^ capsLock;
        const char base = upper ? 'A' : 'a';
        out.push_back(static_cast<char>(base + (code - KEY_A)));
        return true;
    }

    if (code >= LINUX_KEY_1 && code <= LINUX_KEY_0) {
        static const char kDigits[] = "1234567890";
        static const char kShiftDigits[] = "!@#$%^&*()";
        const size_t index = static_cast<size_t>(code - LINUX_KEY_1);
        out.push_back(shift ? kShiftDigits[index] : kDigits[index]);
        return true;
    }

    if (code >= KEY_0 && code <= KEY_9) {
        static const char kDigits[] = "0123456789";
        static const char kShiftDigits[] = ")!@#$%^&*(";
        const size_t index = static_cast<size_t>(code - KEY_0);
        out.push_back(shift ? kShiftDigits[index] : kDigits[index]);
        return true;
    }

    switch (code) {
        case LINUX_KEY_SPACE: out.push_back(' '); return true;
        case LINUX_KEY_GRAVE: out.push_back(shift ? '~' : '`'); return true;
        case LINUX_KEY_MINUS: out.push_back(shift ? '_' : '-'); return true;
        case LINUX_KEY_EQUALS: out.push_back(shift ? '+' : '='); return true;
        case LINUX_KEY_LEFT_BRACE: out.push_back(shift ? '{' : '['); return true;
        case LINUX_KEY_RIGHT_BRACE: out.push_back(shift ? '}' : ']'); return true;
        case LINUX_KEY_BACKSLASH: out.push_back(shift ? '|' : '\\'); return true;
        case LINUX_KEY_SEMICOLON: out.push_back(shift ? ':' : ';'); return true;
        case LINUX_KEY_APOSTROPHE: out.push_back(shift ? '"' : '\''); return true;
        case LINUX_KEY_COMMA: out.push_back(shift ? '<' : ','); return true;
        case LINUX_KEY_DOT: out.push_back(shift ? '>' : '.'); return true;
        case LINUX_KEY_SLASH: out.push_back(shift ? '?' : '/'); return true;
        case KEY_SPACE: out.push_back(' '); return true;
        case KEY_TAB: out.push_back('\t'); return true;
        case KEY_GRAVE: out.push_back(shift ? '~' : '`'); return true;
        case KEY_MINUS: out.push_back(shift ? '_' : '-'); return true;
        case KEY_EQUALS: out.push_back(shift ? '+' : '='); return true;
        case KEY_LEFT_BRACKET: out.push_back(shift ? '{' : '['); return true;
        case KEY_RIGHT_BRACKET: out.push_back(shift ? '}' : ']'); return true;
        case KEY_BACKSLASH: out.push_back(shift ? '|' : '\\'); return true;
        case KEY_SEMICOLON: out.push_back(shift ? ':' : ';'); return true;
        case KEY_APOSTROPHE: out.push_back(shift ? '"' : '\''); return true;
        case KEY_COMMA: out.push_back(shift ? '<' : ','); return true;
        case KEY_PERIOD: out.push_back(shift ? '>' : '.'); return true;
        case KEY_SLASH: out.push_back(shift ? '?' : '/'); return true;
        case KEY_NUMPAD_0: out.push_back('0'); return true;
        case KEY_NUMPAD_1: out.push_back('1'); return true;
        case KEY_NUMPAD_2: out.push_back('2'); return true;
        case KEY_NUMPAD_3: out.push_back('3'); return true;
        case KEY_NUMPAD_4: out.push_back('4'); return true;
        case KEY_NUMPAD_5: out.push_back('5'); return true;
        case KEY_NUMPAD_6: out.push_back('6'); return true;
        case KEY_NUMPAD_7: out.push_back('7'); return true;
        case KEY_NUMPAD_8: out.push_back('8'); return true;
        case KEY_NUMPAD_9: out.push_back('9'); return true;
        case KEY_NUMPAD_DOT: out.push_back('.'); return true;
        case KEY_NUMPAD_COMMA: out.push_back(','); return true;
        case KEY_NUMPAD_DIVIDE: out.push_back('/'); return true;
        case KEY_NUMPAD_MULTIPLY: out.push_back('*'); return true;
        case KEY_NUMPAD_SUBTRACT: out.push_back('-'); return true;
        case KEY_NUMPAD_ADD: out.push_back('+'); return true;
        default:
            return false;
    }
}

bool BuildKeySequence(
    OH_NativeXComponent_KeyCode code,
    uint64_t modifiers,
    bool capsLock,
    std::string& sequence)
{
    const bool ctrl = IsCtrlPressed(modifiers);
    const bool alt = IsAltPressed(modifiers);
    const bool shift = IsShiftPressed(modifiers);

    switch (code) {
        case LINUX_KEY_UP:
        case KEY_DPAD_UP: sequence = "\x1b[A"; return true;
        case LINUX_KEY_DOWN:
        case KEY_DPAD_DOWN: sequence = "\x1b[B"; return true;
        case LINUX_KEY_RIGHT:
        case KEY_DPAD_RIGHT: sequence = "\x1b[C"; return true;
        case LINUX_KEY_LEFT:
        case KEY_DPAD_LEFT: sequence = "\x1b[D"; return true;
        case KEY_ESCAPE: sequence = "\x1b"; return true;
        case LINUX_KEY_ENTER:
        case KEY_ENTER:
        case KEY_NUMPAD_ENTER:
            sequence = "\r";
            return true;
        case LINUX_KEY_TAB:
        case KEY_TAB:
            sequence = "\t";
            return true;
        case LINUX_KEY_BACKSPACE:
        case KEY_DEL:
            sequence = "\x7f";
            return true;
        case LINUX_KEY_DELETE:
        case KEY_FORWARD_DEL:
            sequence = "\x1b[3~";
            return true;
        case LINUX_KEY_HOME:
        case KEY_MOVE_HOME:
        case KEY_HOME:
            sequence = "\x1b[H";
            return true;
        case LINUX_KEY_END:
        case KEY_MOVE_END:
            sequence = "\x1b[F";
            return true;
        case LINUX_KEY_PAGE_UP:
        case KEY_PAGE_UP:
            sequence = "\x1b[5~";
            return true;
        case LINUX_KEY_PAGE_DOWN:
        case KEY_PAGE_DOWN:
            sequence = "\x1b[6~";
            return true;
        case LINUX_KEY_INSERT:
        case KEY_INSERT:
            sequence = "\x1b[2~";
            return true;
        case KEY_F1: sequence = "\x1bOP"; return true;
        case KEY_F2: sequence = "\x1bOQ"; return true;
        case KEY_F3: sequence = "\x1bOR"; return true;
        case KEY_F4: sequence = "\x1bOS"; return true;
        case KEY_F5: sequence = "\x1b[15~"; return true;
        case KEY_F6: sequence = "\x1b[17~"; return true;
        case KEY_F7: sequence = "\x1b[18~"; return true;
        case KEY_F8: sequence = "\x1b[19~"; return true;
        case KEY_F9: sequence = "\x1b[20~"; return true;
        case KEY_F10: sequence = "\x1b[21~"; return true;
        case KEY_F11: sequence = "\x1b[23~"; return true;
        case KEY_F12: sequence = "\x1b[24~"; return true;
        default:
            break;
    }

    const int linuxLetter = LinuxLetterOffset(code);
    if (ctrl && linuxLetter >= 0) {
        sequence.push_back(static_cast<char>(1 + linuxLetter));
        return true;
    }

    if (ctrl && code >= KEY_A && code <= KEY_Z) {
        sequence.push_back(static_cast<char>(1 + (code - KEY_A)));
        return true;
    }

    std::string printable;
    if (!AppendPrintableKey(code, shift, capsLock, printable)) {
        return false;
    }

    if (alt) {
        sequence.push_back('\x1b');
    }
    sequence += printable;
    return true;
}

void RetainNativeWindowLocked(OHNativeWindow* window) {
    if (!window) {
        return;
    }
    const int32_t ret = OH_NativeWindow_NativeObjectReference(window);
    if (ret != 0) {
        OH_LOG_WARN(LOG_APP, "Failed to retain native window, ret=%{public}d", ret);
    }
}

void ReleaseNativeWindowLocked(OHNativeWindow* window) {
    if (!window) {
        return;
    }
    const int32_t ret = OH_NativeWindow_NativeObjectUnreference(window);
    if (ret != 0) {
        OH_LOG_WARN(LOG_APP, "Failed to release native window, ret=%{public}d", ret);
    }
}

class TerminalHost {
public:
    explicit TerminalHost(OH_NativeXComponent* component)
        : m_component(component) {}

    ~TerminalHost() {
        StopRenderLoop();
        ClearInputCallback();
        std::lock_guard<std::mutex> lock(m_surfaceMutex);
        DetachImeLocked();
        CleanupSurfaceLocked();
        if (m_terminal) {
            m_terminal->stop();
            delete m_terminal;
            m_terminal = nullptr;
        }
    }

    void OnSurfaceCreated(OH_NativeXComponent* component, void* window) {
        uint64_t width = 0;
        uint64_t height = 0;
        if (OH_NativeXComponent_GetXComponentSize(component, window, &width, &height) !=
            OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
            OH_LOG_ERROR(LOG_APP, "Failed to get XComponent size");
            return;
        }

        {
            std::lock_guard<std::mutex> surfaceLock(m_surfaceMutex);
            auto* newWindow = static_cast<OHNativeWindow*>(window);
            if (m_nativeWindow != newWindow) {
                ReleaseNativeWindowLocked(m_nativeWindow);
                RetainNativeWindowLocked(newWindow);
            }

            m_nativeWindow = newWindow;
            m_windowWidth = static_cast<uint32_t>(width);
            m_windowHeight = static_cast<uint32_t>(height);

            if (!m_renderer) {
                m_renderer = new NativeDrawingRenderer();
            }

            if (!m_renderer->init(m_nativeWindow, m_windowWidth, m_windowHeight)) {
                m_rendererReady = false;
                m_rendererError = "Native drawing renderer initialization failed";
                CleanupSurfaceLocked();
                OH_LOG_ERROR(LOG_APP, "Failed to initialize native drawing renderer");
                return;
            }

            m_surfaceReady = true;
            m_rendererReady = true;
            m_rendererError.clear();
            LoadFontsIfPossibleLocked();
            TryInitializeTerminalLocked();
            if (m_terminal) {
                m_terminal->setRenderer(m_renderer);
            }
        }

        StartRenderLoop();
        RequestRender();
    }

    void OnSurfaceChanged(OH_NativeXComponent* component, void* window) {
        uint64_t width = 0;
        uint64_t height = 0;
        if (OH_NativeXComponent_GetXComponentSize(component, window, &width, &height) !=
            OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
            return;
        }

        {
            std::lock_guard<std::mutex> surfaceLock(m_surfaceMutex);
            m_windowWidth = static_cast<uint32_t>(width);
            m_windowHeight = static_cast<uint32_t>(height);
        }

        {
            std::lock_guard<std::mutex> lock(m_renderMutex);
            m_pendingWidth = static_cast<uint32_t>(width);
            m_pendingHeight = static_cast<uint32_t>(height);
            m_resizePending = true;
            m_renderDirty = true;
        }
        m_renderCv.notify_one();
    }

    void OnSurfaceShow(OH_NativeXComponent* component, void* window) {
        uint64_t width = 0;
        uint64_t height = 0;
        if (OH_NativeXComponent_GetXComponentSize(component, window, &width, &height) !=
            OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
            OH_LOG_WARN(LOG_APP, "OnSurfaceShow failed to get XComponent size");
        }

        {
            std::lock_guard<std::mutex> surfaceLock(m_surfaceMutex);
            auto* shownWindow = static_cast<OHNativeWindow*>(window);
            if (m_nativeWindow != shownWindow) {
                ReleaseNativeWindowLocked(m_nativeWindow);
                RetainNativeWindowLocked(shownWindow);
                m_nativeWindow = shownWindow;
            }

            if (width > 0 && height > 0) {
                m_windowWidth = static_cast<uint32_t>(width);
                m_windowHeight = static_cast<uint32_t>(height);
            }

            m_surfaceReady = true;
            if (m_renderer && m_nativeWindow && !m_rendererReady) {
                m_rendererReady = m_renderer->init(m_nativeWindow, m_windowWidth, m_windowHeight);
                if (!m_rendererReady) {
                    m_rendererError = "Native drawing renderer initialization failed";
                }
            }
            if (m_terminal && m_rendererReady) {
                m_terminal->setRenderer(m_renderer);
            }
            if (m_wantsIme) {
                ShowImeLocked(IME_REQUEST_REASON_OTHER);
                NotifyImeStateLocked();
            }
        }

        StartRenderLoop();
        RequestRender();
    }

    void OnSurfaceHide() {
        {
            std::lock_guard<std::mutex> surfaceLock(m_surfaceMutex);
            m_surfaceReady = false;
            HideImeLocked();
        }
        {
            std::lock_guard<std::mutex> lock(m_renderMutex);
            m_renderDirty = false;
        }
    }

    void OnSurfaceDestroyed() {
        StopRenderLoop();

        std::lock_guard<std::mutex> surfaceLock(m_surfaceMutex);
        m_surfaceReady = false;
        m_surfaceFrameOriginKnown = false;
        m_surfaceScreenOriginKnown = false;
        m_windowScreenOriginKnown = false;
        m_lastImeBaseKnown = false;
        HideImeLocked();
        if (m_terminal) {
            m_terminal->setRenderer(nullptr);
        }
        DetachImeLocked();
        CleanupSurfaceLocked();
        m_rendererReady = false;
    }

    bool DispatchKeyEvent(OH_NativeXComponent* component) {
        if (!m_terminal) {
            return false;
        }

        OH_NativeXComponent_KeyEvent* keyEvent = nullptr;
        if (OH_NativeXComponent_GetKeyEvent(component, &keyEvent) != OH_NATIVEXCOMPONENT_RESULT_SUCCESS || !keyEvent) {
            return false;
        }

        OH_NativeXComponent_KeyAction action = OH_NATIVEXCOMPONENT_KEY_ACTION_UNKNOWN;
        if (OH_NativeXComponent_GetKeyEventAction(keyEvent, &action) != OH_NATIVEXCOMPONENT_RESULT_SUCCESS ||
            action != OH_NATIVEXCOMPONENT_KEY_ACTION_DOWN) {
            return false;
        }

        OH_NativeXComponent_KeyCode code = KEY_UNKNOWN;
        if (OH_NativeXComponent_GetKeyEventCode(keyEvent, &code) != OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
            return false;
        }

        uint64_t modifiers = 0;
        OH_NativeXComponent_GetKeyEventModifierKeyStates(keyEvent, &modifiers);
        bool capsLock = false;
        OH_NativeXComponent_GetKeyEventCapsLockState(keyEvent, &capsLock);

        if (m_wantsIme && !m_imeVisible) {
            std::lock_guard<std::mutex> surfaceLock(m_surfaceMutex);
            ShowImeLocked(IME_REQUEST_REASON_OTHER);
            NotifyImeStateLocked();
        }

        std::string sequence;
        if (!BuildKeySequence(code, modifiers, capsLock, sequence) || sequence.empty()) {
            return false;
        }

        ExampleDriverWriteInputFn nativeSink = ResolveExampleDriverWriteInput();
        if (nativeSink != nullptr && nativeSink(sequence.data(), sequence.size())) {
            return true;
        }

        m_terminal->writeInput(sequence.data(), sequence.size());
        return true;
    }

    void DispatchTouchEvent(OH_NativeXComponent* component, void* window) {
        OH_NativeXComponent_TouchEvent touchEvent {};
        if (OH_NativeXComponent_GetTouchEvent(component, window, &touchEvent) != OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
            return;
        }

        float windowX = 0.0f;
        float windowY = 0.0f;
        if (OH_NativeXComponent_GetTouchPointWindowX(component, 0, &windowX) ==
                OH_NATIVEXCOMPONENT_RESULT_SUCCESS &&
            OH_NativeXComponent_GetTouchPointWindowY(component, 0, &windowY) ==
                OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
            if (!m_windowScreenOriginKnown) {
                m_windowScreenOriginKnown = true;
                m_windowScreenLeft = static_cast<double>(touchEvent.screenX - windowX);
                m_windowScreenTop = static_cast<double>(touchEvent.screenY - windowY);
            }
        }

        if (!m_surfaceFrameOriginKnown) {
            m_surfaceScreenOriginKnown = true;
            m_surfaceScreenLeft = static_cast<double>(touchEvent.screenX - touchEvent.x);
            m_surfaceScreenTop = static_cast<double>(touchEvent.screenY - touchEvent.y);
        }

        if (!m_terminal || !m_renderer) {
            return;
        }

        const float cellHeight = m_renderer->getCellHeight();
        const float cellWidth = m_renderer->getCellWidth();
        if (!std::isfinite(cellHeight) || !std::isfinite(cellWidth) ||
            cellHeight <= 0.0f || cellWidth <= 0.0f) {
            return;
        }

        switch (touchEvent.type) {
            case OH_NATIVEXCOMPONENT_DOWN: {
                m_isTouching = true;
                m_isSelecting = false;
                m_touchScrollRemainderY = 0.0f;
                m_lastTouchY = touchEvent.y;
                m_touchStartX = touchEvent.x;
                m_touchStartY = touchEvent.y;
                m_touchStartTime = getCurrentTimeMs();
                break;
            }

            case OH_NATIVEXCOMPONENT_MOVE: {
                if (!m_isTouching) {
                    break;
                }

                const float dx = touchEvent.x - m_touchStartX;
                const float dy = touchEvent.y - m_touchStartY;
                const float moveDistance = std::sqrt(dx * dx + dy * dy);
                const uint64_t elapsed = getCurrentTimeMs() - m_touchStartTime;

                if (!m_isSelecting && elapsed >= LONG_PRESS_MS) {
                    int startRow = 0;
                    int startCol = 0;
                    MapPointToCell(m_touchStartX, m_touchStartY, cellWidth, cellHeight, startRow, startCol);
                    m_terminal->startSelection(startRow, startCol);
                    m_isSelecting = true;
                }

                if (m_isSelecting) {
                    int row = 0;
                    int col = 0;
                    MapPointToCell(touchEvent.x, touchEvent.y, cellWidth, cellHeight, row, col);
                    m_terminal->updateSelection(row, col);
                } else if (moveDistance >= MOVE_THRESHOLD) {
                    ScrollViewportByPointerDelta(m_lastTouchY - touchEvent.y, cellHeight);
                }

                m_lastTouchY = touchEvent.y;
                break;
            }

            case OH_NATIVEXCOMPONENT_UP:
            case OH_NATIVEXCOMPONENT_CANCEL: {
                if (m_isTouching && !m_isSelecting) {
                    const float dx = touchEvent.x - m_touchStartX;
                    const float dy = touchEvent.y - m_touchStartY;
                    const float moveDistance = std::sqrt(dx * dx + dy * dy);
                    const uint64_t elapsed = getCurrentTimeMs() - m_touchStartTime;

                    if (elapsed >= LONG_PRESS_MS && moveDistance < MOVE_THRESHOLD) {
                        int row = 0;
                        int col = 0;
                        MapPointToCell(touchEvent.x, touchEvent.y, cellWidth, cellHeight, row, col);
                        m_terminal->startSelection(row, col);
                        m_terminal->updateSelection(row, col);
                    } else if (touchEvent.type == OH_NATIVEXCOMPONENT_UP && moveDistance < MOVE_THRESHOLD) {
                        ShowImeLocked(IME_REQUEST_REASON_TOUCH);
                        NotifyImeStateLocked();
                        QueueLinkActivationAtPoint(touchEvent.x, touchEvent.y, cellWidth, cellHeight);
                    }
                }

                m_isTouching = false;
                m_isSelecting = false;
                m_touchScrollRemainderY = 0.0f;
                break;
            }

            default:
                break;
        }
    }

    void DispatchMouseEvent(OH_NativeXComponent* component, void* window) {
        OH_NativeXComponent_MouseEvent mouseEvent {};
        if (OH_NativeXComponent_GetMouseEvent(component, window, &mouseEvent) != OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
            return;
        }

        if (!m_surfaceFrameOriginKnown) {
            m_surfaceScreenOriginKnown = true;
            m_surfaceScreenLeft = static_cast<double>(mouseEvent.screenX - mouseEvent.x);
            m_surfaceScreenTop = static_cast<double>(mouseEvent.screenY - mouseEvent.y);
        }

        if (!m_terminal || !m_renderer) {
            return;
        }

        const float cellHeight = m_renderer->getCellHeight();
        const float cellWidth = m_renderer->getCellWidth();
        if (!std::isfinite(cellHeight) || !std::isfinite(cellWidth) ||
            cellHeight <= 0.0f || cellWidth <= 0.0f) {
            return;
        }

        int row = 0;
        int col = 0;
        MapPointToCell(mouseEvent.x, mouseEvent.y, cellWidth, cellHeight, row, col);

        switch (mouseEvent.action) {
            case OH_NATIVEXCOMPONENT_MOUSE_PRESS:
                if (mouseEvent.button == OH_NATIVEXCOMPONENT_LEFT_BUTTON) {
                    m_isMousePressed = true;
                    m_isMouseSelecting = false;
                    m_mouseDragged = false;
                    m_mousePressRow = row;
                    m_mousePressCol = col;
                    m_mouseHadSelectionOnPress = m_terminal->hasSelection();
                    m_mousePressOnSelection = m_terminal->isSelectionAt(row, col);
                    if (m_mouseHadSelectionOnPress && !m_mousePressOnSelection) {
                        m_terminal->clearSelection();
                    }
                }
                break;

            case OH_NATIVEXCOMPONENT_MOUSE_MOVE:
                if (m_isMousePressed && !m_isMouseSelecting &&
                    (row != m_mousePressRow || col != m_mousePressCol)) {
                    m_terminal->startSelection(m_mousePressRow, m_mousePressCol);
                    m_isMouseSelecting = true;
                    m_mouseDragged = true;
                }
                if (m_isMouseSelecting) {
                    m_terminal->updateSelection(row, col);
                }
                break;

            case OH_NATIVEXCOMPONENT_MOUSE_RELEASE:
                if (m_isMouseSelecting) {
                    m_terminal->updateSelection(row, col);
                } else if (mouseEvent.button == OH_NATIVEXCOMPONENT_RIGHT_BUTTON) {
                    QueueContextMenuRequest(mouseEvent.x, mouseEvent.y);
                } else if (m_isMousePressed && mouseEvent.button == OH_NATIVEXCOMPONENT_LEFT_BUTTON) {
                    ShowImeLocked(IME_REQUEST_REASON_MOUSE);
                    NotifyImeStateLocked();
                    const bool sameCell = row == m_mousePressRow && col == m_mousePressCol;
                    const bool selectionWasClearedByClick = m_mouseHadSelectionOnPress && !m_mousePressOnSelection;
                    if (sameCell) {
                        const uint64_t now = getCurrentTimeMs();
                        if (m_lastClickCount > 0 &&
                            now - m_lastClickTimeMs <= MULTI_CLICK_MS &&
                            row == m_lastClickRow &&
                            col == m_lastClickCol) {
                            m_lastClickCount = std::min(3, m_lastClickCount + 1);
                        } else {
                            m_lastClickCount = 1;
                        }
                        m_lastClickTimeMs = now;
                        m_lastClickRow = row;
                        m_lastClickCol = col;

                        if (m_lastClickCount == 2) {
                            m_terminal->selectWordAt(row, col);
                        } else if (m_lastClickCount >= 3) {
                            m_terminal->selectLineAt(row);
                        } else if (!selectionWasClearedByClick) {
                            QueueLinkActivationAtPoint(mouseEvent.x, mouseEvent.y, cellWidth, cellHeight);
                        }
                    }
                }
                m_isMousePressed = false;
                m_isMouseSelecting = false;
                m_mouseDragged = false;
                m_mouseHadSelectionOnPress = false;
                m_mousePressOnSelection = false;
                break;

            case OH_NATIVEXCOMPONENT_MOUSE_CANCEL:
                m_isMousePressed = false;
                m_isMouseSelecting = false;
                m_mouseDragged = false;
                m_mouseHadSelectionOnPress = false;
                m_mousePressOnSelection = false;
                break;

            case OH_NATIVEXCOMPONENT_MOUSE_NONE:
            default:
                break;
        }
    }

    void DispatchAxisEvent(ArkUI_UIInputEvent* event) {
        if (!event || !m_terminal || !m_renderer) {
            return;
        }

        const int32_t action = OH_ArkUI_AxisEvent_GetAxisAction(event);
        if (action == UI_AXIS_EVENT_ACTION_END || action == UI_AXIS_EVENT_ACTION_CANCEL) {
            m_axisScrollRemainderY = 0.0;
            return;
        }
        if (action != UI_AXIS_EVENT_ACTION_BEGIN && action != UI_AXIS_EVENT_ACTION_UPDATE) {
            return;
        }

        const double vertical = OH_ArkUI_AxisEvent_GetVerticalAxisValue(event);
        if (!std::isfinite(vertical) || vertical == 0.0) {
            return;
        }

        const float cellHeight = m_renderer->getCellHeight();
        if (!std::isfinite(cellHeight) || cellHeight <= 0.0f) {
            return;
        }

        const int32_t sourceType = OH_ArkUI_UIInputEvent_GetSourceType(event);
        const int32_t toolType = OH_ArkUI_UIInputEvent_GetToolType(event);
        if (sourceType == UI_INPUT_EVENT_SOURCE_TYPE_MOUSE ||
            toolType == UI_INPUT_EVENT_TOOL_TYPE_MOUSE) {
            constexpr double kWheelStepDegrees = 15.0;
            const int scrollLines = std::max(1, static_cast<int>(std::lround(std::abs(vertical) / kWheelStepDegrees)));
            m_terminal->scrollView(vertical > 0.0 ? scrollLines : -scrollLines);
            return;
        }

        m_axisScrollRemainderY += vertical;
        const int scrollLines = static_cast<int>(m_axisScrollRemainderY / cellHeight);
        if (scrollLines == 0) {
            return;
        }

        m_terminal->scrollView(scrollLines);
        m_axisScrollRemainderY += static_cast<double>(scrollLines) * cellHeight;
    }

    void SetResourceManager(napi_env env, napi_value value) {
        m_resourceManager = OH_ResourceManager_InitNativeResourceManager(env, value);
        std::lock_guard<std::mutex> surfaceLock(m_surfaceMutex);
        LoadFontsIfPossibleLocked();
        TryInitializeTerminalLocked();
    }

    void SetFilesDir(const std::string& filesDir) {
        m_filesDir = filesDir;
    }

    void SetDensity(double density) {
        m_density = static_cast<float>(density > 0 ? density : 1.0);
        if (m_renderer) {
            m_renderer->setDensity(m_density);
        }

        if (m_terminal && m_windowWidth > 0 && m_windowHeight > 0) {
            int cols = 80;
            int rows = 24;
            ComputeTerminalSize(m_windowWidth, m_windowHeight, cols, rows);
            m_terminal->resize(cols, rows);
        }
    }

    void SetWindowInfo(int32_t windowId, double left, double top) {
        std::lock_guard<std::mutex> lock(m_surfaceMutex);
        m_windowId = windowId;
        if (std::isfinite(left) && std::isfinite(top)) {
            m_windowScreenOriginKnown = true;
            m_windowScreenLeft = left;
            m_windowScreenTop = top;
        }
    }

    void SetSurfaceOrigin(double left, double top) {
        std::lock_guard<std::mutex> lock(m_surfaceMutex);
        if (m_surfaceFrameOriginKnown) {
            return;
        }
        if (std::isfinite(left) && std::isfinite(top)) {
            m_surfaceScreenOriginKnown = true;
            m_surfaceScreenLeft = left;
            m_surfaceScreenTop = top;
        }
    }

    void SetSurfaceFrame(double left, double top, double width, double height) {
        uint32_t pendingWidth = 0;
        uint32_t pendingHeight = 0;
        bool shouldQueueResize = false;
        {
            std::lock_guard<std::mutex> lock(m_surfaceMutex);
            if (std::isfinite(left) && std::isfinite(top)) {
                m_surfaceFrameOriginKnown = true;
                m_surfaceScreenOriginKnown = true;
                m_surfaceScreenLeft = left;
                m_surfaceScreenTop = top;
            }

            if (std::isfinite(width) && std::isfinite(height)) {
                const auto pixelWidth = static_cast<uint32_t>(std::lround(std::max(0.0, width)));
                const auto pixelHeight = static_cast<uint32_t>(std::lround(std::max(0.0, height)));
                if (pixelWidth > 0 && pixelHeight > 0 &&
                    (pixelWidth != m_windowWidth || pixelHeight != m_windowHeight)) {
                    m_windowWidth = pixelWidth;
                    m_windowHeight = pixelHeight;
                    pendingWidth = pixelWidth;
                    pendingHeight = pixelHeight;
                    shouldQueueResize = true;
                }
            }
        }

        if (shouldQueueResize) {
            OH_LOG_INFO(LOG_APP, "Queueing surface frame resize to %ux%u", pendingWidth, pendingHeight);
            std::lock_guard<std::mutex> renderLock(m_renderMutex);
            m_pendingWidth = pendingWidth;
            m_pendingHeight = pendingHeight;
            m_resizePending = true;
            m_renderDirty = true;
            m_renderCv.notify_one();
        }
    }

    void WriteInput(const std::string& data) {
        if (m_terminal && !data.empty()) {
            m_terminal->writeInput(data.data(), data.size());
        }
    }

    void FeedOutput(const std::string& data) {
        if (m_terminal && !data.empty()) {
            m_terminal->feedOutput(data.data(), data.size());
        }
    }

    std::string DrainPendingInput() {
        std::lock_guard<std::mutex> lock(m_inputMutex);
        std::string drained;
        drained.swap(m_pendingInput);
        return drained;
    }

    void SetInputCallback(napi_env env, napi_value callback)
    {
        napi_threadsafe_function previous = nullptr;
        napi_threadsafe_function next = nullptr;

        if (callback != nullptr) {
            napi_value resourceName = nullptr;
            napi_create_string_utf8(env, "terminalInput", NAPI_AUTO_LENGTH, &resourceName);
            napi_create_threadsafe_function(
                env,
                callback,
                nullptr,
                resourceName,
                0,
                1,
                nullptr,
                nullptr,
                nullptr,
                [](napi_env callbackEnv, napi_value jsCallback, void*, void* data) {
                    std::unique_ptr<std::string> input(static_cast<std::string*>(data));
                    if (!callbackEnv || !jsCallback || !input || input->empty()) {
                        return;
                    }

                    napi_value undefined = nullptr;
                    napi_get_undefined(callbackEnv, &undefined);
                    napi_value argv[1] = {nullptr};
                    napi_create_string_utf8(
                        callbackEnv,
                        input->c_str(),
                        input->size(),
                        &argv[0]);
                    napi_call_function(callbackEnv, undefined, jsCallback, 1, argv, nullptr);
                },
                &next);
        }

        std::string buffered;
        {
            std::lock_guard<std::mutex> lock(m_inputMutex);
            previous = m_inputTsfn;
            m_inputTsfn = next;
            if (m_inputTsfn != nullptr && !m_pendingInput.empty()) {
                buffered.swap(m_pendingInput);
            }
        }

        if (previous != nullptr) {
            napi_release_threadsafe_function(previous, napi_tsfn_abort);
        }
        if (m_inputTsfn != nullptr && !buffered.empty()) {
            EmitInput(buffered);
        }
    }

    void ClearInputCallback()
    {
        napi_threadsafe_function previous = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_inputMutex);
            previous = m_inputTsfn;
            m_inputTsfn = nullptr;
        }
        if (previous != nullptr) {
            napi_release_threadsafe_function(previous, napi_tsfn_abort);
        }
    }

    std::string DrainPendingLinkActivation() {
        std::lock_guard<std::mutex> lock(m_inputMutex);
        std::string drained;
        drained.swap(m_pendingLinkActivation);
        return drained;
    }

    std::string DrainPendingContextMenuRequest() {
        std::lock_guard<std::mutex> lock(m_inputMutex);
        std::string drained;
        drained.swap(m_pendingContextMenuRequest);
        return drained;
    }

    void ResizeTerminal(int cols, int rows) {
        if (m_terminal) {
            m_terminal->resize(cols, rows);
        }
    }

    std::string GetScreenContent() const {
        return m_terminal ? m_terminal->getScreenContent() : std::string();
    }

    void GetCursorPosition(int& row, int& col) const {
        row = 0;
        col = 0;
        if (m_terminal) {
            m_terminal->getCursorPosition(row, col);
        }
    }

    void GetTerminalSize(int& cols, int& rows) const {
        cols = 0;
        rows = 0;
        if (m_terminal) {
            cols = m_terminal->getCols();
            rows = m_terminal->getRows();
        }
    }

    float GetCellWidth() const {
        if (m_renderer) {
            return m_renderer->getCellWidth();
        }
        return std::max(1.0f, m_fontSize * 0.6f * std::max(1.0f, m_density));
    }

    float GetCellHeight() const {
        if (m_renderer) {
            return m_renderer->getCellHeight();
        }
        return std::max(1.0f, m_fontSize * 1.2f * std::max(1.0f, m_density));
    }

    void ScrollView(int delta) {
        if (m_terminal) {
            m_terminal->scrollView(delta);
        }
    }

    void ResetScroll() {
        if (m_terminal) {
            m_terminal->resetViewScroll();
        }
    }

    int GetScrollbackSize() const {
        return m_terminal ? m_terminal->getScrollbackSize() : 0;
    }

    void StartSelection(int row, int col) {
        if (m_terminal) {
            m_terminal->startSelection(row, col);
        }
    }

    void UpdateSelection(int row, int col) {
        if (m_terminal) {
            m_terminal->updateSelection(row, col);
        }
    }

    void ClearSelection() {
        if (m_terminal) {
            m_terminal->clearSelection();
        }
    }

    std::string GetSelectedText() const {
        return m_terminal ? m_terminal->getSelectedText() : std::string();
    }

    void StartSearch(const std::string& query) {
        if (m_terminal) {
            m_terminal->startSearch(query);
        }
    }

    void SearchSelection() {
        if (m_terminal) {
            m_terminal->searchSelection();
        }
    }

    void UpdateSearch(const std::string& query) {
        if (m_terminal) {
            m_terminal->updateSearch(query);
        }
    }

    void NavigateSearch(bool next) {
        if (m_terminal) {
            m_terminal->navigateSearch(next);
        }
    }

    void EndSearch() {
        if (m_terminal) {
            m_terminal->endSearch();
        }
    }

    TerminalSearchStatus GetSearchStatus() const {
        return m_terminal ? m_terminal->getSearchStatus() : TerminalSearchStatus {};
    }

    bool LoadTheme(const std::string& themeName) {
        if (!m_resourceManager || !m_terminal) {
            return false;
        }

        std::string themePath = "themes/";
        themePath += themeName;
        RawFile* file = OH_ResourceManager_OpenRawFile(m_resourceManager, themePath.c_str());
        if (!file) {
            OH_LOG_ERROR(LOG_APP, "Failed to open theme file: %s", themePath.c_str());
            return false;
        }

        const size_t len = OH_ResourceManager_GetRawFileSize(file);
        std::vector<char> data(len);
        OH_ResourceManager_ReadRawFile(file, data.data(), len);
        OH_ResourceManager_CloseRawFile(file);

        TerminalTheme theme;
        theme.name = themeName;
        if (!ThemeParser::parseThemeFile(data.data(), len, theme)) {
            return false;
        }

        m_terminal->setTheme(theme);
        if (m_renderer) {
            m_renderer->setColors(theme.background, theme.foreground);
            m_renderer->setCursorColors(theme.cursorColor, theme.cursorText);
        }
        RequestRender();
        return true;
    }

    std::vector<std::string> GetThemeList() const {
        std::vector<std::string> themes;
        if (!m_resourceManager) {
            return themes;
        }

        RawDir* themeDir = OH_ResourceManager_OpenRawDir(m_resourceManager, "themes");
        if (!themeDir) {
            return themes;
        }

        const int count = OH_ResourceManager_GetRawFileCount(themeDir);
        themes.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) {
            const char* fileName = OH_ResourceManager_GetRawFileName(themeDir, i);
            if (fileName && fileName[0] != '\0') {
                themes.emplace_back(fileName);
            }
        }
        OH_ResourceManager_CloseRawDir(themeDir);
        std::sort(themes.begin(), themes.end());
        return themes;
    }

    void SetConfig(int fontSize, int scrollbackLines, uint32_t bgColor, uint32_t fgColor, int cursorStyle, bool cursorBlink) {
        m_fontSize = static_cast<float>(fontSize);

        if (m_renderer) {
            m_renderer->setFontSize(m_fontSize);
            m_renderer->setColors(bgColor, fgColor);
            m_renderer->setCursorStyle(cursorStyle, cursorBlink);
        }

        if (m_terminal) {
            const TerminalTheme theme = m_terminal->getTheme();
            m_terminal->setTheme(theme);
            if (m_renderer) {
                m_renderer->setColors(theme.background, theme.foreground);
                m_renderer->setCursorColors(theme.cursorColor, theme.cursorText);
            }
            m_terminal->setMaxScrollback(scrollbackLines);

            if (m_windowWidth > 0 && m_windowHeight > 0) {
                int cols = 80;
                int rows = 24;
                ComputeTerminalSize(m_windowWidth, m_windowHeight, cols, rows);
                m_terminal->resize(cols, rows);
            }
        }
        RequestRender();
    }

    bool IsRendererReady() const {
        return m_rendererReady;
    }

    const std::string& GetRendererError() const {
        return m_rendererError;
    }

private:
    static void HandleImeGetTextConfig(InputMethod_TextEditorProxy* proxy, InputMethod_TextConfig* config)
    {
        if (TerminalHost* host = FindImeHost(proxy)) {
            host->FillImeTextConfig(config);
        }
    }

    static void HandleImeInsertText(InputMethod_TextEditorProxy* proxy, const char16_t* text, size_t length)
    {
        if (TerminalHost* host = FindImeHost(proxy)) {
            host->InsertImeText(text, length);
        }
    }

    static void HandleImeDeleteForward(InputMethod_TextEditorProxy* proxy, int32_t length)
    {
        if (TerminalHost* host = FindImeHost(proxy)) {
            host->DeleteImeForward(length);
        }
    }

    static void HandleImeDeleteBackward(InputMethod_TextEditorProxy* proxy, int32_t length)
    {
        if (TerminalHost* host = FindImeHost(proxy)) {
            host->DeleteImeBackward(length);
        }
    }

    static void HandleImeKeyboardStatus(InputMethod_TextEditorProxy* proxy, InputMethod_KeyboardStatus keyboardStatus)
    {
        if (TerminalHost* host = FindImeHost(proxy)) {
            host->HandleImeKeyboardStatus(keyboardStatus);
        }
    }

    static void HandleImeEnterKey(InputMethod_TextEditorProxy* proxy, InputMethod_EnterKeyType enterKeyType)
    {
        if (TerminalHost* host = FindImeHost(proxy)) {
            host->HandleImeEnterKey(enterKeyType);
        }
    }

    static void HandleImeMoveCursor(InputMethod_TextEditorProxy* proxy, InputMethod_Direction direction)
    {
        if (TerminalHost* host = FindImeHost(proxy)) {
            host->HandleImeMoveCursor(direction);
        }
    }

    static void HandleImeSetSelection(InputMethod_TextEditorProxy* proxy, int32_t start, int32_t end)
    {
        if (TerminalHost* host = FindImeHost(proxy)) {
            host->HandleImeSetSelection(start, end);
        }
    }

    static void HandleImeExtendAction(InputMethod_TextEditorProxy* proxy, InputMethod_ExtendAction action)
    {
        if (TerminalHost* host = FindImeHost(proxy)) {
            host->HandleImeExtendAction(action);
        }
    }

    static void HandleImeGetLeftText(InputMethod_TextEditorProxy* proxy, int32_t number, char16_t text[], size_t* length)
    {
        if (TerminalHost* host = FindImeHost(proxy)) {
            host->GetImeLeftText(number, text, length);
        }
    }

    static void HandleImeGetRightText(InputMethod_TextEditorProxy* proxy, int32_t number, char16_t text[], size_t* length)
    {
        if (TerminalHost* host = FindImeHost(proxy)) {
            host->GetImeRightText(number, text, length);
        }
    }

    static int32_t HandleImeGetTextIndexAtCursor(InputMethod_TextEditorProxy* proxy)
    {
        if (TerminalHost* host = FindImeHost(proxy)) {
            return host->GetImeTextIndexAtCursor();
        }
        return 0;
    }

    static int32_t HandleImePrivateCommand(
        InputMethod_TextEditorProxy* proxy, InputMethod_PrivateCommand* privateCommand[], size_t size)
    {
        if (TerminalHost* host = FindImeHost(proxy)) {
            return host->HandleImePrivateCommand(privateCommand, size);
        }
        return 0;
    }

    static int32_t HandleImeSetPreviewText(
        InputMethod_TextEditorProxy* proxy, const char16_t text[], size_t length, int32_t start, int32_t end)
    {
        if (TerminalHost* host = FindImeHost(proxy)) {
            return host->HandleImeSetPreviewText(text, length, start, end);
        }
        return 0;
    }

    static void HandleImeFinishPreview(InputMethod_TextEditorProxy* proxy)
    {
        if (TerminalHost* host = FindImeHost(proxy)) {
            host->HandleImeFinishPreview();
        }
    }

    void RequestRender() {
        {
            std::lock_guard<std::mutex> lock(m_renderMutex);
            m_renderDirty = true;
        }
        m_renderCv.notify_one();
    }

    void StartRenderLoop() {
        if (m_renderThreadRunning) {
            return;
        }

        m_renderThreadRunning = true;
        m_renderThread = std::thread([this]() {
            std::unique_lock<std::mutex> lock(m_renderMutex);
            while (m_renderThreadRunning) {
                const bool animateCursor = m_renderer && m_renderer->cursorBlinkEnabled();
                if (animateCursor) {
                    m_renderCv.wait_for(lock, CURSOR_BLINK_TICK, [this]() {
                        return !m_renderThreadRunning || m_renderDirty || m_resizePending;
                    });
                } else {
                    m_renderCv.wait(lock, [this]() {
                        return !m_renderThreadRunning || m_renderDirty || m_resizePending;
                    });
                }

                if (!m_renderThreadRunning) {
                    break;
                }

                const bool shouldResize = m_resizePending;
                const uint32_t resizeWidth = m_pendingWidth;
                const uint32_t resizeHeight = m_pendingHeight;
                const bool shouldRender = m_renderDirty || shouldResize || animateCursor;
                m_resizePending = false;
                m_renderDirty = false;
                lock.unlock();

                {
                    std::lock_guard<std::mutex> surfaceLock(m_surfaceMutex);
                    if (m_renderer && shouldResize) {
                        m_renderer->resize(resizeWidth, resizeHeight);
                        if (m_terminal && resizeWidth > 0 && resizeHeight > 0) {
                            int cols = 80;
                            int rows = 24;
                            ComputeTerminalSize(resizeWidth, resizeHeight, cols, rows);
                            m_terminal->resize(cols, rows);
                            m_terminal->resetViewScroll();
                        }
                    }
                    if (shouldRender && m_renderer && m_terminal && m_surfaceReady) {
                        m_terminal->drawFrame();
                        NotifyImeStateLocked();
                    }
                }

                lock.lock();
            }
        });
    }

    void StopRenderLoop() {
        {
            std::lock_guard<std::mutex> lock(m_renderMutex);
            m_renderThreadRunning = false;
            m_renderDirty = false;
            m_resizePending = false;
        }
        m_renderCv.notify_all();
        if (m_renderThread.joinable()) {
            m_renderThread.join();
        }
    }

    void ComputeTerminalSize(uint32_t width, uint32_t height, int& cols, int& rows) const {
        cols = static_cast<int>(width / GetCellWidth());
        rows = static_cast<int>(height / GetCellHeight());
        if (cols <= 0) {
            cols = 80;
        }
        if (rows <= 0) {
            rows = 24;
        }
    }

    void MapPointToCell(float x, float y, float cellWidth, float cellHeight, int& row, int& col) const {
        row = static_cast<int>(y / cellHeight);
        col = static_cast<int>(x / cellWidth);
        row = std::max(0, row);
        col = std::max(0, col);
    }

    bool QueueLinkActivationAtPoint(float x, float y, float cellWidth, float cellHeight) {
        if (!m_terminal) {
            return false;
        }

        int row = 0;
        int col = 0;
        MapPointToCell(x, y, cellWidth, cellHeight, row, col);
        const std::string link = m_terminal->getLinkAt(row, col);
        if (link.empty()) {
            return false;
        }

        std::lock_guard<std::mutex> lock(m_inputMutex);
        m_pendingLinkActivation = link;
        return true;
    }

    void QueueContextMenuRequest(float x, float y) {
        std::lock_guard<std::mutex> lock(m_inputMutex);
        const int32_t ix = static_cast<int32_t>(std::lround(x));
        const int32_t iy = static_cast<int32_t>(std::lround(y));
        m_pendingContextMenuRequest = std::to_string(ix) + "," + std::to_string(iy);
    }

    void ScrollViewportByPointerDelta(float deltaY, float cellHeight) {
        if (!m_terminal || !std::isfinite(deltaY) || !std::isfinite(cellHeight) || cellHeight <= 0.0f) {
            return;
        }

        m_touchScrollRemainderY += deltaY;
        const int scrollLines = static_cast<int>(m_touchScrollRemainderY / cellHeight);
        if (scrollLines == 0) {
            return;
        }

        m_terminal->scrollView(scrollLines);
        m_touchScrollRemainderY -= static_cast<float>(scrollLines) * cellHeight;
    }

    void TryInitializeTerminalLocked() {
        if (!m_surfaceReady) {
            return;
        }

        LoadFontsIfPossibleLocked();

        if (!m_terminal) {
            int cols = 80;
            int rows = 24;
            ComputeTerminalSize(m_windowWidth, m_windowHeight, cols, rows);
            m_terminal = new Terminal(cols, rows);
            m_terminal->setRenderer(m_renderer);
            m_terminal->setRenderRequestCallback([this]() { RequestRender(); });
            m_terminal->setInputCallback([this](const std::string& data) {
                EmitInput(data);
            });
            m_terminal->start();
            AttachImeLocked();
            NotifyImeStateLocked();
        } else if (m_renderer) {
            m_terminal->setRenderer(m_renderer);
            AttachImeLocked();
        }
    }

    void EmitInput(const std::string& data)
    {
        if (data.empty()) {
            return;
        }

        ExampleDriverWriteInputFn nativeSink = ResolveExampleDriverWriteInput();
        if (nativeSink != nullptr && nativeSink(data.data(), data.size())) {
            return;
        }

        napi_threadsafe_function tsfn = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_inputMutex);
            if (m_inputTsfn == nullptr) {
                m_pendingInput += data;
                return;
            }
            tsfn = m_inputTsfn;
        }

        auto* copy = new std::string(data);
        const napi_status status = napi_call_threadsafe_function(tsfn, copy, napi_tsfn_nonblocking);
        if (status != napi_ok) {
            delete copy;
            std::lock_guard<std::mutex> lock(m_inputMutex);
            m_pendingInput += data;
        }
    }

    void LoadFontsIfPossibleLocked() {
        if (m_renderer && m_resourceManager && !m_fontsLoaded) {
            m_renderer->loadFontAtlas(m_resourceManager, m_filesDir);
            m_fontsLoaded = true;
        }
    }

    void CleanupSurfaceLocked() {
        ReleaseNativeWindowLocked(m_nativeWindow);
        m_nativeWindow = nullptr;
        m_windowWidth = 0;
        m_windowHeight = 0;
        m_surfaceReady = false;
        m_fontsLoaded = false;

        if (m_renderer) {
            m_renderer->cleanup();
            delete m_renderer;
            m_renderer = nullptr;
        }
    }

    void AttachImeLocked()
    {
        if (m_imeInputMethodProxy != nullptr) {
            return;
        }

        if (m_imeTextEditorProxy == nullptr) {
            m_imeTextEditorProxy = OH_TextEditorProxy_Create();
            if (m_imeTextEditorProxy == nullptr) {
                OH_LOG_ERROR(LOG_APP, "Failed to create IME text editor proxy");
                return;
            }

            RegisterImeHost(m_imeTextEditorProxy, this);
            OH_TextEditorProxy_SetGetTextConfigFunc(m_imeTextEditorProxy, HandleImeGetTextConfig);
            OH_TextEditorProxy_SetInsertTextFunc(m_imeTextEditorProxy, HandleImeInsertText);
            OH_TextEditorProxy_SetDeleteForwardFunc(m_imeTextEditorProxy, HandleImeDeleteForward);
            OH_TextEditorProxy_SetDeleteBackwardFunc(m_imeTextEditorProxy, HandleImeDeleteBackward);
            OH_TextEditorProxy_SetSendKeyboardStatusFunc(m_imeTextEditorProxy, HandleImeKeyboardStatus);
            OH_TextEditorProxy_SetSendEnterKeyFunc(m_imeTextEditorProxy, HandleImeEnterKey);
            OH_TextEditorProxy_SetMoveCursorFunc(m_imeTextEditorProxy, HandleImeMoveCursor);
            OH_TextEditorProxy_SetHandleSetSelectionFunc(m_imeTextEditorProxy, HandleImeSetSelection);
            OH_TextEditorProxy_SetHandleExtendActionFunc(m_imeTextEditorProxy, HandleImeExtendAction);
            OH_TextEditorProxy_SetGetLeftTextOfCursorFunc(m_imeTextEditorProxy, HandleImeGetLeftText);
            OH_TextEditorProxy_SetGetRightTextOfCursorFunc(m_imeTextEditorProxy, HandleImeGetRightText);
            OH_TextEditorProxy_SetGetTextIndexAtCursorFunc(m_imeTextEditorProxy, HandleImeGetTextIndexAtCursor);
            OH_TextEditorProxy_SetReceivePrivateCommandFunc(m_imeTextEditorProxy, HandleImePrivateCommand);
            OH_TextEditorProxy_SetSetPreviewTextFunc(m_imeTextEditorProxy, HandleImeSetPreviewText);
            OH_TextEditorProxy_SetFinishTextPreviewFunc(m_imeTextEditorProxy, HandleImeFinishPreview);
        }

        InputMethod_AttachOptions* options = OH_AttachOptions_Create(false);
        if (options == nullptr) {
            OH_LOG_ERROR(LOG_APP, "Failed to create IME attach options");
            return;
        }

        InputMethod_ErrorCode rc =
            OH_InputMethodController_Attach(m_imeTextEditorProxy, options, &m_imeInputMethodProxy);
        OH_AttachOptions_Destroy(options);
        if (rc != IME_ERR_OK) {
            OH_LOG_ERROR(LOG_APP, "Failed to attach IME: %{public}d", static_cast<int>(rc));
            m_imeInputMethodProxy = nullptr;
        }
    }

    void DetachImeLocked()
    {
        HideImeLocked();
        if (m_imeInputMethodProxy != nullptr) {
            OH_InputMethodController_Detach(m_imeInputMethodProxy);
            m_imeInputMethodProxy = nullptr;
        }
        if (m_imeTextEditorProxy != nullptr) {
            UnregisterImeHost(m_imeTextEditorProxy);
            OH_TextEditorProxy_Destroy(m_imeTextEditorProxy);
            m_imeTextEditorProxy = nullptr;
        }
        m_wantsIme = false;
    }

    void ShowImeLocked(InputMethod_RequestKeyboardReason reason)
    {
        m_wantsIme = true;
        if (m_imeInputMethodProxy == nullptr) {
            AttachImeLocked();
        }
        if (m_imeInputMethodProxy == nullptr) {
            return;
        }

        InputMethod_AttachOptions* options =
            OH_AttachOptions_CreateWithRequestKeyboardReason(true, reason);
        if (options == nullptr) {
            return;
        }
        const InputMethod_ErrorCode rc = OH_InputMethodProxy_ShowTextInput(m_imeInputMethodProxy, options);
        OH_AttachOptions_Destroy(options);
        if (rc == IME_ERR_OK) {
            m_imeVisible = true;
        } else {
            OH_LOG_WARN(LOG_APP, "Failed to show IME: %{public}d", static_cast<int>(rc));
        }
    }

    void HideImeLocked()
    {
        if (m_imeInputMethodProxy == nullptr || !m_imeVisible) {
            return;
        }
        OH_InputMethodProxy_HideKeyboard(m_imeInputMethodProxy);
        m_imeVisible = false;
    }

    void FillImeTextConfig(InputMethod_TextConfig* config)
    {
        if (config == nullptr) {
            return;
        }

        OH_TextConfig_SetInputType(config, IME_TEXT_INPUT_TYPE_TEXT);
        OH_TextConfig_SetEnterKeyType(config, IME_ENTER_KEY_NEWLINE);
        OH_TextConfig_SetPreviewTextSupport(config, true);

        std::u16string text;
        int32_t cursorIndex = 0;
        CaptureImeSurroundingText(text, cursorIndex);
        OH_TextConfig_SetSelection(config, cursorIndex, cursorIndex);
        if (m_windowId >= 0) {
            OH_TextConfig_SetWindowId(config, m_windowId);
        }
    }

    void InsertImeText(const char16_t* text, size_t length)
    {
        if (m_terminal == nullptr || text == nullptr || length == 0) {
            return;
        }
        const std::string utf8 = Utf16ToUtf8(text, length);
        if (!utf8.empty()) {
            m_terminal->writeInput(utf8.data(), utf8.size());
        }
    }

    void DeleteImeForward(int32_t length)
    {
        if (m_terminal == nullptr || length <= 0) {
            return;
        }
        for (int32_t i = 0; i < length; ++i) {
            static constexpr char kDeleteSeq[] = "\x1b[3~";
            m_terminal->writeInput(kDeleteSeq, sizeof(kDeleteSeq) - 1);
        }
    }

    void DeleteImeBackward(int32_t length)
    {
        if (m_terminal == nullptr || length <= 0) {
            return;
        }
        for (int32_t i = 0; i < length; ++i) {
            static constexpr char kBackspace[] = "\x7f";
            m_terminal->writeInput(kBackspace, sizeof(kBackspace) - 1);
        }
    }

    void HandleImeKeyboardStatus(InputMethod_KeyboardStatus keyboardStatus)
    {
        std::lock_guard<std::mutex> lock(m_surfaceMutex);
        m_imeVisible = keyboardStatus == IME_KEYBOARD_STATUS_SHOW;
    }

    void HandleImeEnterKey(InputMethod_EnterKeyType)
    {
        if (m_terminal == nullptr) {
            return;
        }
        static constexpr char kEnter[] = "\r";
        m_terminal->writeInput(kEnter, sizeof(kEnter) - 1);
    }

    void HandleImeMoveCursor(InputMethod_Direction direction)
    {
        if (m_terminal == nullptr) {
            return;
        }

        const char* sequence = nullptr;
        size_t length = 0;
        switch (direction) {
            case IME_DIRECTION_LEFT:
                sequence = "\x1b[D";
                length = 3;
                break;
            case IME_DIRECTION_RIGHT:
                sequence = "\x1b[C";
                length = 3;
                break;
            case IME_DIRECTION_UP:
                sequence = "\x1b[A";
                length = 3;
                break;
            case IME_DIRECTION_DOWN:
                sequence = "\x1b[B";
                length = 3;
                break;
            case IME_DIRECTION_NONE:
            default:
                return;
        }
        m_terminal->writeInput(sequence, length);
    }

    void HandleImeSetSelection(int32_t, int32_t)
    {
    }

    void HandleImeExtendAction(InputMethod_ExtendAction action)
    {
        if (m_terminal == nullptr) {
            return;
        }

        switch (action) {
            case IME_EXTEND_ACTION_SELECT_ALL:
                m_terminal->startSelection(0, 0);
                m_terminal->updateSelection(m_terminal->getRows(), m_terminal->getCols());
                break;
            case IME_EXTEND_ACTION_COPY:
            case IME_EXTEND_ACTION_CUT:
            case IME_EXTEND_ACTION_PASTE:
            default:
                break;
        }
    }

    void GetImeLeftText(int32_t number, char16_t text[], size_t* length)
    {
        FillImeTextSlice(number, true, text, length);
    }

    void GetImeRightText(int32_t number, char16_t text[], size_t* length)
    {
        FillImeTextSlice(number, false, text, length);
    }

    int32_t GetImeTextIndexAtCursor()
    {
        std::u16string text;
        int32_t cursorIndex = 0;
        CaptureImeSurroundingText(text, cursorIndex);
        return cursorIndex;
    }

    int32_t HandleImePrivateCommand(InputMethod_PrivateCommand*[], size_t)
    {
        return 0;
    }

    int32_t HandleImeSetPreviewText(const char16_t[], size_t, int32_t, int32_t)
    {
        return 0;
    }

    void HandleImeFinishPreview()
    {
    }

    void FillImeTextSlice(int32_t number, bool left, char16_t text[], size_t* length)
    {
        if (length == nullptr) {
            return;
        }

        std::u16string surrounding;
        int32_t cursorIndex = 0;
        CaptureImeSurroundingText(surrounding, cursorIndex);
        const size_t safeCursor = static_cast<size_t>(std::max(0, cursorIndex));
        const size_t count = static_cast<size_t>(std::max(0, number));
        std::u16string slice;
        if (left) {
            const size_t start = safeCursor > count ? safeCursor - count : 0;
            slice = surrounding.substr(start, safeCursor - start);
        } else {
            const size_t start = std::min(safeCursor, surrounding.size());
            slice = surrounding.substr(start, count);
        }

        if (text != nullptr && !slice.empty()) {
            std::copy(slice.begin(), slice.end(), text);
        }
        *length = slice.size();
    }

    void CaptureImeSurroundingText(std::u16string& text, int32_t& cursorIndex) const
    {
        text.clear();
        cursorIndex = 0;
        if (m_terminal == nullptr) {
            return;
        }

        std::string screen = m_terminal->getScreenContent();
        std::vector<std::string> lines;
        size_t start = 0;
        while (start <= screen.size()) {
            size_t end = screen.find('\n', start);
            if (end == std::string::npos) {
                lines.push_back(screen.substr(start));
                break;
            }
            lines.push_back(screen.substr(start, end - start));
            start = end + 1;
        }

        int row = 0;
        int col = 0;
        m_terminal->getCursorPosition(row, col);
        if (lines.empty()) {
            return;
        }

        const size_t lineIndex = static_cast<size_t>(std::clamp(row, 0, static_cast<int>(lines.size() - 1)));
        text = Utf8ToUtf16(lines[lineIndex]);
        cursorIndex = std::clamp(col, 0, static_cast<int32_t>(text.size()));
    }

    void NotifyImeStateLocked()
    {
        if (m_imeInputMethodProxy == nullptr || m_terminal == nullptr || !m_surfaceReady) {
            return;
        }

        std::u16string surrounding;
        int32_t cursorIndex = 0;
        CaptureImeSurroundingText(surrounding, cursorIndex);
        OH_InputMethodProxy_NotifySelectionChange(
            m_imeInputMethodProxy,
            surrounding.empty() ? nullptr : surrounding.data(),
            surrounding.size(),
            cursorIndex,
            cursorIndex);

        if (m_windowScreenOriginKnown || m_surfaceScreenOriginKnown || m_lastImeBaseKnown) {
            double baseLeft = 0.0;
            double baseTop = 0.0;
            bool baseKnown = false;
            double surfaceBaseLeft = m_surfaceScreenLeft;
            double surfaceBaseTop = m_surfaceScreenTop;
            const bool surfaceBaseKnown = m_surfaceScreenOriginKnown;
            double windowBaseLeft = m_windowScreenLeft;
            double windowBaseTop = m_windowScreenTop;
            const bool windowBaseKnown = m_windowScreenOriginKnown;
            double xOffset = 0.0;
            double yOffset = 0.0;
            bool xcomponentOffsetKnown = false;
            if (!m_surfaceScreenOriginKnown &&
                m_component != nullptr && m_nativeWindow != nullptr &&
                OH_NativeXComponent_GetXComponentOffset(m_component, m_nativeWindow, &xOffset, &yOffset) ==
                    OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
                xcomponentOffsetKnown = true;
            }

            const bool surfaceLooksWindowLocal =
                m_surfaceFrameOriginKnown &&
                surfaceBaseKnown &&
                windowBaseKnown;

            if (surfaceLooksWindowLocal) {
                baseLeft = windowBaseLeft + surfaceBaseLeft;
                baseTop = windowBaseTop + surfaceBaseTop;
                baseKnown = true;
            } else if (m_surfaceScreenOriginKnown && !IsNearOrigin(m_surfaceScreenLeft, m_surfaceScreenTop)) {
                baseLeft = m_surfaceScreenLeft;
                baseTop = m_surfaceScreenTop;
                baseKnown = true;
            } else if (m_windowScreenOriginKnown && xcomponentOffsetKnown) {
                baseLeft = m_windowScreenLeft + xOffset;
                baseTop = m_windowScreenTop + yOffset;
                baseKnown = true;
            } else if (m_lastImeBaseKnown) {
                baseLeft = m_lastImeBaseLeft;
                baseTop = m_lastImeBaseTop;
                baseKnown = true;
            }

            if (!baseKnown) {
                return;
            }

            const bool fallbackToLastImeBase =
                m_lastImeBaseKnown &&
                (std::fabs(baseLeft - m_lastImeBaseLeft) > 96.0 ||
                    std::fabs(baseTop - m_lastImeBaseTop) > 96.0) &&
                (IsNearOrigin(baseLeft, baseTop) || !m_surfaceScreenOriginKnown);
            if (fallbackToLastImeBase) {
                baseLeft = m_lastImeBaseLeft;
                baseTop = m_lastImeBaseTop;
            } else {
                m_lastImeBaseKnown = true;
                m_lastImeBaseLeft = baseLeft;
                m_lastImeBaseTop = baseTop;
            }

            float cursorLeft = 0.0f;
            float cursorTop = 0.0f;
            float cursorWidth = GetCellWidth();
            float cursorHeight = GetCellHeight();
            bool cursorRectValid = false;
            if (m_renderer != nullptr) {
                m_renderer->getLastCursorRect(
                    cursorLeft,
                    cursorTop,
                    cursorWidth,
                    cursorHeight,
                    cursorRectValid);
            }

            if (!cursorRectValid) {
                int row = 0;
                int col = 0;
                m_terminal->getCursorPosition(row, col);
                cursorLeft = static_cast<float>(std::max(0, col)) * GetCellWidth();
                cursorTop = static_cast<float>(std::max(0, row)) * GetCellHeight();
                cursorWidth = GetCellWidth();
                cursorHeight = GetCellHeight();
            }

            const double left = baseLeft + static_cast<double>(cursorLeft);
            const double top = baseTop + static_cast<double>(cursorTop);
            const bool shouldLogImeRect =
                !m_lastImeLogKnown ||
                m_lastLoggedWindowId != m_windowId ||
                m_lastLoggedFallbackToLastBase != fallbackToLastImeBase ||
                std::fabs(m_lastLoggedBaseLeft - baseLeft) > 1.0 ||
                std::fabs(m_lastLoggedBaseTop - baseTop) > 1.0 ||
                std::fabs(m_lastLoggedSentLeft - left) > 1.0 ||
                std::fabs(m_lastLoggedSentTop - top) > 1.0;
            if (shouldLogImeRect) {
                OH_LOG_INFO(LOG_APP,
                    "IME cursor rect windowId=%{public}d base=(%{public}.1f,%{public}.1f,%{public}d,%{public}d) surfaceBase=(%{public}.1f,%{public}.1f,%{public}d) windowBase=(%{public}.1f,%{public}.1f,%{public}d) lastBase=(%{public}.1f,%{public}.1f,%{public}d) xcompOffset=(%{public}.1f,%{public}.1f,%{public}d) cursor=(%{public}.1f,%{public}.1f,%{public}.1f,%{public}.1f) sent=(%{public}.1f,%{public}.1f,%{public}.1f,%{public}.1f) rendererRect=%{public}d",
                    static_cast<int>(m_windowId),
                    baseLeft,
                    baseTop,
                    fallbackToLastImeBase ? 1 : 0,
                    surfaceLooksWindowLocal ? 1 : 0,
                    surfaceBaseLeft,
                    surfaceBaseTop,
                    surfaceBaseKnown ? 1 : 0,
                    windowBaseLeft,
                    windowBaseTop,
                    windowBaseKnown ? 1 : 0,
                    m_lastImeBaseLeft,
                    m_lastImeBaseTop,
                    m_lastImeBaseKnown ? 1 : 0,
                    xOffset,
                    yOffset,
                    xcomponentOffsetKnown ? 1 : 0,
                    static_cast<double>(cursorLeft),
                    static_cast<double>(cursorTop),
                    static_cast<double>(cursorWidth),
                    static_cast<double>(cursorHeight),
                    left,
                    top,
                    static_cast<double>(cursorWidth),
                    static_cast<double>(cursorHeight),
                    cursorRectValid ? 1 : 0);
                m_lastImeLogKnown = true;
                m_lastLoggedWindowId = m_windowId;
                m_lastLoggedBaseLeft = baseLeft;
                m_lastLoggedBaseTop = baseTop;
                m_lastLoggedSentLeft = left;
                m_lastLoggedSentTop = top;
                m_lastLoggedFallbackToLastBase = fallbackToLastImeBase;
                m_lastLoggedSurfaceLooksWindowLocal = surfaceLooksWindowLocal;
            }
            InputMethod_CursorInfo* cursorInfo =
                OH_CursorInfo_Create(left, top, static_cast<double>(cursorWidth), static_cast<double>(cursorHeight));
            if (cursorInfo != nullptr) {
                OH_InputMethodProxy_NotifyCursorUpdate(m_imeInputMethodProxy, cursorInfo);
                OH_CursorInfo_Destroy(cursorInfo);
            }
        }
    }

    OH_NativeXComponent* m_component = nullptr;
    Terminal* m_terminal = nullptr;
    Renderer* m_renderer = nullptr;
    NativeResourceManager* m_resourceManager = nullptr;
    bool m_rendererReady = false;
    std::string m_rendererError;
    float m_density = 1.0f;
    float m_fontSize = 14.0f;

    float m_lastTouchY = 0.0f;
    bool m_isTouching = false;
    bool m_isSelecting = false;
    bool m_isMousePressed = false;
    bool m_isMouseSelecting = false;
    bool m_mouseDragged = false;
    bool m_mouseHadSelectionOnPress = false;
    bool m_mousePressOnSelection = false;
    int m_mousePressRow = 0;
    int m_mousePressCol = 0;
    int m_lastClickRow = -1;
    int m_lastClickCol = -1;
    int m_lastClickCount = 0;
    float m_touchStartX = 0.0f;
    float m_touchStartY = 0.0f;
    float m_touchScrollRemainderY = 0.0f;
    double m_axisScrollRemainderY = 0.0;
    uint64_t m_touchStartTime = 0;
    uint64_t m_lastClickTimeMs = 0;

    OHNativeWindow* m_nativeWindow = nullptr;
    uint32_t m_windowWidth = 0;
    uint32_t m_windowHeight = 0;
    int32_t m_windowId = -1;
    bool m_surfaceReady = false;
    bool m_fontsLoaded = false;
    bool m_surfaceScreenOriginKnown = false;
    double m_surfaceScreenLeft = 0.0;
    double m_surfaceScreenTop = 0.0;
    bool m_surfaceFrameOriginKnown = false;
    bool m_windowScreenOriginKnown = false;
    double m_windowScreenLeft = 0.0;
    double m_windowScreenTop = 0.0;
    bool m_lastImeBaseKnown = false;
    double m_lastImeBaseLeft = 0.0;
    double m_lastImeBaseTop = 0.0;
    bool m_lastImeLogKnown = false;
    int32_t m_lastLoggedWindowId = -1;
    double m_lastLoggedBaseLeft = 0.0;
    double m_lastLoggedBaseTop = 0.0;
    double m_lastLoggedSentLeft = 0.0;
    double m_lastLoggedSentTop = 0.0;
    bool m_lastLoggedFallbackToLastBase = false;
    bool m_lastLoggedSurfaceLooksWindowLocal = false;
    std::string m_filesDir;
    std::mutex m_inputMutex;
    std::string m_pendingInput;
    std::string m_pendingLinkActivation;
    std::string m_pendingContextMenuRequest;
    napi_threadsafe_function m_inputTsfn = nullptr;
    InputMethod_TextEditorProxy* m_imeTextEditorProxy = nullptr;
    InputMethod_InputMethodProxy* m_imeInputMethodProxy = nullptr;
    bool m_imeVisible = false;
    bool m_wantsIme = false;

    std::thread m_renderThread;
    std::mutex m_renderMutex;
    std::condition_variable m_renderCv;
    std::mutex m_surfaceMutex;
    bool m_renderThreadRunning = false;
    bool m_renderDirty = false;
    bool m_resizePending = false;
    uint32_t m_pendingWidth = 0;
    uint32_t m_pendingHeight = 0;
};

std::mutex g_hostsMutex;
std::unordered_map<OH_NativeXComponent*, std::unique_ptr<TerminalHost>> g_hosts;

TerminalHost* EnsureHost(OH_NativeXComponent* component) {
    std::lock_guard<std::mutex> lock(g_hostsMutex);
    std::unique_ptr<TerminalHost>& host = g_hosts[component];
    if (!host) {
        host = std::make_unique<TerminalHost>(component);
    }
    return host.get();
}

TerminalHost* FindHost(OH_NativeXComponent* component) {
    std::lock_guard<std::mutex> lock(g_hostsMutex);
    auto it = g_hosts.find(component);
    return it == g_hosts.end() ? nullptr : it->second.get();
}

TerminalHost* GetHostFromCallback(napi_env env, napi_callback_info info, size_t* argc, napi_value* args) {
    void* data = nullptr;
    napi_get_cb_info(env, info, argc, args, nullptr, &data);
    return static_cast<TerminalHost*>(data);
}

void OnSurfaceCreatedCB(OH_NativeXComponent* component, void* window) {
    if (TerminalHost* host = FindHost(component)) {
        host->OnSurfaceCreated(component, window);
    }
}

void OnSurfaceChangedCB(OH_NativeXComponent* component, void* window) {
    if (TerminalHost* host = FindHost(component)) {
        host->OnSurfaceChanged(component, window);
    }
}

void OnSurfaceShowCB(OH_NativeXComponent* component, void* window) {
    if (TerminalHost* host = FindHost(component)) {
        host->OnSurfaceShow(component, window);
    }
}

void OnSurfaceHideCB(OH_NativeXComponent* component, void* window) {
    (void)window;
    if (TerminalHost* host = FindHost(component)) {
        host->OnSurfaceHide();
    }
}

void OnSurfaceDestroyedCB(OH_NativeXComponent* component, void* window) {
    (void)window;
    if (TerminalHost* host = FindHost(component)) {
        host->OnSurfaceDestroyed();
    }
}

void DispatchTouchEventCB(OH_NativeXComponent* component, void* window) {
    if (TerminalHost* host = FindHost(component)) {
        host->DispatchTouchEvent(component, window);
    }
}

void DispatchMouseEventCB(OH_NativeXComponent* component, void* window) {
    if (TerminalHost* host = FindHost(component)) {
        host->DispatchMouseEvent(component, window);
    }
}

void DispatchAxisEventCB(OH_NativeXComponent* component, ArkUI_UIInputEvent* event, ArkUI_UIInputEvent_Type type) {
    if (type != ARKUI_UIINPUTEVENT_TYPE_AXIS) {
        return;
    }
    if (TerminalHost* host = FindHost(component)) {
        host->DispatchAxisEvent(event);
    }
}

bool DispatchKeyEventCB(OH_NativeXComponent* component, void* window) {
    (void)window;
    if (TerminalHost* host = FindHost(component)) {
        return host->DispatchKeyEvent(component);
    }
    return false;
}

static napi_value WriteInput(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    TerminalHost* host = GetHostFromCallback(env, info, &argc, args);
    if (!host || argc < 1) {
        return nullptr;
    }

    size_t strLen = 0;
    napi_get_value_string_utf8(env, args[0], nullptr, 0, &strLen);
    std::vector<char> buffer(strLen + 1, '\0');
    napi_get_value_string_utf8(env, args[0], buffer.data(), buffer.size(), &strLen);
    host->WriteInput(std::string(buffer.data(), strLen));
    return nullptr;
}

static napi_value ResizeTerminal(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    TerminalHost* host = GetHostFromCallback(env, info, &argc, args);
    if (!host || argc < 2) {
        return nullptr;
    }

    int32_t cols = 0;
    int32_t rows = 0;
    napi_get_value_int32(env, args[0], &cols);
    napi_get_value_int32(env, args[1], &rows);
    host->ResizeTerminal(cols, rows);
    return nullptr;
}

static napi_value FeedOutput(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    TerminalHost* host = GetHostFromCallback(env, info, &argc, args);
    if (!host || argc < 1) {
        return nullptr;
    }

    size_t strLen = 0;
    napi_get_value_string_utf8(env, args[0], nullptr, 0, &strLen);
    std::vector<char> buffer(strLen + 1, '\0');
    napi_get_value_string_utf8(env, args[0], buffer.data(), buffer.size(), &strLen);
    host->FeedOutput(std::string(buffer.data(), strLen));
    return nullptr;
}

static napi_value DrainPendingInput(napi_env env, napi_callback_info info) {
    size_t argc = 0;
    TerminalHost* host = GetHostFromCallback(env, info, &argc, nullptr);
    napi_value result;
    const std::string content = host ? host->DrainPendingInput() : std::string();
    napi_create_string_utf8(env, content.c_str(), content.length(), &result);
    return result;
}

static napi_value SetInputCallback(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    TerminalHost* host = GetHostFromCallback(env, info, &argc, args);
    if (!host) {
        return nullptr;
    }

    if (argc < 1) {
        host->ClearInputCallback();
        return nullptr;
    }

    napi_valuetype valueType = napi_undefined;
    napi_typeof(env, args[0], &valueType);
    if (valueType == napi_function) {
        host->SetInputCallback(env, args[0]);
    } else {
        host->ClearInputCallback();
    }
    return nullptr;
}

static napi_value DrainPendingLinkActivation(napi_env env, napi_callback_info info) {
    size_t argc = 0;
    TerminalHost* host = GetHostFromCallback(env, info, &argc, nullptr);
    napi_value result;
    const std::string content = host ? host->DrainPendingLinkActivation() : std::string();
    napi_create_string_utf8(env, content.c_str(), content.length(), &result);
    return result;
}

static napi_value DrainPendingContextMenuRequest(napi_env env, napi_callback_info info) {
    size_t argc = 0;
    TerminalHost* host = GetHostFromCallback(env, info, &argc, nullptr);
    napi_value result;
    const std::string content = host ? host->DrainPendingContextMenuRequest() : std::string();
    napi_create_string_utf8(env, content.c_str(), content.length(), &result);
    return result;
}

static napi_value GetScreenContent(napi_env env, napi_callback_info info) {
    size_t argc = 0;
    TerminalHost* host = GetHostFromCallback(env, info, &argc, nullptr);
    napi_value result;
    const std::string content = host ? host->GetScreenContent() : std::string();
    napi_create_string_utf8(env, content.c_str(), content.length(), &result);
    return result;
}

static napi_value GetCursorPosition(napi_env env, napi_callback_info info) {
    size_t argc = 0;
    TerminalHost* host = GetHostFromCallback(env, info, &argc, nullptr);
    napi_value result;
    napi_create_object(env, &result);

    int row = 0;
    int col = 0;
    if (host) {
        host->GetCursorPosition(row, col);
    }

    napi_value rowVal;
    napi_value colVal;
    napi_create_int32(env, row, &rowVal);
    napi_create_int32(env, col, &colVal);
    napi_set_named_property(env, result, "row", rowVal);
    napi_set_named_property(env, result, "col", colVal);
    return result;
}

static napi_value GetTerminalSize(napi_env env, napi_callback_info info) {
    size_t argc = 0;
    TerminalHost* host = GetHostFromCallback(env, info, &argc, nullptr);
    napi_value result;
    napi_create_object(env, &result);

    int cols = 0;
    int rows = 0;
    if (host) {
        host->GetTerminalSize(cols, rows);
    }

    napi_value colsVal;
    napi_value rowsVal;
    napi_create_int32(env, cols, &colsVal);
    napi_create_int32(env, rows, &rowsVal);
    napi_set_named_property(env, result, "cols", colsVal);
    napi_set_named_property(env, result, "rows", rowsVal);
    return result;
}

static napi_value GetCellMetrics(napi_env env, napi_callback_info info) {
    size_t argc = 0;
    TerminalHost* host = GetHostFromCallback(env, info, &argc, nullptr);
    napi_value result;
    napi_create_object(env, &result);

    napi_value widthVal;
    napi_value heightVal;
    napi_create_double(env, host ? host->GetCellWidth() : 0.0, &widthVal);
    napi_create_double(env, host ? host->GetCellHeight() : 0.0, &heightVal);
    napi_set_named_property(env, result, "width", widthVal);
    napi_set_named_property(env, result, "height", heightVal);
    return result;
}

static napi_value SetResourceManager(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    TerminalHost* host = GetHostFromCallback(env, info, &argc, args);
    if (host && argc >= 1) {
        host->SetResourceManager(env, args[0]);
    }
    return nullptr;
}

static napi_value SetFilesDir(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    TerminalHost* host = GetHostFromCallback(env, info, &argc, args);
    if (!host || argc < 1) {
        return nullptr;
    }

    size_t strLen = 0;
    napi_get_value_string_utf8(env, args[0], nullptr, 0, &strLen);
    std::vector<char> filesDir(strLen + 1, '\0');
    napi_get_value_string_utf8(env, args[0], filesDir.data(), filesDir.size(), &strLen);
    host->SetFilesDir(std::string(filesDir.data(), strLen));
    return nullptr;
}

static napi_value ScrollView(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    TerminalHost* host = GetHostFromCallback(env, info, &argc, args);
    if (host && argc >= 1) {
        int32_t delta = 0;
        napi_get_value_int32(env, args[0], &delta);
        host->ScrollView(delta);
    }
    return nullptr;
}

static napi_value ResetScroll(napi_env env, napi_callback_info info) {
    size_t argc = 0;
    TerminalHost* host = GetHostFromCallback(env, info, &argc, nullptr);
    if (host) {
        host->ResetScroll();
    }
    return nullptr;
}

static napi_value GetScrollbackSize(napi_env env, napi_callback_info info) {
    size_t argc = 0;
    TerminalHost* host = GetHostFromCallback(env, info, &argc, nullptr);
    napi_value result;
    napi_create_int32(env, host ? host->GetScrollbackSize() : 0, &result);
    return result;
}

static napi_value StartSelection(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    TerminalHost* host = GetHostFromCallback(env, info, &argc, args);
    if (host && argc >= 2) {
        int32_t row = 0;
        int32_t col = 0;
        napi_get_value_int32(env, args[0], &row);
        napi_get_value_int32(env, args[1], &col);
        host->StartSelection(row, col);
    }
    return nullptr;
}

static napi_value UpdateSelection(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    TerminalHost* host = GetHostFromCallback(env, info, &argc, args);
    if (host && argc >= 2) {
        int32_t row = 0;
        int32_t col = 0;
        napi_get_value_int32(env, args[0], &row);
        napi_get_value_int32(env, args[1], &col);
        host->UpdateSelection(row, col);
    }
    return nullptr;
}

static napi_value ClearSelection(napi_env env, napi_callback_info info) {
    size_t argc = 0;
    TerminalHost* host = GetHostFromCallback(env, info, &argc, nullptr);
    if (host) {
        host->ClearSelection();
    }
    return nullptr;
}

static napi_value GetSelectedText(napi_env env, napi_callback_info info) {
    size_t argc = 0;
    TerminalHost* host = GetHostFromCallback(env, info, &argc, nullptr);
    napi_value result;
    const std::string text = host ? host->GetSelectedText() : std::string();
    napi_create_string_utf8(env, text.c_str(), text.length(), &result);
    return result;
}

static napi_value StartSearch(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    TerminalHost* host = GetHostFromCallback(env, info, &argc, args);
    if (host) {
        std::string query;
        if (argc >= 1) {
            size_t strLen = 0;
            napi_get_value_string_utf8(env, args[0], nullptr, 0, &strLen);
            std::vector<char> buffer(strLen + 1, '\0');
            napi_get_value_string_utf8(env, args[0], buffer.data(), buffer.size(), &strLen);
            query.assign(buffer.data(), strLen);
        }
        host->StartSearch(query);
    }
    return nullptr;
}

static napi_value SearchSelection(napi_env env, napi_callback_info info) {
    size_t argc = 0;
    TerminalHost* host = GetHostFromCallback(env, info, &argc, nullptr);
    if (host) {
        host->SearchSelection();
    }
    return nullptr;
}

static napi_value UpdateSearch(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    TerminalHost* host = GetHostFromCallback(env, info, &argc, args);
    if (host && argc >= 1) {
        size_t strLen = 0;
        napi_get_value_string_utf8(env, args[0], nullptr, 0, &strLen);
        std::vector<char> buffer(strLen + 1, '\0');
        napi_get_value_string_utf8(env, args[0], buffer.data(), buffer.size(), &strLen);
        host->UpdateSearch(std::string(buffer.data(), strLen));
    }
    return nullptr;
}

static napi_value NavigateSearch(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    TerminalHost* host = GetHostFromCallback(env, info, &argc, args);
    if (host && argc >= 1) {
        bool next = true;
        napi_get_value_bool(env, args[0], &next);
        host->NavigateSearch(next);
    }
    return nullptr;
}

static napi_value EndSearch(napi_env env, napi_callback_info info) {
    size_t argc = 0;
    TerminalHost* host = GetHostFromCallback(env, info, &argc, nullptr);
    if (host) {
        host->EndSearch();
    }
    return nullptr;
}

static napi_value GetSearchStatus(napi_env env, napi_callback_info info) {
    size_t argc = 0;
    TerminalHost* host = GetHostFromCallback(env, info, &argc, nullptr);
    const TerminalSearchStatus status = host ? host->GetSearchStatus() : TerminalSearchStatus {};

    napi_value result;
    napi_create_object(env, &result);

    napi_value activeValue;
    napi_get_boolean(env, status.active, &activeValue);
    napi_set_named_property(env, result, "active", activeValue);

    napi_value totalValue;
    napi_create_uint32(env, static_cast<uint32_t>(status.total), &totalValue);
    napi_set_named_property(env, result, "total", totalValue);

    napi_value selectedValue;
    napi_create_int32(env, status.selected, &selectedValue);
    napi_set_named_property(env, result, "selected", selectedValue);

    napi_value queryValue;
    napi_create_string_utf8(env, status.query.c_str(), status.query.length(), &queryValue);
    napi_set_named_property(env, result, "query", queryValue);

    return result;
}

static napi_value LoadTheme(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    TerminalHost* host = GetHostFromCallback(env, info, &argc, args);
    napi_value result;
    bool loaded = false;

    if (host && argc >= 1) {
        size_t strLen = 0;
        napi_get_value_string_utf8(env, args[0], nullptr, 0, &strLen);
        std::vector<char> themeName(strLen + 1, '\0');
        napi_get_value_string_utf8(env, args[0], themeName.data(), themeName.size(), &strLen);
        loaded = host->LoadTheme(std::string(themeName.data(), strLen));
    }

    napi_get_boolean(env, loaded, &result);
    return result;
}

static napi_value GetThemeList(napi_env env, napi_callback_info info) {
    size_t argc = 0;
    TerminalHost* host = GetHostFromCallback(env, info, &argc, nullptr);
    const std::vector<std::string> themes = host ? host->GetThemeList() : std::vector<std::string>();

    napi_value result;
    napi_create_array_with_length(env, themes.size(), &result);
    for (size_t i = 0; i < themes.size(); ++i) {
        napi_value themeName;
        napi_create_string_utf8(env, themes[i].c_str(), themes[i].length(), &themeName);
        napi_set_element(env, result, i, themeName);
    }
    return result;
}

static napi_value SetDensity(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    TerminalHost* host = GetHostFromCallback(env, info, &argc, args);
    if (host && argc >= 1) {
        double density = 1.0;
        napi_get_value_double(env, args[0], &density);
        host->SetDensity(density);
    }
    return nullptr;
}

static napi_value SetWindowInfo(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3];
    TerminalHost* host = GetHostFromCallback(env, info, &argc, args);
    if (host && argc >= 3) {
        int32_t windowId = -1;
        double left = 0.0;
        double top = 0.0;
        napi_get_value_int32(env, args[0], &windowId);
        napi_get_value_double(env, args[1], &left);
        napi_get_value_double(env, args[2], &top);
        host->SetWindowInfo(windowId, left, top);
    }
    return nullptr;
}

static napi_value SetSurfaceOrigin(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    TerminalHost* host = GetHostFromCallback(env, info, &argc, args);
    if (host && argc >= 2) {
        double left = 0.0;
        double top = 0.0;
        napi_get_value_double(env, args[0], &left);
        napi_get_value_double(env, args[1], &top);
        host->SetSurfaceOrigin(left, top);
    }
    return nullptr;
}

static napi_value SetSurfaceFrame(napi_env env, napi_callback_info info) {
    size_t argc = 4;
    napi_value args[4];
    TerminalHost* host = GetHostFromCallback(env, info, &argc, args);
    if (host && argc >= 4) {
        double left = 0.0;
        double top = 0.0;
        double width = 0.0;
        double height = 0.0;
        napi_get_value_double(env, args[0], &left);
        napi_get_value_double(env, args[1], &top);
        napi_get_value_double(env, args[2], &width);
        napi_get_value_double(env, args[3], &height);
        host->SetSurfaceFrame(left, top, width, height);
    }
    return nullptr;
}

static napi_value SetConfig(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    TerminalHost* host = GetHostFromCallback(env, info, &argc, args);
    if (!host || argc < 1) {
        return nullptr;
    }

    napi_value fontSizeVal;
    napi_value scrollbackVal;
    napi_value bgColorVal;
    napi_value fgColorVal;
    napi_value cursorStyleVal;
    napi_value cursorBlinkVal;
    napi_get_named_property(env, args[0], "fontSize", &fontSizeVal);
    napi_get_named_property(env, args[0], "scrollbackLines", &scrollbackVal);
    napi_get_named_property(env, args[0], "bgColor", &bgColorVal);
    napi_get_named_property(env, args[0], "fgColor", &fgColorVal);
    napi_get_named_property(env, args[0], "cursorStyle", &cursorStyleVal);
    napi_get_named_property(env, args[0], "cursorBlink", &cursorBlinkVal);

    int32_t fontSize = 14;
    int32_t scrollbackLines = 10000;
    uint32_t bgColor = 0xFF000000;
    uint32_t fgColor = 0xFFFFFFFF;
    int32_t cursorStyle = 0;
    bool cursorBlink = true;

    napi_get_value_int32(env, fontSizeVal, &fontSize);
    napi_get_value_int32(env, scrollbackVal, &scrollbackLines);
    napi_get_value_uint32(env, bgColorVal, &bgColor);
    napi_get_value_uint32(env, fgColorVal, &fgColor);
    napi_get_value_int32(env, cursorStyleVal, &cursorStyle);
    napi_get_value_bool(env, cursorBlinkVal, &cursorBlink);
    host->SetConfig(fontSize, scrollbackLines, bgColor, fgColor, cursorStyle, cursorBlink);
    return nullptr;
}

static napi_value IsRendererReady(napi_env env, napi_callback_info info) {
    size_t argc = 0;
    TerminalHost* host = GetHostFromCallback(env, info, &argc, nullptr);
    napi_value result;
    napi_get_boolean(env, host ? host->IsRendererReady() : false, &result);
    return result;
}

static napi_value GetRendererError(napi_env env, napi_callback_info info) {
    size_t argc = 0;
    TerminalHost* host = GetHostFromCallback(env, info, &argc, nullptr);
    napi_value result;
    const std::string error = host ? host->GetRendererError() : std::string();
    napi_create_string_utf8(env, error.c_str(), error.length(), &result);
    return result;
}

static napi_value Init(napi_env env, napi_value exports) {
    napi_value exportInstance = nullptr;
    OH_NativeXComponent* nativeXComponent = nullptr;
    napi_get_named_property(env, exports, OH_NATIVE_XCOMPONENT_OBJ, &exportInstance);
    if (exportInstance != nullptr) {
        napi_valuetype valueType = napi_undefined;
        napi_typeof(env, exportInstance, &valueType);
        if (valueType == napi_object) {
            napi_unwrap(env, exportInstance, reinterpret_cast<void**>(&nativeXComponent));
        }
    }

    TerminalHost* host = nativeXComponent ? EnsureHost(nativeXComponent) : nullptr;

    if (nativeXComponent) {
        static OH_NativeXComponent_Callback callback;
        callback.OnSurfaceCreated = OnSurfaceCreatedCB;
        callback.OnSurfaceChanged = OnSurfaceChangedCB;
        callback.OnSurfaceDestroyed = OnSurfaceDestroyedCB;
        callback.DispatchTouchEvent = DispatchTouchEventCB;

        OH_NativeXComponent_RegisterCallback(nativeXComponent, &callback);
        OH_NativeXComponent_RegisterSurfaceShowCallback(nativeXComponent, OnSurfaceShowCB);
        OH_NativeXComponent_RegisterSurfaceHideCallback(nativeXComponent, OnSurfaceHideCB);

        static OH_NativeXComponent_MouseEvent_Callback mouseCallback;
        mouseCallback.DispatchMouseEvent = DispatchMouseEventCB;
        mouseCallback.DispatchHoverEvent = nullptr;
        OH_NativeXComponent_RegisterMouseEventCallback(nativeXComponent, &mouseCallback);
        OH_NativeXComponent_RegisterUIInputEventCallback(
            nativeXComponent,
            DispatchAxisEventCB,
            ARKUI_UIINPUTEVENT_TYPE_AXIS);
        OH_NativeXComponent_RegisterKeyEventCallbackWithResult(nativeXComponent, DispatchKeyEventCB);
    }

    napi_property_descriptor desc[] = {
        {"writeInput", nullptr, WriteInput, nullptr, nullptr, nullptr, napi_default, host},
        {"feedOutput", nullptr, FeedOutput, nullptr, nullptr, nullptr, napi_default, host},
        {"drainPendingInput", nullptr, DrainPendingInput, nullptr, nullptr, nullptr, napi_default, host},
        {"setInputCallback", nullptr, SetInputCallback, nullptr, nullptr, nullptr, napi_default, host},
        {"drainPendingLinkActivation", nullptr, DrainPendingLinkActivation, nullptr, nullptr, nullptr, napi_default, host},
        {"drainPendingContextMenuRequest", nullptr, DrainPendingContextMenuRequest, nullptr, nullptr, nullptr, napi_default, host},
        {"resizeTerminal", nullptr, ResizeTerminal, nullptr, nullptr, nullptr, napi_default, host},
        {"getScreenContent", nullptr, GetScreenContent, nullptr, nullptr, nullptr, napi_default, host},
        {"getCursorPosition", nullptr, GetCursorPosition, nullptr, nullptr, nullptr, napi_default, host},
        {"getTerminalSize", nullptr, GetTerminalSize, nullptr, nullptr, nullptr, napi_default, host},
        {"getCellMetrics", nullptr, GetCellMetrics, nullptr, nullptr, nullptr, napi_default, host},
        {"setFilesDir", nullptr, SetFilesDir, nullptr, nullptr, nullptr, napi_default, host},
        {"setResourceManager", nullptr, SetResourceManager, nullptr, nullptr, nullptr, napi_default, host},
        {"scrollView", nullptr, ScrollView, nullptr, nullptr, nullptr, napi_default, host},
        {"resetScroll", nullptr, ResetScroll, nullptr, nullptr, nullptr, napi_default, host},
        {"getScrollbackSize", nullptr, GetScrollbackSize, nullptr, nullptr, nullptr, napi_default, host},
        {"startSelection", nullptr, StartSelection, nullptr, nullptr, nullptr, napi_default, host},
        {"updateSelection", nullptr, UpdateSelection, nullptr, nullptr, nullptr, napi_default, host},
        {"clearSelection", nullptr, ClearSelection, nullptr, nullptr, nullptr, napi_default, host},
        {"getSelectedText", nullptr, GetSelectedText, nullptr, nullptr, nullptr, napi_default, host},
        {"startSearch", nullptr, StartSearch, nullptr, nullptr, nullptr, napi_default, host},
        {"searchSelection", nullptr, SearchSelection, nullptr, nullptr, nullptr, napi_default, host},
        {"updateSearch", nullptr, UpdateSearch, nullptr, nullptr, nullptr, napi_default, host},
        {"navigateSearch", nullptr, NavigateSearch, nullptr, nullptr, nullptr, napi_default, host},
        {"endSearch", nullptr, EndSearch, nullptr, nullptr, nullptr, napi_default, host},
        {"getSearchStatus", nullptr, GetSearchStatus, nullptr, nullptr, nullptr, napi_default, host},
        {"setConfig", nullptr, SetConfig, nullptr, nullptr, nullptr, napi_default, host},
        {"setDensity", nullptr, SetDensity, nullptr, nullptr, nullptr, napi_default, host},
        {"setWindowInfo", nullptr, SetWindowInfo, nullptr, nullptr, nullptr, napi_default, host},
        {"setSurfaceOrigin", nullptr, SetSurfaceOrigin, nullptr, nullptr, nullptr, napi_default, host},
        {"setSurfaceFrame", nullptr, SetSurfaceFrame, nullptr, nullptr, nullptr, napi_default, host},
        {"loadTheme", nullptr, LoadTheme, nullptr, nullptr, nullptr, napi_default, host},
        {"getThemeList", nullptr, GetThemeList, nullptr, nullptr, nullptr, napi_default, host},
        {"isRendererReady", nullptr, IsRendererReady, nullptr, nullptr, nullptr, napi_default, host},
        {"getRendererError", nullptr, GetRendererError, nullptr, nullptr, nullptr, napi_default, host},
    };
    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);
    return exports;
}

} // namespace

EXTERN_C_START
static napi_module terminalModule = {
    .nm_version = 1,
    .nm_flags = 0,
    .nm_filename = nullptr,
    .nm_register_func = Init,
    .nm_modname = "libghostty_ohos",
    .nm_priv = ((void*)0),
    .reserved = {0},
};
EXTERN_C_END

extern "C" __attribute__((constructor)) void RegisterTerminalModule(void) {
    napi_module_register(&terminalModule);
}
