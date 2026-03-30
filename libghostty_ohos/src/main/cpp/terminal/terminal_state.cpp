#include "terminal_state.h"
#include <algorithm>
#include <cstring>

Cell TerminalState::s_emptyCell;
std::vector<Cell> TerminalState::s_emptyLine;

TerminalState::TerminalState(int cols, int rows)
    : m_cols(cols), m_rows(rows), m_cursorRow(0), m_cursorCol(0),
      m_cursorVisible(true), m_scrollTop(0), m_scrollBottom(rows - 1) {
    m_buffer.resize(cols * rows);
}

void TerminalState::resize(int cols, int rows) {
    std::vector<Cell> newBuffer(cols * rows);

    int copyRows = std::min(rows, m_rows);
    int copyCols = std::min(cols, m_cols);

    for (int r = 0; r < copyRows; r++) {
        for (int c = 0; c < copyCols; c++) {
            newBuffer[r * cols + c] = m_buffer[r * m_cols + c];
        }
    }

    m_buffer = std::move(newBuffer);
    m_cols = cols;
    m_rows = rows;
    m_scrollTop = 0;
    m_scrollBottom = rows - 1;

    ensureCursorInBounds();
}

void TerminalState::clear() {
    std::fill(m_buffer.begin(), m_buffer.end(), Cell());
    m_cursorRow = 0;
    m_cursorCol = 0;
}

void TerminalState::setCell(int row, int col, uint32_t codepoint, const CellAttributes& attrs) {
    if (row >= 0 && row < m_rows && col >= 0 && col < m_cols) {
        Cell& cell = m_buffer[index(row, col)];
        cell.codepoint = codepoint;
        cell.attrs = attrs;
    }
}

const Cell& TerminalState::getCell(int row, int col) const {
    if (row >= 0 && row < m_rows && col >= 0 && col < m_cols) {
        return m_buffer[index(row, col)];
    }
    return s_emptyCell;
}

void TerminalState::setCursor(int row, int col) {
    m_cursorRow = row;
    m_cursorCol = col;
    ensureCursorInBounds();
}

void TerminalState::getCursor(int& row, int& col) const {
    row = m_cursorRow;
    col = m_cursorCol;
}

void TerminalState::putChar(uint32_t codepoint) {
    if (m_cursorCol >= m_cols) {
        m_cursorCol = 0;
        m_cursorRow++;
        if (m_cursorRow > m_scrollBottom) {
            scrollUp(1);
            m_cursorRow = m_scrollBottom;
        }
    }

    setCell(m_cursorRow, m_cursorCol, codepoint, m_currentAttrs);
    m_cursorCol++;
}

void TerminalState::newLine() {
    m_cursorCol = 0;
    m_cursorRow++;
    if (m_cursorRow > m_scrollBottom) {
        scrollUp(1);
        m_cursorRow = m_scrollBottom;
    }
}

void TerminalState::carriageReturn() {
    m_cursorCol = 0;
}

void TerminalState::backspace() {
    if (m_cursorCol > 0) {
        m_cursorCol--;
    }
}

void TerminalState::tab() {
    int nextTab = ((m_cursorCol / 8) + 1) * 8;
    m_cursorCol = std::min(nextTab, m_cols - 1);
}

void TerminalState::lineFeed() {
    m_cursorRow++;
    if (m_cursorRow > m_scrollBottom) {
        scrollUp(1);
        m_cursorRow = m_scrollBottom;
    }
}

void TerminalState::reverseIndex() {
    if (m_cursorRow > m_scrollTop) {
        m_cursorRow--;
    } else {
        scrollDown(1);
    }
}

void TerminalState::eraseInLine(int mode) {
    switch (mode) {
        case 0:  // Erase to end of line
            for (int c = m_cursorCol; c < m_cols; c++) {
                m_buffer[index(m_cursorRow, c)] = Cell();
            }
            break;
        case 1:  // Erase to beginning of line
            for (int c = 0; c <= m_cursorCol; c++) {
                m_buffer[index(m_cursorRow, c)] = Cell();
            }
            break;
        case 2:  // Erase entire line
            for (int c = 0; c < m_cols; c++) {
                m_buffer[index(m_cursorRow, c)] = Cell();
            }
            break;
    }
}

void TerminalState::eraseInDisplay(int mode) {
    switch (mode) {
        case 0:  // Erase from cursor to end of display
            eraseInLine(0);
            for (int r = m_cursorRow + 1; r < m_rows; r++) {
                for (int c = 0; c < m_cols; c++) {
                    m_buffer[index(r, c)] = Cell();
                }
            }
            break;
        case 1:  // Erase from start to cursor
            for (int r = 0; r < m_cursorRow; r++) {
                for (int c = 0; c < m_cols; c++) {
                    m_buffer[index(r, c)] = Cell();
                }
            }
            eraseInLine(1);
            break;
        case 2:  // Erase entire display
        case 3:
            for (auto& cell : m_buffer) {
                cell = Cell();
            }
            break;
    }
}

void TerminalState::scrollUp(int lines) {
    if (lines <= 0) return;

    int scrollHeight = m_scrollBottom - m_scrollTop + 1;

    // Save lines going to scrollback (only if scrolling from top)
    if (m_scrollTop == 0) {
        int linesToSave = std::min(lines, scrollHeight);
        for (int i = 0; i < linesToSave; i++) {
            std::vector<Cell> line(m_cols);
            for (int c = 0; c < m_cols; c++) {
                line[c] = m_buffer[index(i, c)];
            }
            addLineToScrollback(line);
        }
    }

    if (lines >= scrollHeight) {
        for (int r = m_scrollTop; r <= m_scrollBottom; r++) {
            for (int c = 0; c < m_cols; c++) {
                m_buffer[index(r, c)] = Cell();
            }
        }
        return;
    }

    for (int r = m_scrollTop; r <= m_scrollBottom - lines; r++) {
        for (int c = 0; c < m_cols; c++) {
            m_buffer[index(r, c)] = m_buffer[index(r + lines, c)];
        }
    }

    for (int r = m_scrollBottom - lines + 1; r <= m_scrollBottom; r++) {
        for (int c = 0; c < m_cols; c++) {
            m_buffer[index(r, c)] = Cell();
        }
    }
}

void TerminalState::addLineToScrollback(const std::vector<Cell>& line) {
    m_scrollback.push_back(line);
    while (static_cast<int>(m_scrollback.size()) > m_maxScrollback) {
        m_scrollback.erase(m_scrollback.begin());
    }
}

void TerminalState::scrollDown(int lines) {
    if (lines <= 0) return;

    int scrollHeight = m_scrollBottom - m_scrollTop + 1;
    if (lines >= scrollHeight) {
        for (int r = m_scrollTop; r <= m_scrollBottom; r++) {
            for (int c = 0; c < m_cols; c++) {
                m_buffer[index(r, c)] = Cell();
            }
        }
        return;
    }

    for (int r = m_scrollBottom; r >= m_scrollTop + lines; r--) {
        for (int c = 0; c < m_cols; c++) {
            m_buffer[index(r, c)] = m_buffer[index(r - lines, c)];
        }
    }

    for (int r = m_scrollTop; r < m_scrollTop + lines; r++) {
        for (int c = 0; c < m_cols; c++) {
            m_buffer[index(r, c)] = Cell();
        }
    }
}

void TerminalState::setScrollRegion(int top, int bottom) {
    if (top >= 0 && bottom < m_rows && top < bottom) {
        m_scrollTop = top;
        m_scrollBottom = bottom;
    }
}

void TerminalState::ensureCursorInBounds() {
    m_cursorRow = std::max(0, std::min(m_cursorRow, m_rows - 1));
    m_cursorCol = std::max(0, std::min(m_cursorCol, m_cols - 1));
}

std::string TerminalState::getContent() const {
    std::string result;
    result.reserve(m_cols * m_rows + m_rows);

    for (int r = 0; r < m_rows; r++) {
        for (int c = 0; c < m_cols; c++) {
            const Cell& cell = m_buffer[index(r, c)];
            if (cell.codepoint < 128) {
                result += static_cast<char>(cell.codepoint);
            } else {
                // UTF-8 encode
                if (cell.codepoint < 0x80) {
                    result += static_cast<char>(cell.codepoint);
                } else if (cell.codepoint < 0x800) {
                    result += static_cast<char>(0xC0 | (cell.codepoint >> 6));
                    result += static_cast<char>(0x80 | (cell.codepoint & 0x3F));
                } else if (cell.codepoint < 0x10000) {
                    result += static_cast<char>(0xE0 | (cell.codepoint >> 12));
                    result += static_cast<char>(0x80 | ((cell.codepoint >> 6) & 0x3F));
                    result += static_cast<char>(0x80 | (cell.codepoint & 0x3F));
                } else {
                    result += static_cast<char>(0xF0 | (cell.codepoint >> 18));
                    result += static_cast<char>(0x80 | ((cell.codepoint >> 12) & 0x3F));
                    result += static_cast<char>(0x80 | ((cell.codepoint >> 6) & 0x3F));
                    result += static_cast<char>(0x80 | (cell.codepoint & 0x3F));
                }
            }
        }
        result += '\n';
    }

    return result;
}

void TerminalState::scrollView(int delta) {
    int maxOffset = static_cast<int>(m_scrollback.size());
    m_viewOffset = std::max(0, std::min(m_viewOffset + delta, maxOffset));
}

const std::vector<Cell>& TerminalState::getScrollbackLine(int index) const {
    if (index >= 0 && index < static_cast<int>(m_scrollback.size())) {
        return m_scrollback[index];
    }
    return s_emptyLine;
}

std::vector<Cell> TerminalState::getVisibleBuffer() const {
    std::vector<Cell> visible(m_cols * m_rows);

    if (m_viewOffset == 0) {
        visible = m_buffer;
    } else {
        int scrollbackStart = static_cast<int>(m_scrollback.size()) - m_viewOffset;
        int scrollbackLines = std::min(m_viewOffset, m_rows);

        for (int r = 0; r < m_rows; r++) {
            int scrollbackIdx = scrollbackStart + r;
            if (scrollbackIdx >= 0 && scrollbackIdx < static_cast<int>(m_scrollback.size())) {
                const auto& line = m_scrollback[scrollbackIdx];
                for (int c = 0; c < m_cols; c++) {
                    if (c < static_cast<int>(line.size())) {
                        visible[r * m_cols + c] = line[c];
                    } else {
                        visible[r * m_cols + c] = Cell();
                    }
                }
            } else {
                int bufferRow = r - scrollbackLines + (m_rows - scrollbackLines);
                if (bufferRow >= 0 && bufferRow < m_rows) {
                    for (int c = 0; c < m_cols; c++) {
                        visible[r * m_cols + c] = m_buffer[bufferRow * m_cols + c];
                    }
                }
            }
        }
    }

    // Mark selected cells
    if (m_selectionActive) {
        int startRow, startCol, endRow, endCol;
        getSelectionBounds(startRow, startCol, endRow, endCol);

        for (int r = startRow; r <= endRow && r < m_rows; r++) {
            if (r < 0) continue;
            int colStart = (r == startRow) ? startCol : 0;
            int colEnd = (r == endRow) ? endCol : m_cols - 1;

            for (int c = colStart; c <= colEnd && c < m_cols; c++) {
                if (c >= 0) {
                    visible[r * m_cols + c].selected = true;
                }
            }
        }
    }

    return visible;
}

void TerminalState::startSelection(int row, int col) {
    m_selectionActive = true;
    m_selStartRow = row;
    m_selStartCol = col;
    m_selEndRow = row;
    m_selEndCol = col;
}

void TerminalState::updateSelection(int row, int col) {
    if (m_selectionActive) {
        m_selEndRow = row;
        m_selEndCol = col;
    }
}

void TerminalState::clearSelection() {
    m_selectionActive = false;
}

void TerminalState::getSelectionBounds(int& startRow, int& startCol, int& endRow, int& endCol) const {
    if (m_selStartRow < m_selEndRow || (m_selStartRow == m_selEndRow && m_selStartCol <= m_selEndCol)) {
        startRow = m_selStartRow;
        startCol = m_selStartCol;
        endRow = m_selEndRow;
        endCol = m_selEndCol;
    } else {
        startRow = m_selEndRow;
        startCol = m_selEndCol;
        endRow = m_selStartRow;
        endCol = m_selStartCol;
    }
}

std::string TerminalState::getSelectedText() const {
    if (!m_selectionActive) {
        return "";
    }

    int startRow, startCol, endRow, endCol;
    getSelectionBounds(startRow, startCol, endRow, endCol);

    std::string result;
    for (int r = startRow; r <= endRow; r++) {
        int colStart = (r == startRow) ? startCol : 0;
        int colEnd = (r == endRow) ? endCol : m_cols - 1;

        for (int c = colStart; c <= colEnd; c++) {
            const Cell& cell = getCell(r, c);
            if (cell.codepoint < 128) {
                result += static_cast<char>(cell.codepoint);
            } else if (cell.codepoint < 0x800) {
                result += static_cast<char>(0xC0 | (cell.codepoint >> 6));
                result += static_cast<char>(0x80 | (cell.codepoint & 0x3F));
            } else if (cell.codepoint < 0x10000) {
                result += static_cast<char>(0xE0 | (cell.codepoint >> 12));
                result += static_cast<char>(0x80 | ((cell.codepoint >> 6) & 0x3F));
                result += static_cast<char>(0x80 | (cell.codepoint & 0x3F));
            } else {
                result += static_cast<char>(0xF0 | (cell.codepoint >> 18));
                result += static_cast<char>(0x80 | ((cell.codepoint >> 12) & 0x3F));
                result += static_cast<char>(0x80 | ((cell.codepoint >> 6) & 0x3F));
                result += static_cast<char>(0x80 | (cell.codepoint & 0x3F));
            }
        }

        if (r < endRow) {
            result += '\n';
        }
    }

    return result;
}

void TerminalState::setTheme(const TerminalTheme& theme) {
    m_theme = theme;
    // Update default attributes to use theme colors
    m_currentAttrs.fg = theme.foreground;
    m_currentAttrs.bg = theme.background;
}

uint32_t TerminalState::getPaletteColor(int index) const {
    if (index >= 0 && index < 16) {
        return m_theme.palette[index];
    }
    // Extended 256-color palette
    if (index >= 16 && index < 232) {
        // 216 color cube (6x6x6)
        int idx = index - 16;
        int r = (idx / 36) * 51;
        int g = ((idx / 6) % 6) * 51;
        int b = (idx % 6) * 51;
        return 0xFF000000 | (r << 16) | (g << 8) | b;
    }
    if (index >= 232 && index < 256) {
        // 24 grayscale colors
        int gray = (index - 232) * 10 + 8;
        return 0xFF000000 | (gray << 16) | (gray << 8) | gray;
    }
    return m_theme.foreground;
}
