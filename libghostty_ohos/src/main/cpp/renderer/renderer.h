#pragma once

#include <chrono>
#include <cstdint>
#include <vector>
#include "../terminal/terminal_state.h"

struct NativeResourceManager;
struct NativeWindow;
typedef struct NativeWindow OHNativeWindow;

class Renderer {
public:
    virtual ~Renderer() = default;

    virtual bool init(OHNativeWindow* window, uint32_t width, uint32_t height) = 0;
    virtual void cleanup() = 0;
    virtual void resize(uint32_t width, uint32_t height) = 0;
    virtual bool loadFontAtlas(NativeResourceManager* resourceManager, const std::string& filesDir) = 0;
    virtual void beginFrame() = 0;
    virtual void renderGrid(const std::vector<Cell>& cells, int cols, int rows,
                            int cursorRow, int cursorCol, bool cursorVisible) = 0;
    virtual void endFrame() = 0;

    float getCellWidth() const { return m_cellWidth; }
    float getCellHeight() const { return m_cellHeight; }
    uint32_t getWidth() const { return m_width; }
    uint32_t getHeight() const { return m_height; }
    void getLastCursorRect(float& left, float& top, float& width, float& height, bool& valid) const {
        left = m_lastCursorLeft;
        top = m_lastCursorTop;
        width = m_lastCursorWidth;
        height = m_lastCursorHeight;
        valid = m_lastCursorRectValid;
    }

    void setFontSize(float size) {
        m_fontSize = size;
        updateCellDimensions();
    }

    void setDensity(float density) {
        m_density = density > 0 ? density : 1.0f;
        updateCellDimensions();
    }

    void setColors(uint32_t bgColor, uint32_t fgColor) {
        m_defaultBgColor = bgColor;
        m_defaultFgColor = fgColor;
    }

    void setCursorColors(uint32_t cursorColor, uint32_t cursorTextColor) {
        m_cursorBgColor = cursorColor;
        m_cursorFgColor = cursorTextColor;
    }

    void setCursorStyle(int style, bool blink) {
        m_cursorStyle = style;
        m_cursorBlink = blink;
    }

    bool cursorBlinkEnabled() const { return m_cursorBlink; }

    bool shouldRenderCursor(bool cursorVisible) const {
        if (!cursorVisible) {
            return false;
        }
        if (!m_cursorBlink) {
            return true;
        }

        using namespace std::chrono;
        const auto now = steady_clock::now().time_since_epoch();
        const auto phase = duration_cast<milliseconds>(now).count() % 1000;
        return phase < 500;
    }

protected:
    virtual void updateCellDimensions() {
        m_cellWidth = m_fontSize * 0.6f * m_density;
        m_cellHeight = m_fontSize * 1.2f * m_density;
    }

    uint32_t m_width = 0;
    uint32_t m_height = 0;
    float m_cellWidth = 10.0f;
    float m_cellHeight = 20.0f;
    float m_fontSize = 14.0f;
    float m_density = 1.0f;
    uint32_t m_defaultBgColor = 0xFF000000;
    uint32_t m_defaultFgColor = 0xFFFFFFFF;
    uint32_t m_cursorBgColor = 0xFFFFFFFF;
    uint32_t m_cursorFgColor = 0xFF000000;
    int m_cursorStyle = 0;
    bool m_cursorBlink = true;
    float m_lastCursorLeft = 0.0f;
    float m_lastCursorTop = 0.0f;
    float m_lastCursorWidth = 0.0f;
    float m_lastCursorHeight = 0.0f;
    bool m_lastCursorRectValid = false;
};
