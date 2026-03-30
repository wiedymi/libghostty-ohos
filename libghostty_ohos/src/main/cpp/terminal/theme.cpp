#include "theme.h"
#include <sstream>
#include <algorithm>
#include <cctype>

static std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, last - first + 1);
}

uint32_t ThemeParser::parseColor(const std::string& colorStr) {
    std::string s = trim(colorStr);
    if (s.empty()) return 0xFF000000;

    // Remove # prefix if present
    if (s[0] == '#') {
        s = s.substr(1);
    }

    // Parse hex color
    uint32_t color = 0;
    try {
        color = std::stoul(s, nullptr, 16);
    } catch (...) {
        return 0xFF000000;
    }

    // Convert RGB to ARGB (add alpha)
    if (s.length() == 6) {
        color = 0xFF000000 | color;
    }

    return color;
}

bool ThemeParser::parseThemeFile(const char* data, size_t len, TerminalTheme& theme) {
    std::string content(data, len);
    std::istringstream stream(content);
    std::string line;

    while (std::getline(stream, line)) {
        // Skip empty lines and comments
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }

        // Parse key = value
        size_t eqPos = trimmed.find('=');
        if (eqPos == std::string::npos) {
            continue;
        }

        std::string key = trim(trimmed.substr(0, eqPos));
        std::string value = trim(trimmed.substr(eqPos + 1));

        if (key == "background") {
            theme.background = parseColor(value);
        } else if (key == "foreground") {
            theme.foreground = parseColor(value);
        } else if (key == "cursor-color") {
            theme.cursorColor = parseColor(value);
        } else if (key == "cursor-text") {
            theme.cursorText = parseColor(value);
        } else if (key == "selection-background") {
            theme.selectionBackground = parseColor(value);
        } else if (key == "selection-foreground") {
            theme.selectionForeground = parseColor(value);
        } else if (key == "palette") {
            // Parse palette entry: "N=#RRGGBB"
            size_t eqPos2 = value.find('=');
            if (eqPos2 != std::string::npos) {
                try {
                    int index = std::stoi(value.substr(0, eqPos2));
                    if (index >= 0 && index < 16) {
                        theme.palette[index] = parseColor(value.substr(eqPos2 + 1));
                    }
                } catch (...) {
                    // Ignore parse errors
                }
            }
        }
    }

    return true;
}
