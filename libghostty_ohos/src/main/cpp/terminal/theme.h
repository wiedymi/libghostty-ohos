#pragma once

#include <string>
#include <array>
#include <cstdint>

struct TerminalTheme {
    std::string name;
    std::array<uint32_t, 16> palette;
    uint32_t background;
    uint32_t foreground;
    uint32_t cursorColor;
    uint32_t cursorText;
    uint32_t selectionBackground;
    uint32_t selectionForeground;

    TerminalTheme() {
        // Default theme (similar to xterm)
        palette = {
            0xFF000000, 0xFFCC0000, 0xFF00CC00, 0xFFCCCC00,
            0xFF0000CC, 0xFFCC00CC, 0xFF00CCCC, 0xFFCCCCCC,
            0xFF555555, 0xFFFF5555, 0xFF55FF55, 0xFFFFFF55,
            0xFF5555FF, 0xFFFF55FF, 0xFF55FFFF, 0xFFFFFFFF
        };
        background = 0xFF000000;
        foreground = 0xFFFFFFFF;
        cursorColor = 0xFFFFFFFF;
        cursorText = 0xFF000000;
        selectionBackground = 0xFF444444;
        selectionForeground = 0xFFFFFFFF;
    }
};

class ThemeParser {
public:
    static bool parseThemeFile(const char* data, size_t len, TerminalTheme& theme);
    static uint32_t parseColor(const std::string& colorStr);
};
