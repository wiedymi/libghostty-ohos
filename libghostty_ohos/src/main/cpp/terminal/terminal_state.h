#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "theme.h"

struct CellAttributes {
    uint32_t fg;
    uint32_t bg;
    bool bold;
    bool italic;
    bool underline;
    bool strikethrough;
    bool inverse;
    bool hidden;
    bool blink;

    CellAttributes()
        : fg(0xFFFFFFFF), bg(0xFF000000), bold(false), italic(false),
          underline(false), strikethrough(false), inverse(false),
          hidden(false), blink(false) {}
};

struct Cell {
    uint32_t codepoint;
    char text[64];
    uint8_t textLen;
    uint8_t width;
    CellAttributes attrs;
    bool selected;

    Cell() : codepoint(' '), text{0}, textLen(0), width(1), attrs(), selected(false) {}
};

class TerminalState {
public:
    TerminalState(int cols, int rows);

    void resize(int cols, int rows);
    void clear();

    void setCell(int row, int col, uint32_t codepoint, const CellAttributes& attrs);
    const Cell& getCell(int row, int col) const;

    void setCursor(int row, int col);
    void getCursor(int& row, int& col) const;
    void setCursorVisible(bool visible) { m_cursorVisible = visible; }
    bool isCursorVisible() const { return m_cursorVisible; }

    void setCurrentAttrs(const CellAttributes& attrs) { m_currentAttrs = attrs; }
    const CellAttributes& getCurrentAttrs() const { return m_currentAttrs; }

    void putChar(uint32_t codepoint);
    void newLine();
    void carriageReturn();
    void backspace();
    void tab();
    void lineFeed();
    void reverseIndex();

    void eraseInLine(int mode);
    void eraseInDisplay(int mode);

    void scrollUp(int lines);
    void scrollDown(int lines);

    void setScrollRegion(int top, int bottom);

    int getCols() const { return m_cols; }
    int getRows() const { return m_rows; }

    std::string getContent() const;
    const std::vector<Cell>& getBuffer() const { return m_buffer; }

    // Scrollback support
    void setScrollbackSize(int lines) { m_maxScrollback = lines; }
    int getScrollbackSize() const { return static_cast<int>(m_scrollback.size()); }
    void scrollView(int delta);
    void resetViewScroll() { m_viewOffset = 0; }
    int getViewOffset() const { return m_viewOffset; }
    const std::vector<Cell>& getScrollbackLine(int index) const;
    std::vector<Cell> getVisibleBuffer() const;

    // Selection support
    void startSelection(int row, int col);
    void updateSelection(int row, int col);
    void clearSelection();
    bool hasSelection() const { return m_selectionActive; }
    std::string getSelectedText() const;
    void getSelectionBounds(int& startRow, int& startCol, int& endRow, int& endCol) const;

    // Theme support
    void setTheme(const TerminalTheme& theme);
    const TerminalTheme& getTheme() const { return m_theme; }
    uint32_t getPaletteColor(int index) const;

private:
    int index(int row, int col) const { return row * m_cols + col; }
    void ensureCursorInBounds();
    void addLineToScrollback(const std::vector<Cell>& line);

    int m_cols;
    int m_rows;
    int m_cursorRow;
    int m_cursorCol;
    bool m_cursorVisible;

    int m_scrollTop;
    int m_scrollBottom;

    CellAttributes m_currentAttrs;
    std::vector<Cell> m_buffer;

    // Scrollback
    std::vector<std::vector<Cell>> m_scrollback;
    int m_maxScrollback = 10000;
    int m_viewOffset = 0;

    // Selection
    bool m_selectionActive = false;
    int m_selStartRow = 0;
    int m_selStartCol = 0;
    int m_selEndRow = 0;
    int m_selEndCol = 0;

    // Theme
    TerminalTheme m_theme;

    static Cell s_emptyCell;
    static std::vector<Cell> s_emptyLine;
};
