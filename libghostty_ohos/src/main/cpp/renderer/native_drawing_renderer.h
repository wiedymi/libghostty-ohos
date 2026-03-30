#pragma once

#include <native_window/external_window.h>
#include <native_buffer/native_buffer.h>
#include <native_drawing/drawing_bitmap.h>
#include <native_drawing/drawing_brush.h>
#include <native_drawing/drawing_canvas.h>
#include <native_drawing/drawing_font_collection.h>
#include <native_drawing/drawing_rect.h>
#include <native_drawing/drawing_register_font.h>
#include <native_drawing/drawing_text_typography.h>
#include <unordered_map>
#include <string>
#include <cstdint>
#include "renderer.h"

class NativeDrawingRenderer : public Renderer {
public:
    NativeDrawingRenderer();
    ~NativeDrawingRenderer() override;

    bool init(OHNativeWindow* window, uint32_t width, uint32_t height) override;
    void cleanup() override;
    void resize(uint32_t width, uint32_t height) override;

    bool loadFontAtlas(NativeResourceManager* resourceManager, const std::string& filesDir) override;

    void beginFrame() override;
    void renderGrid(const std::vector<Cell>& cells, int cols, int rows,
                    int cursorRow, int cursorCol, bool cursorVisible) override;
    void endFrame() override;

protected:
    void updateCellDimensions() override;

private:
    enum class LineStyle : uint8_t {
        None,
        Light,
        Heavy,
        Double,
    };

    struct BoxGlyphEdges {
        LineStyle up = LineStyle::None;
        LineStyle right = LineStyle::None;
        LineStyle down = LineStyle::None;
        LineStyle left = LineStyle::None;
    };

    struct GlyphKey {
        std::string text;
        uint32_t fg = 0;
        uint8_t styleBits = 0;
        uint8_t span = 1;

        bool operator==(const GlyphKey& other) const {
            return text == other.text &&
                   fg == other.fg &&
                   styleBits == other.styleBits &&
                   span == other.span;
        }
    };

    struct GlyphKeyHash {
        size_t operator()(const GlyphKey& key) const;
    };

    struct GlyphLayout {
        OH_Drawing_Typography* typography = nullptr;
        float width = 0.0f;
        float height = 0.0f;
    };

    bool configureWindow();
    bool ensureDrawingObjects();
    void destroyGlyphCache();
    void trimGlyphCache();
    GlyphLayout* getGlyphLayout(const std::string& text, const CellAttributes& attrs, uint8_t span);
    bool paintBuiltinGlyph(const Cell& cell, const CellAttributes& attrs, float left, float top, float width, float height);
    void paintCellBackground(float left, float top, float width, float height, uint32_t color);
    void paintCursor(float left, float top, float width, float height, uint32_t color);
    void paintBuiltinRect(float left, float top, float width, float height, uint32_t color);
    void paintBuiltinLineHorizontal(float left, float top, float width, float cellHeight, LineStyle style, bool upperHalf, bool lowerHalf, uint32_t color);
    void paintBuiltinLineVertical(float left, float top, float cellWidth, float height, LineStyle style, bool leftHalf, bool rightHalf, uint32_t color);
    void paintBuiltinDiagonal(float left, float top, float width, float height, bool forwardSlash, uint32_t color);
    static BoxGlyphEdges getBoxGlyphEdges(uint32_t codepoint);
    static uint32_t blendColor(uint32_t bg, uint32_t fg, uint8_t alpha);
    static uint8_t makeStyleBits(const CellAttributes& attrs);
    static std::string cellText(const Cell& cell);

    OHNativeWindow* m_window = nullptr;
    OHNativeWindowBuffer* m_currentBuffer = nullptr;
    OH_NativeBuffer* m_currentNativeBuffer = nullptr;
    void* m_currentPixels = nullptr;
    OH_NativeBuffer_Config m_currentConfig {};
    int m_currentFenceFd = -1;
    uint64_t m_currentFrameId = 0;

    OH_Drawing_Canvas* m_canvas = nullptr;
    OH_Drawing_Brush* m_brush = nullptr;
    OH_Drawing_Rect* m_rect = nullptr;
    OH_Drawing_FontCollection* m_fontCollection = nullptr;

    std::unordered_map<GlyphKey, GlyphLayout, GlyphKeyHash> m_glyphCache;
    std::string m_primaryFontFamily = "libghostty Mono";
    std::string m_symbolFontFamily = "libghostty Nerd Symbols";
    bool m_fontsConfigured = false;
};
