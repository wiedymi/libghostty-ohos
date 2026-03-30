#include "terminal.h"
#include "../renderer/renderer.h"
#include "../include/ghostty_vt.h"
#include <hilog/log.h>
#include <atomic>
#include <cctype>
#include <cstring>
#include <inttypes.h>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <vector>

#undef LOG_TAG
#define LOG_TAG "Terminal"

namespace {
constexpr size_t kGhosttyTextCapacity = 64;
constexpr uint32_t kSearchMatchBackground = 0xFF2F4159;
constexpr uint32_t kSearchMatchForeground = 0xFFFFFFFF;
constexpr uint32_t kSearchCurrentMatchBackground = 0xFFE0A23A;
constexpr uint32_t kSearchCurrentMatchForeground = 0xFF111111;
std::atomic<uint64_t> g_drawFrameCounter {0};

bool IsSuspiciousCodepoint(uint32_t codepoint)
{
    return codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF);
}

uint32_t RgbToArgb(const ghostty_rgb_t& rgb)
{
    return 0xFF000000u |
        (static_cast<uint32_t>(rgb.r) << 16) |
        (static_cast<uint32_t>(rgb.g) << 8) |
        static_cast<uint32_t>(rgb.b);
}

ghostty_rgb_t ArgbToRgb(uint32_t argb)
{
    ghostty_rgb_t rgb {};
    rgb.r = static_cast<uint8_t>((argb >> 16) & 0xFF);
    rgb.g = static_cast<uint8_t>((argb >> 8) & 0xFF);
    rgb.b = static_cast<uint8_t>(argb & 0xFF);
    return rgb;
}

uint32_t ResolveStyleColor(
    const ghostty_style_color_t& color,
    const ghostty_render_state_colors_t& colors,
    uint32_t fallback)
{
    switch (color.tag) {
        case GHOSTTY_STYLE_COLOR_PALETTE:
            return RgbToArgb(colors.palette[color.value.palette]);
        case GHOSTTY_STYLE_COLOR_RGB:
            return RgbToArgb(color.value.rgb);
        case GHOSTTY_STYLE_COLOR_NONE:
        default:
            return fallback;
    }
}

void ApplyGhosttyStyle(
    CellAttributes& attrs,
    const ghostty_style_t& style,
    const ghostty_render_state_colors_t& colors)
{
    attrs.fg = ResolveStyleColor(style.fg_color, colors, RgbToArgb(colors.foreground));
    attrs.bg = ResolveStyleColor(style.bg_color, colors, RgbToArgb(colors.background));
    attrs.bold = style.bold;
    attrs.italic = style.italic;
    attrs.underline = style.underline != 0;
    attrs.strikethrough = style.strikethrough;
    attrs.inverse = style.inverse;
    attrs.hidden = style.invisible;
    attrs.blink = style.blink;
}

void ApplyThemeToRenderColors(
    const TerminalTheme& theme,
    ghostty_render_state_colors_t& colors)
{
    colors.background = ArgbToRgb(theme.background);
    colors.foreground = ArgbToRgb(theme.foreground);
    colors.cursor = ArgbToRgb(theme.cursorColor);
    colors.cursor_has_value = true;
    for (size_t i = 0; i < theme.palette.size(); ++i) {
        colors.palette[i] = ArgbToRgb(theme.palette[i]);
    }
}

void AppendUtf8(std::string& out, uint32_t codepoint)
{
    if (codepoint <= 0x7F) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

void AppendCodepointUtf8(std::string& out, uint32_t codepoint)
{
    AppendUtf8(out, codepoint);
}

void SetCellTextFromCodepoints(Cell& cell, const uint32_t* codepoints, size_t count)
{
    cell.text[0] = '\0';
    cell.textLen = 0;
    if (!codepoints || count == 0) {
        return;
    }

    std::string utf8;
    utf8.reserve(count * 4);
    for (size_t i = 0; i < count; ++i) {
        AppendCodepointUtf8(utf8, codepoints[i]);
    }

    const size_t len = std::min(utf8.size(), kGhosttyTextCapacity - 1);
    if (len > 0) {
        std::memcpy(cell.text, utf8.data(), len);
    }
    cell.text[len] = '\0';
    cell.textLen = static_cast<uint8_t>(len);
}

void ClearCellText(Cell& cell)
{
    cell.text[0] = '\0';
    cell.textLen = 0;
}

uint8_t CellWidthFromGhostty(ghostty_cell_wide_t wide)
{
    switch (wide) {
        case GHOSTTY_CELL_WIDE_WIDE:
            return 2;
        case GHOSTTY_CELL_WIDE_SPACER_TAIL:
            return 3;
        case GHOSTTY_CELL_WIDE_SPACER_HEAD:
            return 4;
        case GHOSTTY_CELL_WIDE_NARROW:
        default:
            return 1;
    }
}

bool IsCellSelected(
    bool selectionActive,
    int startRow,
    int startCol,
    int endRow,
    int endCol,
    int row,
    int col)
{
    if (!selectionActive) {
        return false;
    }

    if (row < startRow || row > endRow) {
        return false;
    }

    const int colStart = row == startRow ? startCol : 0;
    const int colEnd = row == endRow ? endCol : col;
    return col >= colStart && col <= colEnd;
}

int ClampIndex(int value, int limit)
{
    if (limit <= 0) {
        return 0;
    }
    return std::max(0, std::min(value, limit - 1));
}

bool ByteRangesOverlap(size_t lhsStart, size_t lhsEnd, size_t rhsStart, size_t rhsEnd)
{
    return lhsStart < rhsEnd && rhsStart < lhsEnd;
}

std::string AsciiLower(std::string_view value)
{
    std::string lowered;
    lowered.reserve(value.size());
    for (const unsigned char ch : value) {
        lowered.push_back(static_cast<char>(std::tolower(ch)));
    }
    return lowered;
}

bool IsCsiFinalByte(char ch)
{
    const unsigned char uch = static_cast<unsigned char>(ch);
    return uch >= 0x40 && uch <= 0x7E;
}

bool IsUrlDelimiter(unsigned char ch)
{
    switch (ch) {
        case ' ':
        case '\t':
        case '\r':
        case '\n':
        case '"':
        case '\'':
        case '<':
        case '>':
        case '(':
        case ')':
        case '[':
        case ']':
        case '{':
        case '}':
            return true;
        default:
            return false;
    }
}

void TrimUrlToken(std::string& token)
{
    while (!token.empty()) {
        const unsigned char ch = static_cast<unsigned char>(token.back());
        if (ch == '.' || ch == ',' || ch == ';' || ch == ':' || ch == '!' || ch == '?' ||
            ch == ')' || ch == ']' || ch == '}') {
            token.pop_back();
            continue;
        }
        break;
    }
}

bool HasSupportedLinkScheme(std::string_view token)
{
    return token.rfind("http://", 0) == 0 ||
        token.rfind("https://", 0) == 0 ||
        token.rfind("mailto:", 0) == 0 ||
        token.rfind("file://", 0) == 0;
}

bool IsTerminalQueryComplete(std::string_view candidate)
{
    if (candidate.empty() || candidate.front() != '\x1b') {
        return false;
    }

    if (candidate.size() == 1) {
        return false;
    }

    switch (candidate[1]) {
        case '[':
            return IsCsiFinalByte(candidate.back());
        case ']':
            return candidate.back() == '\a' ||
                (candidate.size() >= 2 && candidate.substr(candidate.size() - 2) == "\x1b\\");
        case 'P':
            return candidate.size() >= 2 && candidate.substr(candidate.size() - 2) == "\x1b\\";
        default:
            return candidate.size() >= 2;
    }
}

enum class SelectionTokenKind {
    Whitespace,
    Word,
    Punctuation,
};

SelectionTokenKind ClassifySelectionCodepoint(uint32_t codepoint, bool hasText)
{
    if (!hasText || codepoint == 0) {
        return SelectionTokenKind::Whitespace;
    }

    if (codepoint <= 0x7F) {
        const unsigned char ch = static_cast<unsigned char>(codepoint);
        if (std::isspace(ch)) {
            return SelectionTokenKind::Whitespace;
        }
        if (std::isalnum(ch) || ch == '_') {
            return SelectionTokenKind::Word;
        }
        return SelectionTokenKind::Punctuation;
    }

    return SelectionTokenKind::Word;
}
}

Terminal::Terminal(int cols, int rows)
    : m_cols(cols), m_rows(rows), m_running(false), m_vt(nullptr),
      m_renderState(nullptr), m_rowIterator(nullptr), m_rowCells(nullptr), m_renderer(nullptr) {
    ghostty_terminal_options_t opts {};
    opts.cols = static_cast<uint16_t>(cols);
    opts.rows = static_cast<uint16_t>(rows);
    opts.max_scrollback = 10000;
    if (ghostty_terminal_new(nullptr, &m_vt, opts) != GHOSTTY_SUCCESS) {
        m_vt = nullptr;
        OH_LOG_ERROR(LOG_APP, "ghostty_terminal_new failed");
    } else {
        configureCallbacksLocked();
        applyThemeLocked();
    }
    if (ghostty_render_state_new(nullptr, &m_renderState) != GHOSTTY_SUCCESS) {
        m_renderState = nullptr;
        OH_LOG_ERROR(LOG_APP, "ghostty_render_state_new failed");
    }
    if (ghostty_render_state_row_iterator_new(nullptr, &m_rowIterator) != GHOSTTY_SUCCESS) {
        m_rowIterator = nullptr;
        OH_LOG_ERROR(LOG_APP, "ghostty_render_state_row_iterator_new failed");
    }
    if (ghostty_render_state_row_cells_new(nullptr, &m_rowCells) != GHOSTTY_SUCCESS) {
        m_rowCells = nullptr;
        OH_LOG_ERROR(LOG_APP, "ghostty_render_state_row_cells_new failed");
    }
    OH_LOG_INFO(LOG_APP, "Terminal created %dx%d using libghostty-vt", cols, rows);
}

Terminal::~Terminal() {
    stop();
    if (m_rowCells) {
        ghostty_render_state_row_cells_free(m_rowCells);
        m_rowCells = nullptr;
    }
    if (m_rowIterator) {
        ghostty_render_state_row_iterator_free(m_rowIterator);
        m_rowIterator = nullptr;
    }
    if (m_renderState) {
        ghostty_render_state_free(m_renderState);
        m_renderState = nullptr;
    }
    if (m_vt) {
        ghostty_terminal_free(m_vt);
        m_vt = nullptr;
    }
}

bool Terminal::start() {
    if (m_running) {
        return true;
    }

    m_running = true;
    OH_LOG_INFO(LOG_APP, "Terminal started %dx%d", m_cols, m_rows);
    return true;
}

void Terminal::stop() {
    m_running = false;
}

void Terminal::resize(int cols, int rows) {
    if (cols == m_cols && rows == m_rows) return;

    m_cols = cols;
    m_rows = rows;

    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        ghostty_terminal_resize(
            m_vt,
            static_cast<uint16_t>(cols),
            static_cast<uint16_t>(rows),
            0,
            0);
    }

    OH_LOG_INFO(LOG_APP, "Terminal resized to %dx%d", cols, rows);
    notifyRenderNeeded();
}

void Terminal::feedOutput(const char* data, size_t len) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    if (m_vt && data && len > 0) {
        ghostty_terminal_vt_write(m_vt, reinterpret_cast<const uint8_t*>(data), len);
    }
    notifyRenderNeeded();
}

void Terminal::emitInput(const char* data, size_t len)
{
    if (!data || len == 0 || !m_inputCallback) {
        return;
    }
    m_inputCallback(std::string(data, len));
}

void Terminal::emitInput(const std::string& data)
{
    emitInput(data.data(), data.size());
}

void Terminal::applyThemeLocked()
{
    if (!m_vt) {
        return;
    }

    const ghostty_rgb_t foreground = ArgbToRgb(m_theme.foreground);
    const ghostty_rgb_t background = ArgbToRgb(m_theme.background);
    const ghostty_rgb_t cursor = ArgbToRgb(m_theme.cursorColor);
    ghostty_rgb_t palette[256] {};
    for (size_t i = 0; i < m_theme.palette.size() && i < 256; ++i) {
        palette[i] = ArgbToRgb(m_theme.palette[i]);
    }

    ghostty_terminal_set(m_vt, GHOSTTY_TERMINAL_OPT_COLOR_FOREGROUND, &foreground);
    ghostty_terminal_set(m_vt, GHOSTTY_TERMINAL_OPT_COLOR_BACKGROUND, &background);
    ghostty_terminal_set(m_vt, GHOSTTY_TERMINAL_OPT_COLOR_CURSOR, &cursor);
    ghostty_terminal_set(m_vt, GHOSTTY_TERMINAL_OPT_COLOR_PALETTE, palette);
}

void Terminal::configureCallbacksLocked()
{
    if (!m_vt) {
        return;
    }

    ghostty_terminal_set(m_vt, GHOSTTY_TERMINAL_OPT_USERDATA, this);
    ghostty_terminal_set(m_vt, GHOSTTY_TERMINAL_OPT_WRITE_PTY,
        reinterpret_cast<const void*>(&Terminal::HandleWritePty));
    ghostty_terminal_set(m_vt, GHOSTTY_TERMINAL_OPT_BELL,
        reinterpret_cast<const void*>(&Terminal::HandleBell));
    ghostty_terminal_set(m_vt, GHOSTTY_TERMINAL_OPT_ENQUIRY,
        reinterpret_cast<const void*>(&Terminal::HandleEnquiry));
    ghostty_terminal_set(m_vt, GHOSTTY_TERMINAL_OPT_XTVERSION,
        reinterpret_cast<const void*>(&Terminal::HandleXtversion));
    ghostty_terminal_set(m_vt, GHOSTTY_TERMINAL_OPT_TITLE_CHANGED,
        reinterpret_cast<const void*>(&Terminal::HandleTitleChanged));
    ghostty_terminal_set(m_vt, GHOSTTY_TERMINAL_OPT_SIZE,
        reinterpret_cast<const void*>(&Terminal::HandleSize));
    ghostty_terminal_set(m_vt, GHOSTTY_TERMINAL_OPT_COLOR_SCHEME,
        reinterpret_cast<const void*>(&Terminal::HandleColorScheme));
    ghostty_terminal_set(m_vt, GHOSTTY_TERMINAL_OPT_DEVICE_ATTRIBUTES,
        reinterpret_cast<const void*>(&Terminal::HandleDeviceAttributes));
}

void Terminal::HandleWritePty(ghostty_terminal_t, void* userdata, const uint8_t* data, size_t len)
{
    if (!userdata || !data || len == 0) {
        return;
    }
    static_cast<Terminal*>(userdata)->emitInput(reinterpret_cast<const char*>(data), len);
}

void Terminal::HandleBell(ghostty_terminal_t, void* userdata)
{
    (void)userdata;
    OH_LOG_INFO(LOG_APP, "Terminal bell");
}

ghostty_string_t Terminal::HandleEnquiry(ghostty_terminal_t, void* userdata)
{
    (void)userdata;
    return ghostty_string_t {nullptr, 0};
}

ghostty_string_t Terminal::HandleXtversion(ghostty_terminal_t, void* userdata)
{
    (void)userdata;
    static constexpr uint8_t kVersion[] = "libghostty-ohos";
    return ghostty_string_t {kVersion, sizeof(kVersion) - 1};
}

void Terminal::HandleTitleChanged(ghostty_terminal_t, void* userdata)
{
    if (!userdata) {
        return;
    }
    static_cast<Terminal*>(userdata)->notifyRenderNeeded();
}

bool Terminal::HandleSize(ghostty_terminal_t, void* userdata, ghostty_size_report_size_t* out_size)
{
    if (!userdata || !out_size) {
        return false;
    }

    const auto* terminal = static_cast<const Terminal*>(userdata);
    out_size->rows = static_cast<uint16_t>(terminal->m_rows);
    out_size->columns = static_cast<uint16_t>(terminal->m_cols);
    out_size->cell_width = 0;
    out_size->cell_height = 0;
    return true;
}

bool Terminal::HandleColorScheme(ghostty_terminal_t, void* userdata, ghostty_color_scheme_t* out_scheme)
{
    if (!userdata || !out_scheme) {
        return false;
    }

    const uint32_t background = static_cast<const Terminal*>(userdata)->m_theme.background;
    const uint32_t red = (background >> 16) & 0xFF;
    const uint32_t green = (background >> 8) & 0xFF;
    const uint32_t blue = background & 0xFF;
    const uint32_t luminance = (red * 299u) + (green * 587u) + (blue * 114u);
    *out_scheme = luminance >= 128000u ? GHOSTTY_COLOR_SCHEME_LIGHT : GHOSTTY_COLOR_SCHEME_DARK;
    return true;
}

bool Terminal::HandleDeviceAttributes(ghostty_terminal_t, void* userdata, ghostty_device_attributes_t* out_attrs)
{
    (void)userdata;
    if (!out_attrs) {
        return false;
    }

    std::memset(out_attrs, 0, sizeof(*out_attrs));
    out_attrs->primary.conformance_level = GHOSTTY_DA_CONFORMANCE_VT220;
    out_attrs->primary.features[0] = GHOSTTY_DA_FEATURE_ANSI_COLOR;
    out_attrs->primary.features[1] = GHOSTTY_DA_FEATURE_CLIPBOARD;
    out_attrs->primary.num_features = 2;
    out_attrs->secondary.device_type = GHOSTTY_DA_DEVICE_TYPE_VT220;
    out_attrs->secondary.firmware_version = 0;
    out_attrs->secondary.rom_cartridge = 0;
    out_attrs->tertiary.unit_id = 0;
    return true;
}

namespace {
std::string RgbReplyText(uint32_t argb)
{
    std::ostringstream out;
    out << "rgb:"
        << std::hex << std::nouppercase << std::setfill('0')
        << std::setw(2) << ((argb >> 16) & 0xFF)
        << "/"
        << std::setw(2) << ((argb >> 8) & 0xFF)
        << "/"
        << std::setw(2) << (argb & 0xFF);
    return out.str();
}

std::string OscColorReply(int code, uint32_t argb, bool useBel)
{
    std::ostringstream out;
    out << "\x1b]" << code << ";" << RgbReplyText(argb);
    if (useBel) {
        out << '\a';
    } else {
        out << "\x1b\\";
    }
    return out.str();
}

std::string OscPaletteReply(int index, uint32_t argb, bool useBel)
{
    std::ostringstream out;
    out << "\x1b]4;" << index << ";" << RgbReplyText(argb);
    if (useBel) {
        out << '\a';
    } else {
        out << "\x1b\\";
    }
    return out.str();
}

bool ResolveKittyColorQuery(std::string_view key, const TerminalTheme& theme, std::string& value)
{
    value.clear();
    if (key == "foreground") {
        value = RgbReplyText(theme.foreground);
        return true;
    }
    if (key == "background") {
        value = RgbReplyText(theme.background);
        return true;
    }
    if (key == "cursor") {
        value = RgbReplyText(theme.cursorColor);
        return true;
    }

    const std::string keyText(key);
    char* parseEnd = nullptr;
    const long index = std::strtol(keyText.c_str(), &parseEnd, 10);
    if (parseEnd != keyText.c_str() && *parseEnd == '\0' && index >= 0 && index < 256) {
        const uint32_t color = index < static_cast<long>(theme.palette.size())
            ? theme.palette[static_cast<size_t>(index)]
            : theme.foreground;
        value = RgbReplyText(color);
        return true;
    }

    return false;
}

struct XTGetTCapEntry {
    std::string_view name;
    std::string_view value;
    bool hasValue;
};

bool LookupGhosttyXTGetTCap(std::string_view key, std::string_view& value, bool& hasValue)
{
    static constexpr XTGetTCapEntry kEntries[] = {
        {"TN", "xterm-ghostty", true},
        {"Co", "256", true},
        {"RGB", "8", true},
        {"AX", "", false},
        {"Tc", "", false},
        {"Su", "", false},
        {"XT", "", false},
        {"fullkbd", "", false},
        {"bce", "", false},
        {"ccc", "", false},
        {"hs", "", false},
        {"km", "", false},
        {"mc5i", "", false},
        {"mir", "", false},
        {"msgr", "", false},
        {"npc", "", false},
        {"xenl", "", false},
        {"colors", "256", true},
        {"cols", "80", true},
        {"it", "8", true},
        {"lines", "24", true},
        {"pairs", "32767", true},
        {"acsc", "++\\,\\,--..00``aaffgghhiijjkkllmmnnooppqqrrssttuuvvwwxxyyzz{{||}}~~", true},
        {"Smulx", "\\E[4:%p1%dm", true},
        {"Setulc", "\\E[58:2::%p1%{65536}%/%d:%p1%{256}%/%{255}%&%d:%p1%{255}%&%d%;m", true},
        {"Ss", "\\E[%p1%d q", true},
        {"Se", "\\E[2 q", true},
        {"Ms", "\\E]52;%p1%s;%p2%s\\007", true},
        {"Sync", "\\E[?2026%?%p1%{1}%-%tl%eh%;", true},
        {"BD", "\\E[?2004l", true},
        {"BE", "\\E[?2004h", true},
        {"PS", "\\E[200~", true},
        {"PE", "\\E[201~", true},
        {"XM", "\\E[?1006;1000%?%p1%{1}%=%th%el%;", true},
        {"xm", "\\E[<%i%p3%d;%p1%d;%p2%d;%?%p4%tM%em%;", true},
        {"RV", "\\E[>c", true},
        {"rv", "\\E\\\\[[0-9]+;[0-9]+;[0-9]+c", true},
        {"XR", "\\E[>0q", true},
        {"xr", "\\EP>\\\\|[ -~]+a\\E\\\\", true},
        {"Enmg", "\\E[?69h", true},
        {"Dsmg", "\\E[?69l", true},
        {"Clmg", "\\E[s", true},
        {"Cmg", "\\E[%i%p1%d;%p2%ds", true},
        {"clear", "\\E[H\\E[2J", true},
        {"E3", "\\E[3J", true},
        {"fe", "\\E[?1004h", true},
        {"fd", "\\E[?1004l", true},
        {"kxIN", "\\E[I", true},
        {"kxOUT", "\\E[O", true},
        {"bel", "^G", true},
        {"blink", "\\E[5m", true},
        {"bold", "\\E[1m", true},
        {"cbt", "\\E[Z", true},
        {"civis", "\\E[?25l", true},
        {"cnorm", "\\E[?12l\\E[?25h", true},
        {"cr", "\\r", true},
        {"csr", "\\E[%i%p1%d;%p2%dr", true},
        {"cub", "\\E[%p1%dD", true},
        {"cub1", "^H", true},
        {"cud", "\\E[%p1%dB", true},
        {"cud1", "^J", true},
        {"cuf", "\\E[%p1%dC", true},
        {"cuf1", "\\E[C", true},
        {"cup", "\\E[%i%p1%d;%p2%dH", true},
        {"cuu", "\\E[%p1%dA", true},
        {"cuu1", "\\E[A", true},
        {"cvvis", "\\E[?12;25h", true},
        {"dch", "\\E[%p1%dP", true},
        {"dch1", "\\E[P", true},
        {"dim", "\\E[2m", true},
        {"dl", "\\E[%p1%dM", true},
        {"dl1", "\\E[M", true},
        {"dsl", "\\E]2;\\007", true},
        {"ech", "\\E[%p1%dX", true},
        {"ed", "\\E[J", true},
        {"el", "\\E[K", true},
        {"el1", "\\E[1K", true},
        {"flash", "\\E[?5h$<100/>\\E[?5l", true},
        {"fsl", "^G", true},
        {"home", "\\E[H", true},
        {"hpa", "\\E[%i%p1%dG", true},
        {"ht", "^I", true},
        {"hts", "\\EH", true},
        {"ich", "\\E[%p1%d@", true},
        {"ich1", "\\E[@", true},
        {"il", "\\E[%p1%dL", true},
        {"il1", "\\E[L", true},
        {"ind", "\\n", true},
        {"indn", "\\E[%p1%dS", true},
        {"initc", "\\E]4;%p1%d;rgb\\:%p2%{255}%*%{1000}%/%2.2X/%p3%{255}%*%{1000}%/%2.2X/%p4%{255}%*%{1000}%/%2.2X\\E\\\\", true},
        {"invis", "\\E[8m", true},
        {"oc", "\\E]104\\007", true},
        {"op", "\\E[39;49m", true},
        {"query-os-name", "HarmonyOS", true},
        {"rc", "\\E8", true},
        {"rep", "%p1%c\\E[%p2%{1}%-%db", true},
        {"rev", "\\E[7m", true},
        {"ri", "\\EM", true},
        {"rin", "\\E[%p1%dT", true},
        {"ritm", "\\E[23m", true},
        {"rmacs", "\\E(B", true},
        {"rmam", "\\E[?7l", true},
        {"rmcup", "\\E[?1049l", true},
        {"rmir", "\\E[4l", true},
        {"rmkx", "\\E[?1l\\E>", true},
        {"rmso", "\\E[27m", true},
        {"rmul", "\\E[24m", true},
        {"rmxx", "\\E[29m", true},
        {"setab", "\\E[%?%p1%{8}%<%t4%p1%d%e%p1%{16}%<%t10%p1%{8}%-%d%e48;5;%p1%d%;m", true},
        {"setaf", "\\E[%?%p1%{8}%<%t3%p1%d%e%p1%{16}%<%t9%p1%{8}%-%d%e38;5;%p1%d%;m", true},
        {"setrgbb", "\\E[48:2:%p1%d:%p2%d:%p3%dm", true},
        {"setrgbf", "\\E[38:2:%p1%d:%p2%d:%p3%dm", true},
        {"sgr", "%?%p9%t\\E(0%e\\E(B%;\\E[0%?%p6%t;1%;%?%p5%t;2%;%?%p2%t;4%;%?%p1%p3%|%t;7%;%?%p4%t;5%;%?%p7%t;8%;m", true},
        {"sgr0", "\\E(B\\E[m", true},
        {"sitm", "\\E[3m", true},
        {"smacs", "\\E(0", true},
        {"smam", "\\E[?7h", true},
        {"smcup", "\\E[?1049h", true},
        {"smir", "\\E[4h", true},
        {"smkx", "\\E[?1h\\E=", true},
        {"smso", "\\E[7m", true},
        {"smul", "\\E[4m", true},
        {"smxx", "\\E[9m", true},
        {"tbc", "\\E[3g", true},
        {"tsl", "\\E]2;", true},
        {"u6", "\\E[%i%d;%dR", true},
        {"u7", "\\E[6n", true},
        {"u8", "\\E[?%[;0123456789]c", true},
        {"u9", "\\E[c", true},
        {"vpa", "\\E[%i%p1%dd", true},
    };

    for (const auto& entry : kEntries) {
        if (entry.name == key) {
            value = entry.value;
            hasValue = entry.hasValue;
            return true;
        }
    }

    return false;
}

std::string XTGetTCapReply(std::string_view encodedName, std::string_view value, bool hasValue)
{
    std::ostringstream out;
    out << "\x1bP1+r" << encodedName;
    if (hasValue) {
        out << "=";
        for (unsigned char ch : value) {
            static constexpr char kHex[] = "0123456789ABCDEF";
            out << kHex[(ch >> 4) & 0x0F] << kHex[ch & 0x0F];
        }
    }
    out << "\x1b\\";
    return out.str();
}

std::string EscapePreview(std::string_view text, size_t maxLen = 160)
{
    std::ostringstream out;
    size_t count = 0;
    for (unsigned char ch : text) {
        if (count >= maxLen) {
            out << "...";
            break;
        }
        switch (ch) {
            case '\x1b': out << "\\e"; count += 2; break;
            case '\a': out << "\\a"; count += 2; break;
            case '\r': out << "\\r"; count += 2; break;
            case '\n': out << "\\n"; count += 2; break;
            case '\t': out << "\\t"; count += 2; break;
            default:
                if (ch < 0x20 || ch == 0x7F) {
                    char buf[5];
                    std::snprintf(buf, sizeof(buf), "\\x%02X", ch);
                    out << buf;
                    count += 4;
                } else {
                    out << static_cast<char>(ch);
                    count += 1;
                }
                break;
        }
    }
    return out.str();
}
}

bool Terminal::IsSelectionBefore(int rowA, int colA, int rowB, int colB)
{
    return rowA < rowB || (rowA == rowB && colA <= colB);
}

void Terminal::normalizeSelectionBounds(int& startRow, int& startCol, int& endRow, int& endCol) const
{
    if (IsSelectionBefore(m_selStartRow, m_selStartCol, m_selEndRow, m_selEndCol)) {
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

void Terminal::writeInput(const char* data, size_t len) {
    if (len == 0 || !m_running) {
        return;
    }
    emitInput(data, len);
}

std::string Terminal::getScreenContent() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    if (!m_renderState || !m_vt || ghostty_render_state_update(m_renderState, m_vt) != GHOSTTY_SUCCESS) {
        return {};
    }

    ghostty_row_iterator_t rowIterator = m_rowIterator;
    ghostty_row_cells_t rowCells = m_rowCells;
    ghostty_render_state_get(m_renderState, GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR, &rowIterator);

    std::string result;
    result.reserve(static_cast<size_t>(m_rows) * static_cast<size_t>(m_cols + 1));
    for (int row = 0; row < m_rows && ghostty_render_state_row_iterator_next(rowIterator); ++row) {
        ghostty_render_state_row_get(rowIterator, GHOSTTY_RENDER_STATE_ROW_DATA_CELLS, &rowCells);
        for (int col = 0; col < m_cols && ghostty_render_state_row_cells_next(rowCells); ++col) {
            ghostty_cell_t raw = 0;
            bool hasText = false;
            ghostty_cell_wide_t wide = GHOSTTY_CELL_WIDE_NARROW;
            uint32_t codepoint = 0;
            ghostty_render_state_row_cells_get(rowCells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_RAW, &raw);
            ghostty_cell_get(raw, GHOSTTY_CELL_DATA_HAS_TEXT, &hasText);
            ghostty_cell_get(raw, GHOSTTY_CELL_DATA_WIDE, &wide);
            ghostty_cell_get(raw, GHOSTTY_CELL_DATA_CODEPOINT, &codepoint);
            if (!hasText || wide == GHOSTTY_CELL_WIDE_SPACER_TAIL || wide == GHOSTTY_CELL_WIDE_SPACER_HEAD) {
                result.push_back(' ');
                continue;
            }
            if (codepoint == 0) {
                result.push_back(' ');
                continue;
            }
            AppendCodepointUtf8(result, codepoint);
        }
        result.push_back('\n');
    }
    return result;
}

void Terminal::getCursorPosition(int& row, int& col) const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    row = 0;
    col = 0;
    if (!m_vt) {
        return;
    }
    if (m_renderState && ghostty_render_state_update(m_renderState, m_vt) == GHOSTTY_SUCCESS) {
        bool cursorHasViewport = false;
        bool cursorWideTail = false;
        ghostty_render_state_get(m_renderState, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_HAS_VALUE, &cursorHasViewport);
        if (cursorHasViewport) {
            ghostty_render_state_get(m_renderState, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_Y, &row);
            ghostty_render_state_get(m_renderState, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_X, &col);
            ghostty_render_state_get(m_renderState, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_WIDE_TAIL, &cursorWideTail);
            if (cursorWideTail && col > 0) {
                --col;
            }
            return;
        }
    }
    ghostty_terminal_get(m_vt, GHOSTTY_TERMINAL_DATA_CURSOR_Y, &row);
    ghostty_terminal_get(m_vt, GHOSTTY_TERMINAL_DATA_CURSOR_X, &col);
}

std::string Terminal::getLinkAt(int row, int col) const
{
    std::lock_guard<std::mutex> lock(m_stateMutex);
    if (!m_renderState || !m_vt || row < 0 || col < 0 || row >= m_rows || col >= m_cols) {
        return {};
    }
    if (ghostty_render_state_update(m_renderState, m_vt) != GHOSTTY_SUCCESS) {
        return {};
    }

    ghostty_row_iterator_t rowIterator = m_rowIterator;
    ghostty_row_cells_t rowCells = m_rowCells;
    ghostty_render_state_get(m_renderState, GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR, &rowIterator);

    for (int currentRow = 0; currentRow < m_rows && ghostty_render_state_row_iterator_next(rowIterator); ++currentRow) {
        if (currentRow != row) {
            continue;
        }

        ghostty_render_state_row_get(rowIterator, GHOSTTY_RENDER_STATE_ROW_DATA_CELLS, &rowCells);
        std::string rowText;
        std::vector<size_t> colOffsets(static_cast<size_t>(m_cols) + 1, 0);
        rowText.reserve(static_cast<size_t>(m_cols));

        for (int currentCol = 0; currentCol < m_cols && ghostty_render_state_row_cells_next(rowCells); ++currentCol) {
            colOffsets[static_cast<size_t>(currentCol)] = rowText.size();

            ghostty_cell_t raw = 0;
            bool hasText = false;
            ghostty_cell_wide_t wide = GHOSTTY_CELL_WIDE_NARROW;
            uint32_t codepoint = 0;
            ghostty_render_state_row_cells_get(rowCells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_RAW, &raw);
            ghostty_cell_get(raw, GHOSTTY_CELL_DATA_HAS_TEXT, &hasText);
            ghostty_cell_get(raw, GHOSTTY_CELL_DATA_WIDE, &wide);
            ghostty_cell_get(raw, GHOSTTY_CELL_DATA_CODEPOINT, &codepoint);

            if (!hasText || wide == GHOSTTY_CELL_WIDE_SPACER_TAIL || wide == GHOSTTY_CELL_WIDE_SPACER_HEAD || codepoint == 0) {
                rowText.push_back(' ');
            } else {
                AppendCodepointUtf8(rowText, codepoint);
            }
        }
        colOffsets[static_cast<size_t>(m_cols)] = rowText.size();

        const size_t startOffset = colOffsets[static_cast<size_t>(col)];
        if (startOffset >= rowText.size()) {
            return {};
        }
        if (IsUrlDelimiter(static_cast<unsigned char>(rowText[startOffset]))) {
            return {};
        }

        size_t left = startOffset;
        while (left > 0 && !IsUrlDelimiter(static_cast<unsigned char>(rowText[left - 1]))) {
            --left;
        }

        size_t right = startOffset;
        while (right < rowText.size() && !IsUrlDelimiter(static_cast<unsigned char>(rowText[right]))) {
            ++right;
        }

        std::string token = rowText.substr(left, right - left);
        TrimUrlToken(token);
        if (!HasSupportedLinkScheme(token)) {
            return {};
        }
        return token;
    }

    return {};
}

void Terminal::scrollView(int delta) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    if (!m_vt) return;
    ghostty_terminal_scroll_viewport_t behavior {};
    behavior.tag = GHOSTTY_SCROLL_VIEWPORT_DELTA;
    behavior.value.delta = delta;
    // The prebuilt libghostty-vt binary expects a pointer here even though the
    // local header declares the behavior parameter by value.
    using ScrollViewportFn = void (*)(ghostty_terminal_t, const ghostty_terminal_scroll_viewport_t*);
    auto scrollViewport = reinterpret_cast<ScrollViewportFn>(ghostty_terminal_scroll_viewport);
    scrollViewport(m_vt, &behavior);
    notifyRenderNeeded();
}

void Terminal::resetViewScroll() {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    if (!m_vt) return;
    scrollViewportLocked(GHOSTTY_SCROLL_VIEWPORT_BOTTOM);
    notifyRenderNeeded();
}

ghostty_terminal_scrollbar_t Terminal::getScrollbarLocked() const
{
    ghostty_terminal_scrollbar_t scrollbar {};
    if (!m_vt) {
        return scrollbar;
    }
    if (ghostty_terminal_get(m_vt, GHOSTTY_TERMINAL_DATA_SCROLLBAR, &scrollbar) != GHOSTTY_SUCCESS) {
        scrollbar.total = static_cast<uint64_t>(m_rows);
        scrollbar.len = static_cast<uint64_t>(m_rows);
        scrollbar.offset = 0;
    }
    return scrollbar;
}

void Terminal::scrollViewportLocked(ghostty_terminal_scroll_viewport_tag_t tag, int64_t delta)
{
    if (!m_vt) {
        return;
    }

    ghostty_terminal_scroll_viewport_t behavior {};
    behavior.tag = tag;
    behavior.value.delta = delta;
    using ScrollViewportFn = void (*)(ghostty_terminal_t, const ghostty_terminal_scroll_viewport_t*);
    auto scrollViewport = reinterpret_cast<ScrollViewportFn>(ghostty_terminal_scroll_viewport);
    scrollViewport(m_vt, &behavior);
}

int64_t Terminal::determineScrollStepTowardsBottomLocked()
{
    if (!m_vt) {
        return 0;
    }

    const ghostty_terminal_scrollbar_t originalScrollbar = getScrollbarLocked();
    const size_t viewportRows = static_cast<size_t>(std::max<uint64_t>(
        1,
        originalScrollbar.len > 0 ? originalScrollbar.len : static_cast<uint64_t>(m_rows)));
    const size_t totalRows = static_cast<size_t>(std::max<uint64_t>(
        viewportRows,
        originalScrollbar.total > 0 ? originalScrollbar.total : static_cast<uint64_t>(m_rows)));
    const size_t maxOffset = totalRows > viewportRows ? totalRows - viewportRows : 0;
    const size_t originalOffset = std::min(static_cast<size_t>(originalScrollbar.offset), maxOffset);
    if (maxOffset == 0) {
        return 0;
    }

    scrollViewportLocked(GHOSTTY_SCROLL_VIEWPORT_TOP);
    int64_t stepTowardsBottom = 0;
    for (const int64_t candidate : {-1LL, 1LL}) {
        scrollViewportLocked(GHOSTTY_SCROLL_VIEWPORT_DELTA, candidate);
        const size_t candidateOffset = std::min(static_cast<size_t>(getScrollbarLocked().offset), maxOffset);
        if (candidateOffset > 0) {
            stepTowardsBottom = candidate;
            break;
        }
        scrollViewportLocked(GHOSTTY_SCROLL_VIEWPORT_TOP);
    }

    if (stepTowardsBottom != 0 && originalOffset > 0) {
        scrollViewportLocked(
            GHOSTTY_SCROLL_VIEWPORT_DELTA,
            stepTowardsBottom * static_cast<int64_t>(originalOffset));
    }

    return stepTowardsBottom;
}

std::vector<std::string> Terminal::captureScrollbackSnapshotLocked(size_t& viewportTopRow)
{
    viewportTopRow = 0;
    if (!m_vt || m_rows <= 0 || m_cols <= 0) {
        return {};
    }

    const ghostty_terminal_scrollbar_t originalScrollbar = getScrollbarLocked();
    const size_t viewportRows = static_cast<size_t>(std::max<uint64_t>(1, originalScrollbar.len > 0 ? originalScrollbar.len : static_cast<uint64_t>(m_rows)));
    const size_t totalRows = static_cast<size_t>(std::max<uint64_t>(viewportRows, originalScrollbar.total > 0 ? originalScrollbar.total : static_cast<uint64_t>(m_rows)));
    const size_t maxOffset = totalRows > viewportRows ? totalRows - viewportRows : 0;
    viewportTopRow = std::min(static_cast<size_t>(originalScrollbar.offset), maxOffset);
    std::vector<std::string> snapshot;
    snapshot.reserve(totalRows);

    for (size_t row = 0; row < totalRows; ++row) {
        std::string line;
        line.reserve(static_cast<size_t>(m_cols));
        for (int col = 0; col < m_cols; ++col) {
            ghostty_point_t point {
                .tag = GHOSTTY_POINT_TAG_SCREEN,
                .value = { .coordinate = {
                    .x = static_cast<uint16_t>(col),
                    .y = static_cast<uint32_t>(row),
                } },
            };
            ghostty_grid_ref_t ref = GHOSTTY_INIT_SIZED(ghostty_grid_ref_t);
            if (ghostty_terminal_grid_ref(m_vt, point, &ref) != GHOSTTY_SUCCESS) {
                line.push_back(' ');
                continue;
            }

            ghostty_cell_t raw = 0;
            if (ghostty_grid_ref_cell(&ref, &raw) != GHOSTTY_SUCCESS) {
                line.push_back(' ');
                continue;
            }

            bool hasText = false;
            ghostty_cell_wide_t wide = GHOSTTY_CELL_WIDE_NARROW;
            uint32_t codepoint = 0;
            ghostty_cell_get(raw, GHOSTTY_CELL_DATA_HAS_TEXT, &hasText);
            ghostty_cell_get(raw, GHOSTTY_CELL_DATA_WIDE, &wide);
            ghostty_cell_get(raw, GHOSTTY_CELL_DATA_CODEPOINT, &codepoint);

            if (!hasText || wide == GHOSTTY_CELL_WIDE_SPACER_TAIL ||
                wide == GHOSTTY_CELL_WIDE_SPACER_HEAD || codepoint == 0) {
                line.push_back(' ');
            } else {
                AppendCodepointUtf8(line, codepoint);
            }
        }
        snapshot.push_back(std::move(line));
    }

    return snapshot;
}

void Terminal::rebuildSearchMatchesLocked()
{
    m_searchMatches.clear();
    m_searchSelectedIndex = -1;

    if (!m_searchActive || m_searchQuery.empty()) {
        const ghostty_terminal_scrollbar_t scrollbar = getScrollbarLocked();
        const size_t viewportRows = static_cast<size_t>(std::max<uint64_t>(1, scrollbar.len > 0 ? scrollbar.len : static_cast<uint64_t>(m_rows)));
        const size_t totalRows = static_cast<size_t>(std::max<uint64_t>(viewportRows, scrollbar.total > 0 ? scrollbar.total : static_cast<uint64_t>(m_rows)));
        const size_t maxOffset = totalRows > viewportRows ? totalRows - viewportRows : 0;
        m_searchViewportTopRow = std::min(static_cast<size_t>(scrollbar.offset), maxOffset);
        return;
    }

    size_t viewportTopRow = 0;
    std::vector<std::string> snapshot = captureScrollbackSnapshotLocked(viewportTopRow);
    m_searchViewportTopRow = viewportTopRow;
    const std::string loweredQuery = AsciiLower(m_searchQuery);

    for (size_t row = 0; row < snapshot.size(); ++row) {
        const std::string loweredLine = AsciiLower(snapshot[row]);
        size_t pos = loweredLine.find(loweredQuery);
        while (pos != std::string::npos) {
            SearchMatch match;
            match.row = row;
            match.startByte = pos;
            match.endByte = pos + m_searchQuery.size();
            m_searchMatches.push_back(match);
            pos = loweredLine.find(loweredQuery, pos + 1);
        }
    }

    if (m_searchMatches.empty()) {
        return;
    }

    const ghostty_terminal_scrollbar_t scrollbar = getScrollbarLocked();
    const size_t viewportRows = static_cast<size_t>(std::max<uint64_t>(1, scrollbar.len > 0 ? scrollbar.len : static_cast<uint64_t>(m_rows)));
    for (size_t i = 0; i < m_searchMatches.size(); ++i) {
        const SearchMatch& match = m_searchMatches[i];
        if (match.row >= viewportTopRow && match.row < viewportTopRow + viewportRows) {
            m_searchSelectedIndex = static_cast<int>(i);
            return;
        }
    }

    m_searchSelectedIndex = 0;
}

void Terminal::syncSearchSelectionToViewportLocked()
{
    if (!m_searchActive || m_searchSelectedIndex < 0 ||
        m_searchSelectedIndex >= static_cast<int>(m_searchMatches.size())) {
        return;
    }

    const SearchMatch& match = m_searchMatches[static_cast<size_t>(m_searchSelectedIndex)];
    const ghostty_terminal_scrollbar_t scrollbar = getScrollbarLocked();
    const size_t viewportRows = static_cast<size_t>(std::max<uint64_t>(1, scrollbar.len > 0 ? scrollbar.len : static_cast<uint64_t>(m_rows)));
    const size_t totalRows = static_cast<size_t>(std::max<uint64_t>(viewportRows, scrollbar.total > 0 ? scrollbar.total : static_cast<uint64_t>(m_rows)));
    const size_t maxOffset = totalRows > viewportRows ? totalRows - viewportRows : 0;
    const size_t currentTop = std::min(static_cast<size_t>(scrollbar.offset), maxOffset);
    m_searchViewportTopRow = currentTop;

    if (match.row >= currentTop && match.row < currentTop + viewportRows) {
        return;
    }

    const size_t targetTop = std::min(
        match.row > viewportRows / 2 ? match.row - (viewportRows / 2) : 0,
        maxOffset);
    const int64_t stepTowardsBottom = determineScrollStepTowardsBottomLocked();
    const int64_t logicalDelta = static_cast<int64_t>(targetTop) - static_cast<int64_t>(currentTop);
    if (stepTowardsBottom != 0 && logicalDelta != 0) {
        scrollViewportLocked(
            GHOSTTY_SCROLL_VIEWPORT_DELTA,
            stepTowardsBottom * logicalDelta);
        m_searchViewportTopRow = targetTop;
    }
}

void Terminal::startSearch(const std::string& query)
{
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_searchActive = true;
        m_searchQuery = query;
        rebuildSearchMatchesLocked();
        syncSearchSelectionToViewportLocked();
    }
    notifyRenderNeeded();
}

void Terminal::searchSelection()
{
    startSearch(getSelectedText());
}

void Terminal::updateSearch(const std::string& query)
{
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_searchActive = true;
        m_searchQuery = query;
        rebuildSearchMatchesLocked();
        syncSearchSelectionToViewportLocked();
    }
    notifyRenderNeeded();
}

void Terminal::navigateSearch(bool next)
{
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        if (!m_searchActive || m_searchMatches.empty()) {
            return;
        }

        if (m_searchSelectedIndex < 0) {
            m_searchSelectedIndex = next ? 0 : static_cast<int>(m_searchMatches.size()) - 1;
        } else if (next) {
            m_searchSelectedIndex = (m_searchSelectedIndex + 1) % static_cast<int>(m_searchMatches.size());
        } else {
            m_searchSelectedIndex =
                (m_searchSelectedIndex - 1 + static_cast<int>(m_searchMatches.size())) %
                static_cast<int>(m_searchMatches.size());
        }
        syncSearchSelectionToViewportLocked();
    }
    notifyRenderNeeded();
}

void Terminal::endSearch()
{
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        if (!m_searchActive && m_searchQuery.empty() && m_searchMatches.empty()) {
            return;
        }
        m_searchActive = false;
        m_searchQuery.clear();
        m_searchMatches.clear();
        m_searchSelectedIndex = -1;
    }
    notifyRenderNeeded();
}

TerminalSearchStatus Terminal::getSearchStatus() const
{
    std::lock_guard<std::mutex> lock(m_stateMutex);
    TerminalSearchStatus status;
    status.active = m_searchActive;
    status.total = m_searchMatches.size();
    status.selected = m_searchSelectedIndex;
    status.query = m_searchQuery;
    return status;
}

int Terminal::getScrollbackSize() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    if (!m_vt) return 0;
    const ghostty_terminal_scrollbar_t scrollbar = getScrollbarLocked();
    if (scrollbar.total <= scrollbar.len) {
        return 0;
    }
    return static_cast<int>(scrollbar.total - scrollbar.len);
}

bool Terminal::hasSelection() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_selectionActive;
}

bool Terminal::isSelectionAt(int row, int col) const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    if (!m_selectionActive) {
        return false;
    }

    const int clampedRow = ClampIndex(row, m_rows);
    const int clampedCol = ClampIndex(col, m_cols);
    int startRow = 0;
    int startCol = 0;
    int endRow = 0;
    int endCol = 0;
    normalizeSelectionBounds(startRow, startCol, endRow, endCol);
    return IsCellSelected(true, startRow, startCol, endRow, endCol, clampedRow, clampedCol);
}

void Terminal::startSelection(int row, int col) {
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_selectionActive = true;
        m_selStartRow = ClampIndex(row, m_rows);
        m_selStartCol = ClampIndex(col, m_cols);
        m_selEndRow = m_selStartRow;
        m_selEndCol = m_selStartCol;
    }
    notifyRenderNeeded();
}

void Terminal::updateSelection(int row, int col) {
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        if (!m_selectionActive) {
            return;
        }
        const int nextRow = ClampIndex(row, m_rows);
        const int nextCol = ClampIndex(col, m_cols);
        changed = nextRow != m_selEndRow || nextCol != m_selEndCol;
        m_selEndRow = nextRow;
        m_selEndCol = nextCol;
    }
    if (changed) {
        notifyRenderNeeded();
    }
}

void Terminal::selectWordAt(int row, int col) {
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        if (!m_renderState || !m_vt || m_rows <= 0 || m_cols <= 0) {
            return;
        }
        if (ghostty_render_state_update(m_renderState, m_vt) != GHOSTTY_SUCCESS) {
            return;
        }

        const int targetRow = ClampIndex(row, m_rows);
        const int targetCol = ClampIndex(col, m_cols);
        ghostty_row_iterator_t rowIterator = m_rowIterator;
        ghostty_row_cells_t rowCells = m_rowCells;
        ghostty_render_state_get(m_renderState, GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR, &rowIterator);

        std::vector<uint32_t> codepoints(static_cast<size_t>(m_cols), 0);
        std::vector<bool> hasText(static_cast<size_t>(m_cols), false);

        bool foundRow = false;
        for (int currentRow = 0; currentRow < m_rows && ghostty_render_state_row_iterator_next(rowIterator); ++currentRow) {
            if (currentRow != targetRow) {
                continue;
            }

            foundRow = true;
            ghostty_render_state_row_get(rowIterator, GHOSTTY_RENDER_STATE_ROW_DATA_CELLS, &rowCells);
            for (int currentCol = 0; currentCol < m_cols && ghostty_render_state_row_cells_next(rowCells); ++currentCol) {
                ghostty_cell_t raw = 0;
                ghostty_cell_wide_t wide = GHOSTTY_CELL_WIDE_NARROW;
                uint32_t codepointValue = 0;
                bool cellHasText = false;

                ghostty_render_state_row_cells_get(rowCells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_RAW, &raw);
                ghostty_cell_get(raw, GHOSTTY_CELL_DATA_WIDE, &wide);
                ghostty_cell_get(raw, GHOSTTY_CELL_DATA_CODEPOINT, &codepointValue);
                ghostty_cell_get(raw, GHOSTTY_CELL_DATA_HAS_TEXT, &cellHasText);

                if (wide == GHOSTTY_CELL_WIDE_SPACER_TAIL || wide == GHOSTTY_CELL_WIDE_SPACER_HEAD) {
                    cellHasText = false;
                    codepointValue = 0;
                }

                codepoints[static_cast<size_t>(currentCol)] = codepointValue;
                hasText[static_cast<size_t>(currentCol)] = cellHasText;
            }
            break;
        }

        if (!foundRow) {
            return;
        }

        const SelectionTokenKind targetKind =
            ClassifySelectionCodepoint(codepoints[static_cast<size_t>(targetCol)], hasText[static_cast<size_t>(targetCol)]);
        int startCol = targetCol;
        int endCol = targetCol;

        while (startCol > 0 &&
               ClassifySelectionCodepoint(codepoints[static_cast<size_t>(startCol - 1)],
                                          hasText[static_cast<size_t>(startCol - 1)]) == targetKind) {
            --startCol;
        }
        while (endCol + 1 < m_cols &&
               ClassifySelectionCodepoint(codepoints[static_cast<size_t>(endCol + 1)],
                                          hasText[static_cast<size_t>(endCol + 1)]) == targetKind) {
            ++endCol;
        }

        changed = !m_selectionActive ||
            m_selStartRow != targetRow || m_selEndRow != targetRow ||
            m_selStartCol != startCol || m_selEndCol != endCol;
        m_selectionActive = true;
        m_selStartRow = targetRow;
        m_selEndRow = targetRow;
        m_selStartCol = startCol;
        m_selEndCol = endCol;
    }
    if (changed) {
        notifyRenderNeeded();
    }
}

void Terminal::selectLineAt(int row) {
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        if (!m_renderState || !m_vt || m_rows <= 0 || m_cols <= 0) {
            return;
        }
        if (ghostty_render_state_update(m_renderState, m_vt) != GHOSTTY_SUCCESS) {
            return;
        }

        const int targetRow = ClampIndex(row, m_rows);
        ghostty_row_iterator_t rowIterator = m_rowIterator;
        ghostty_row_cells_t rowCells = m_rowCells;
        ghostty_render_state_get(m_renderState, GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR, &rowIterator);

        int lastTextCol = -1;
        bool foundRow = false;
        for (int currentRow = 0; currentRow < m_rows && ghostty_render_state_row_iterator_next(rowIterator); ++currentRow) {
            if (currentRow != targetRow) {
                continue;
            }

            foundRow = true;
            ghostty_render_state_row_get(rowIterator, GHOSTTY_RENDER_STATE_ROW_DATA_CELLS, &rowCells);
            for (int currentCol = 0; currentCol < m_cols && ghostty_render_state_row_cells_next(rowCells); ++currentCol) {
                ghostty_cell_t raw = 0;
                ghostty_cell_wide_t wide = GHOSTTY_CELL_WIDE_NARROW;
                bool cellHasText = false;

                ghostty_render_state_row_cells_get(rowCells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_RAW, &raw);
                ghostty_cell_get(raw, GHOSTTY_CELL_DATA_WIDE, &wide);
                ghostty_cell_get(raw, GHOSTTY_CELL_DATA_HAS_TEXT, &cellHasText);
                if (wide == GHOSTTY_CELL_WIDE_SPACER_TAIL || wide == GHOSTTY_CELL_WIDE_SPACER_HEAD) {
                    cellHasText = false;
                }
                if (cellHasText) {
                    lastTextCol = currentCol;
                }
            }
            break;
        }

        if (!foundRow) {
            return;
        }

        const int endCol = std::max(0, lastTextCol);
        changed = !m_selectionActive ||
            m_selStartRow != targetRow || m_selEndRow != targetRow ||
            m_selStartCol != 0 || m_selEndCol != endCol;
        m_selectionActive = true;
        m_selStartRow = targetRow;
        m_selEndRow = targetRow;
        m_selStartCol = 0;
        m_selEndCol = endCol;
    }
    if (changed) {
        notifyRenderNeeded();
    }
}

void Terminal::clearSelection() {
    bool hadSelection = false;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        if (!m_selectionActive) {
            return;
        }
        m_selectionActive = false;
        hadSelection = true;
    }
    if (hadSelection) {
        notifyRenderNeeded();
    }
}

std::string Terminal::getSelectedText() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    if (!m_selectionActive || !m_renderState || !m_vt) {
        return {};
    }
    if (ghostty_render_state_update(m_renderState, m_vt) != GHOSTTY_SUCCESS) {
        return {};
    }

    ghostty_row_iterator_t rowIterator = m_rowIterator;
    ghostty_row_cells_t rowCells = m_rowCells;
    ghostty_render_state_get(m_renderState, GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR, &rowIterator);

    int startRow = 0;
    int startCol = 0;
    int endRow = 0;
    int endCol = 0;
    normalizeSelectionBounds(startRow, startCol, endRow, endCol);

    std::string result;
    for (int row = 0; row < m_rows && ghostty_render_state_row_iterator_next(rowIterator); ++row) {
        ghostty_render_state_row_get(rowIterator, GHOSTTY_RENDER_STATE_ROW_DATA_CELLS, &rowCells);
        if (row < startRow || row > endRow) {
            continue;
        }

        bool rowHasSelection = false;
        for (int col = 0; col < m_cols && ghostty_render_state_row_cells_next(rowCells); ++col) {
            ghostty_cell_t raw = 0;
            bool hasText = false;
            ghostty_cell_wide_t wide = GHOSTTY_CELL_WIDE_NARROW;
            uint32_t codepoint = 0;

            ghostty_render_state_row_cells_get(rowCells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_RAW, &raw);
            ghostty_cell_get(raw, GHOSTTY_CELL_DATA_HAS_TEXT, &hasText);
            ghostty_cell_get(raw, GHOSTTY_CELL_DATA_WIDE, &wide);
            ghostty_cell_get(raw, GHOSTTY_CELL_DATA_CODEPOINT, &codepoint);

            if (!IsCellSelected(true, startRow, startCol, endRow, endCol, row, col)) {
                continue;
            }

            rowHasSelection = true;
            if (!hasText || wide == GHOSTTY_CELL_WIDE_SPACER_TAIL || wide == GHOSTTY_CELL_WIDE_SPACER_HEAD || codepoint == 0) {
                result.push_back(' ');
            } else {
                AppendCodepointUtf8(result, codepoint);
            }
        }

        if (rowHasSelection && row < endRow) {
            result.push_back('\n');
        }
    }

    return result;
}

void Terminal::setMaxScrollback(int lines) {
    // ghostty handles scrollback internally
    (void)lines;
}

void Terminal::setTheme(const TerminalTheme& theme) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_theme = theme;
    applyThemeLocked();
    notifyRenderNeeded();
}

const TerminalTheme& Terminal::getTheme() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_theme;
}

void Terminal::notifyRenderNeeded()
{
    if (m_renderRequestCallback) {
        m_renderRequestCallback();
    }
}

void Terminal::drawFrame() {
    if (!m_renderer) return;

    std::lock_guard<std::mutex> lock(m_stateMutex);
    if (!m_vt || !m_renderState || !m_rowIterator || !m_rowCells) {
        return;
    }
    if (ghostty_render_state_update(m_renderState, m_vt) != GHOSTTY_SUCCESS) {
        return;
    }
    const uint64_t frameId = ++g_drawFrameCounter;

    std::vector<Cell> cells(m_cols * m_rows);
    ghostty_render_state_colors_t colors {};
    colors.size = sizeof(colors);
    ghostty_render_state_colors_get(m_renderState, &colors);
    ghostty_render_state_get(m_renderState, GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR, &m_rowIterator);

    int cursorRow = 0;
    int cursorCol = 0;
    bool cursorVisible = false;
    bool cursorHasViewport = false;
    bool cursorWideTail = false;
    int suspiciousCells = 0;
    int selectionStartRow = 0;
    int selectionStartCol = 0;
    int selectionEndRow = 0;
    int selectionEndCol = 0;
    const bool selectionActive = m_selectionActive;
    const bool searchActive = m_searchActive;
    const std::string searchQuery = m_searchQuery;
    const std::vector<SearchMatch> searchMatches = m_searchMatches;
    const int selectedSearchIndex = m_searchSelectedIndex;
    if (selectionActive) {
        normalizeSelectionBounds(selectionStartRow, selectionStartCol, selectionEndRow, selectionEndCol);
    }
    const ghostty_terminal_scrollbar_t scrollbar = getScrollbarLocked();
    const size_t viewportRows = static_cast<size_t>(std::max<uint64_t>(1, scrollbar.len > 0 ? scrollbar.len : static_cast<uint64_t>(m_rows)));
    const size_t totalRows = static_cast<size_t>(std::max<uint64_t>(viewportRows, scrollbar.total > 0 ? scrollbar.total : static_cast<uint64_t>(m_rows)));
    const size_t maxOffset = totalRows > viewportRows ? totalRows - viewportRows : 0;
    const size_t viewportTopRow = std::min(static_cast<size_t>(scrollbar.offset), maxOffset);
    m_searchViewportTopRow = viewportTopRow;
    ghostty_render_state_get(m_renderState, GHOSTTY_RENDER_STATE_DATA_CURSOR_VISIBLE, &cursorVisible);
    ghostty_render_state_get(m_renderState, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_HAS_VALUE, &cursorHasViewport);
    if (cursorHasViewport) {
        ghostty_render_state_get(m_renderState, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_Y, &cursorRow);
        ghostty_render_state_get(m_renderState, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_X, &cursorCol);
        ghostty_render_state_get(m_renderState, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_WIDE_TAIL, &cursorWideTail);
        if (cursorWideTail && cursorCol > 0) {
            --cursorCol;
        }
    }

    for (int row = 0; row < m_rows && ghostty_render_state_row_iterator_next(m_rowIterator); ++row) {
        ghostty_render_state_row_get(m_rowIterator, GHOSTTY_RENDER_STATE_ROW_DATA_CELLS, &m_rowCells);
        std::string rowText;
        std::vector<size_t> rowByteStart(static_cast<size_t>(m_cols), 0);
        std::vector<size_t> rowByteEnd(static_cast<size_t>(m_cols), 0);
        rowText.reserve(static_cast<size_t>(m_cols));
        for (int col = 0; col < m_cols && ghostty_render_state_row_cells_next(m_rowCells); ++col) {
            Cell& dst = cells[row * m_cols + col];
            ghostty_cell_t raw = 0;
            bool hasText = false;
            bool hasStyling = false;
            ghostty_cell_wide_t wide = GHOSTTY_CELL_WIDE_NARROW;
            uint32_t codepoint = 0;
            ghostty_style_t style {};
            ghostty_style_color_t fgColor {};
            ghostty_style_color_t bgColor {};

            ghostty_render_state_row_cells_get(m_rowCells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_RAW, &raw);
            ghostty_cell_get(raw, GHOSTTY_CELL_DATA_HAS_TEXT, &hasText);
            ghostty_cell_get(raw, GHOSTTY_CELL_DATA_HAS_STYLING, &hasStyling);
            ghostty_cell_get(raw, GHOSTTY_CELL_DATA_WIDE, &wide);
            ghostty_cell_get(raw, GHOSTTY_CELL_DATA_CODEPOINT, &codepoint);

            rowByteStart[static_cast<size_t>(col)] = rowText.size();
            if (!hasText || wide == GHOSTTY_CELL_WIDE_SPACER_TAIL || wide == GHOSTTY_CELL_WIDE_SPACER_HEAD || codepoint == 0) {
                rowText.push_back(' ');
            } else {
                AppendCodepointUtf8(rowText, codepoint);
            }
            rowByteEnd[static_cast<size_t>(col)] = rowText.size();

            dst.codepoint = codepoint;
            dst.width = CellWidthFromGhostty(wide);
            dst.attrs.bg = RgbToArgb(colors.background);
            dst.attrs.fg = RgbToArgb(colors.foreground);
            dst.attrs.bold = false;
            dst.attrs.italic = false;
            dst.attrs.underline = false;
            dst.attrs.strikethrough = false;
            dst.attrs.inverse = false;
            dst.attrs.hidden = false;
            dst.attrs.blink = false;
            dst.selected = false;
            ClearCellText(dst);

            if (hasStyling) {
                style.size = sizeof(style);
                if (ghostty_render_state_row_cells_get(m_rowCells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_STYLE, &style) ==
                    GHOSTTY_SUCCESS) {
                    ApplyGhosttyStyle(dst.attrs, style, colors);
                }
            }

            fgColor.tag = GHOSTTY_STYLE_COLOR_NONE;
            if (ghostty_render_state_row_cells_get(m_rowCells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_FG_COLOR, &fgColor) ==
                GHOSTTY_SUCCESS) {
                dst.attrs.fg = ResolveStyleColor(fgColor, colors, dst.attrs.fg);
            }

            bgColor.tag = GHOSTTY_STYLE_COLOR_NONE;
            if (ghostty_render_state_row_cells_get(m_rowCells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_BG_COLOR, &bgColor) ==
                GHOSTTY_SUCCESS) {
                dst.attrs.bg = ResolveStyleColor(bgColor, colors, dst.attrs.bg);
            }

            if (hasText && wide != GHOSTTY_CELL_WIDE_SPACER_TAIL && wide != GHOSTTY_CELL_WIDE_SPACER_HEAD) {
                if (codepoint != 0) {
                    SetCellTextFromCodepoints(dst, &codepoint, 1);
                }
            }

            if (IsSuspiciousCodepoint(codepoint) && suspiciousCells < 8) {
                ++suspiciousCells;
                OH_LOG_ERROR(LOG_APP,
                    "drawFrame suspicious cell frame=%{public}" PRIu64 " row=%{public}d col=%{public}d raw=0x%{public}" PRIX64 " cp=0x%{public}X hasText=%{public}d wide=%{public}d",
                    frameId,
                    row,
                    col,
                    static_cast<uint64_t>(raw),
                    codepoint,
                    hasText ? 1 : 0,
                    static_cast<int>(wide));
            }

            if (searchActive && !searchQuery.empty()) {
                const size_t logicalRow = viewportTopRow + static_cast<size_t>(row);
                const size_t cellStartByte = rowByteStart[static_cast<size_t>(col)];
                const size_t cellEndByte = rowByteEnd[static_cast<size_t>(col)];
                for (size_t matchIndex = 0; matchIndex < searchMatches.size(); ++matchIndex) {
                    const SearchMatch& match = searchMatches[matchIndex];
                    if (match.row < logicalRow) {
                        continue;
                    }
                    if (match.row > logicalRow) {
                        break;
                    }
                    if (!ByteRangesOverlap(cellStartByte, cellEndByte, match.startByte, match.endByte)) {
                        continue;
                    }

                    if (static_cast<int>(matchIndex) == selectedSearchIndex) {
                        dst.attrs.bg = kSearchCurrentMatchBackground;
                        dst.attrs.fg = kSearchCurrentMatchForeground;
                    } else {
                        dst.attrs.bg = kSearchMatchBackground;
                        dst.attrs.fg = kSearchMatchForeground;
                    }
                    break;
                }
            }

            if (selectionActive) {
                if (IsCellSelected(true, selectionStartRow, selectionStartCol, selectionEndRow, selectionEndCol, row, col)) {
                    dst.selected = true;
                    dst.attrs.bg = m_theme.selectionBackground;
                    dst.attrs.fg = m_theme.selectionForeground;
                }
            }
        }
    }

    if (suspiciousCells > 0) {
        OH_LOG_ERROR(LOG_APP, "drawFrame frame=%{public}" PRIu64 " suspiciousCells=%{public}d size=%{public}dx%{public}d",
            frameId, suspiciousCells, m_cols, m_rows);
    }

    m_renderer->setColors(m_theme.background, m_theme.foreground);
    m_renderer->setCursorColors(m_theme.cursorColor, m_theme.cursorText);
    m_renderer->beginFrame();
    m_renderer->renderGrid(cells, m_cols, m_rows, cursorRow, cursorCol, cursorVisible);
    m_renderer->endFrame();
}
