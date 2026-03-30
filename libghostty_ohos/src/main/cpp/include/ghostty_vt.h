#ifndef GHOSTTY_VT_H
#define GHOSTTY_VT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ghostty_result {
    GHOSTTY_SUCCESS = 0,
    GHOSTTY_OUT_OF_MEMORY = -1,
    GHOSTTY_INVALID_VALUE = -2,
    GHOSTTY_OUT_OF_SPACE = -3,
    GHOSTTY_NO_VALUE = -4,
} ghostty_result_t;

typedef struct ghostty_string {
    const uint8_t* ptr;
    size_t len;
} ghostty_string_t;

typedef struct ghostty_terminal_wrapper* ghostty_terminal_t;
typedef struct ghostty_render_state_wrapper* ghostty_render_state_t;
typedef struct ghostty_row_iterator_wrapper* ghostty_row_iterator_t;
typedef struct ghostty_row_cells_wrapper* ghostty_row_cells_t;

typedef struct ghostty_terminal_options {
    uint16_t cols;
    uint16_t rows;
    size_t max_scrollback;
} ghostty_terminal_options_t;

typedef enum ghostty_terminal_scroll_viewport_tag {
    GHOSTTY_SCROLL_VIEWPORT_TOP = 0,
    GHOSTTY_SCROLL_VIEWPORT_BOTTOM = 1,
    GHOSTTY_SCROLL_VIEWPORT_DELTA = 2,
} ghostty_terminal_scroll_viewport_tag_t;

typedef union ghostty_terminal_scroll_viewport_value {
    intptr_t delta;
    uint64_t _padding[2];
} ghostty_terminal_scroll_viewport_value_t;

typedef struct ghostty_terminal_scroll_viewport {
    ghostty_terminal_scroll_viewport_tag_t tag;
    ghostty_terminal_scroll_viewport_value_t value;
} ghostty_terminal_scroll_viewport_t;

typedef struct ghostty_terminal_scrollbar {
    uint64_t total;
    uint64_t offset;
    uint64_t len;
} ghostty_terminal_scrollbar_t;

typedef enum ghostty_terminal_screen {
    GHOSTTY_TERMINAL_SCREEN_PRIMARY = 0,
    GHOSTTY_TERMINAL_SCREEN_ALTERNATE = 1,
} ghostty_terminal_screen_t;

typedef enum ghostty_color_scheme {
    GHOSTTY_COLOR_SCHEME_LIGHT = 0,
    GHOSTTY_COLOR_SCHEME_DARK = 1,
} ghostty_color_scheme_t;

typedef enum ghostty_terminal_option {
    GHOSTTY_TERMINAL_OPT_USERDATA = 0,
    GHOSTTY_TERMINAL_OPT_WRITE_PTY = 1,
    GHOSTTY_TERMINAL_OPT_BELL = 2,
    GHOSTTY_TERMINAL_OPT_ENQUIRY = 3,
    GHOSTTY_TERMINAL_OPT_XTVERSION = 4,
    GHOSTTY_TERMINAL_OPT_TITLE_CHANGED = 5,
    GHOSTTY_TERMINAL_OPT_SIZE = 6,
    GHOSTTY_TERMINAL_OPT_COLOR_SCHEME = 7,
    GHOSTTY_TERMINAL_OPT_DEVICE_ATTRIBUTES = 8,
    GHOSTTY_TERMINAL_OPT_TITLE = 9,
    GHOSTTY_TERMINAL_OPT_PWD = 10,
    GHOSTTY_TERMINAL_OPT_COLOR_FOREGROUND = 11,
    GHOSTTY_TERMINAL_OPT_COLOR_BACKGROUND = 12,
    GHOSTTY_TERMINAL_OPT_COLOR_CURSOR = 13,
    GHOSTTY_TERMINAL_OPT_COLOR_PALETTE = 14,
} ghostty_terminal_option_t;

typedef enum ghostty_terminal_data {
    GHOSTTY_TERMINAL_DATA_INVALID = 0,
    GHOSTTY_TERMINAL_DATA_COLS = 1,
    GHOSTTY_TERMINAL_DATA_ROWS = 2,
    GHOSTTY_TERMINAL_DATA_CURSOR_X = 3,
    GHOSTTY_TERMINAL_DATA_CURSOR_Y = 4,
    GHOSTTY_TERMINAL_DATA_CURSOR_PENDING_WRAP = 5,
    GHOSTTY_TERMINAL_DATA_ACTIVE_SCREEN = 6,
    GHOSTTY_TERMINAL_DATA_CURSOR_VISIBLE = 7,
    GHOSTTY_TERMINAL_DATA_KITTY_KEYBOARD_FLAGS = 8,
    GHOSTTY_TERMINAL_DATA_SCROLLBAR = 9,
    GHOSTTY_TERMINAL_DATA_CURSOR_STYLE = 10,
    GHOSTTY_TERMINAL_DATA_MOUSE_TRACKING = 11,
    GHOSTTY_TERMINAL_DATA_TITLE = 12,
    GHOSTTY_TERMINAL_DATA_PWD = 13,
    GHOSTTY_TERMINAL_DATA_TOTAL_ROWS = 14,
    GHOSTTY_TERMINAL_DATA_SCROLLBACK_ROWS = 15,
    GHOSTTY_TERMINAL_DATA_WIDTH_PX = 16,
    GHOSTTY_TERMINAL_DATA_HEIGHT_PX = 17,
    GHOSTTY_TERMINAL_DATA_COLOR_FOREGROUND = 18,
    GHOSTTY_TERMINAL_DATA_COLOR_BACKGROUND = 19,
    GHOSTTY_TERMINAL_DATA_COLOR_CURSOR = 20,
    GHOSTTY_TERMINAL_DATA_COLOR_PALETTE = 21,
    GHOSTTY_TERMINAL_DATA_COLOR_FOREGROUND_DEFAULT = 22,
    GHOSTTY_TERMINAL_DATA_COLOR_BACKGROUND_DEFAULT = 23,
    GHOSTTY_TERMINAL_DATA_COLOR_CURSOR_DEFAULT = 24,
    GHOSTTY_TERMINAL_DATA_COLOR_PALETTE_DEFAULT = 25,
} ghostty_terminal_data_t;

typedef struct ghostty_rgb {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} ghostty_rgb_t;

typedef uint8_t ghostty_color_palette_index_t;

typedef enum ghostty_style_color_tag {
    GHOSTTY_STYLE_COLOR_NONE = 0,
    GHOSTTY_STYLE_COLOR_PALETTE = 1,
    GHOSTTY_STYLE_COLOR_RGB = 2,
} ghostty_style_color_tag_t;

typedef union ghostty_style_color_value {
    ghostty_color_palette_index_t palette;
    ghostty_rgb_t rgb;
    uint64_t _padding;
} ghostty_style_color_value_t;

typedef struct ghostty_style_color {
    ghostty_style_color_tag_t tag;
    ghostty_style_color_value_t value;
} ghostty_style_color_t;

typedef struct ghostty_style {
    size_t size;
    ghostty_style_color_t fg_color;
    ghostty_style_color_t bg_color;
    ghostty_style_color_t underline_color;
    bool bold;
    bool italic;
    bool faint;
    bool blink;
    bool inverse;
    bool invisible;
    bool strikethrough;
    bool overline;
    int underline;
} ghostty_style_t;

typedef uint64_t ghostty_cell_t;
typedef uint64_t ghostty_row_t;

typedef struct ghostty_grid_ref {
    size_t size;
    void* node;
    uint16_t x;
    uint16_t y;
} ghostty_grid_ref_t;

typedef struct ghostty_point_coordinate {
    uint16_t x;
    uint32_t y;
} ghostty_point_coordinate_t;

typedef enum ghostty_point_tag {
    GHOSTTY_POINT_TAG_ACTIVE = 0,
    GHOSTTY_POINT_TAG_VIEWPORT = 1,
    GHOSTTY_POINT_TAG_SCREEN = 2,
    GHOSTTY_POINT_TAG_HISTORY = 3,
} ghostty_point_tag_t;

typedef union ghostty_point_value {
    ghostty_point_coordinate_t coordinate;
    uint64_t _padding[2];
} ghostty_point_value_t;

typedef struct ghostty_point {
    ghostty_point_tag_t tag;
    ghostty_point_value_t value;
} ghostty_point_t;

typedef enum ghostty_cell_wide {
    GHOSTTY_CELL_WIDE_NARROW = 0,
    GHOSTTY_CELL_WIDE_WIDE = 1,
    GHOSTTY_CELL_WIDE_SPACER_TAIL = 2,
    GHOSTTY_CELL_WIDE_SPACER_HEAD = 3,
} ghostty_cell_wide_t;

typedef enum ghostty_cell_data {
    GHOSTTY_CELL_DATA_INVALID = 0,
    GHOSTTY_CELL_DATA_CODEPOINT = 1,
    GHOSTTY_CELL_DATA_CONTENT_TAG = 2,
    GHOSTTY_CELL_DATA_WIDE = 3,
    GHOSTTY_CELL_DATA_HAS_TEXT = 4,
    GHOSTTY_CELL_DATA_HAS_STYLING = 5,
} ghostty_cell_data_t;

#define GHOSTTY_INIT_SIZED(type) \
  ((type){ .size = sizeof(type) })

typedef enum ghostty_render_state_dirty {
    GHOSTTY_RENDER_STATE_DIRTY_FALSE = 0,
    GHOSTTY_RENDER_STATE_DIRTY_PARTIAL = 1,
    GHOSTTY_RENDER_STATE_DIRTY_FULL = 2,
} ghostty_render_state_dirty_t;

typedef enum ghostty_render_state_data {
    GHOSTTY_RENDER_STATE_DATA_INVALID = 0,
    GHOSTTY_RENDER_STATE_DATA_COLS = 1,
    GHOSTTY_RENDER_STATE_DATA_ROWS = 2,
    GHOSTTY_RENDER_STATE_DATA_DIRTY = 3,
    GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR = 4,
    GHOSTTY_RENDER_STATE_DATA_COLOR_BACKGROUND = 5,
    GHOSTTY_RENDER_STATE_DATA_COLOR_FOREGROUND = 6,
    GHOSTTY_RENDER_STATE_DATA_COLOR_CURSOR = 7,
    GHOSTTY_RENDER_STATE_DATA_COLOR_CURSOR_HAS_VALUE = 8,
    GHOSTTY_RENDER_STATE_DATA_COLOR_PALETTE = 9,
    GHOSTTY_RENDER_STATE_DATA_CURSOR_VISUAL_STYLE = 10,
    GHOSTTY_RENDER_STATE_DATA_CURSOR_VISIBLE = 11,
    GHOSTTY_RENDER_STATE_DATA_CURSOR_BLINKING = 12,
    GHOSTTY_RENDER_STATE_DATA_CURSOR_PASSWORD_INPUT = 13,
    GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_HAS_VALUE = 14,
    GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_X = 15,
    GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_Y = 16,
    GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_WIDE_TAIL = 17,
} ghostty_render_state_data_t;

typedef struct ghostty_render_state_colors {
    size_t size;
    ghostty_rgb_t background;
    ghostty_rgb_t foreground;
    ghostty_rgb_t cursor;
    bool cursor_has_value;
    ghostty_rgb_t palette[256];
} ghostty_render_state_colors_t;

typedef enum ghostty_render_state_row_data {
    GHOSTTY_RENDER_STATE_ROW_DATA_INVALID = 0,
    GHOSTTY_RENDER_STATE_ROW_DATA_DIRTY = 1,
    GHOSTTY_RENDER_STATE_ROW_DATA_RAW = 2,
    GHOSTTY_RENDER_STATE_ROW_DATA_CELLS = 3,
} ghostty_render_state_row_data_t;

typedef enum ghostty_render_state_row_cells_data {
    GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_INVALID = 0,
    GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_RAW = 1,
    GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_STYLE = 2,
    GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_LEN = 3,
    GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_BUF = 4,
    GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_BG_COLOR = 5,
    GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_FG_COLOR = 6,
} ghostty_render_state_row_cells_data_t;

typedef struct ghostty_size_report_size {
    uint16_t rows;
    uint16_t columns;
    uint32_t cell_width;
    uint32_t cell_height;
} ghostty_size_report_size_t;

typedef struct ghostty_device_attributes_primary {
    uint16_t conformance_level;
    uint16_t features[64];
    size_t num_features;
} ghostty_device_attributes_primary_t;

typedef struct ghostty_device_attributes_secondary {
    uint16_t device_type;
    uint16_t firmware_version;
    uint16_t rom_cartridge;
} ghostty_device_attributes_secondary_t;

typedef struct ghostty_device_attributes_tertiary {
    uint32_t unit_id;
} ghostty_device_attributes_tertiary_t;

typedef struct ghostty_device_attributes {
    ghostty_device_attributes_primary_t primary;
    ghostty_device_attributes_secondary_t secondary;
    ghostty_device_attributes_tertiary_t tertiary;
} ghostty_device_attributes_t;

#define GHOSTTY_DA_CONFORMANCE_VT220 62
#define GHOSTTY_DA_FEATURE_ANSI_COLOR 22
#define GHOSTTY_DA_FEATURE_CLIPBOARD 52
#define GHOSTTY_DA_DEVICE_TYPE_VT220 1

typedef void (*ghostty_terminal_write_pty_fn)(
    ghostty_terminal_t terminal,
    void* userdata,
    const uint8_t* data,
    size_t len);
typedef void (*ghostty_terminal_bell_fn)(ghostty_terminal_t terminal, void* userdata);
typedef ghostty_string_t (*ghostty_terminal_enquiry_fn)(ghostty_terminal_t terminal, void* userdata);
typedef ghostty_string_t (*ghostty_terminal_xtversion_fn)(ghostty_terminal_t terminal, void* userdata);
typedef void (*ghostty_terminal_title_changed_fn)(ghostty_terminal_t terminal, void* userdata);
typedef bool (*ghostty_terminal_size_fn)(
    ghostty_terminal_t terminal,
    void* userdata,
    ghostty_size_report_size_t* out_size);
typedef bool (*ghostty_terminal_color_scheme_fn)(
    ghostty_terminal_t terminal,
    void* userdata,
    ghostty_color_scheme_t* out_scheme);
typedef bool (*ghostty_terminal_device_attributes_fn)(
    ghostty_terminal_t terminal,
    void* userdata,
    ghostty_device_attributes_t* out_attrs);

ghostty_result_t ghostty_terminal_new(
    const void* alloc,
    ghostty_terminal_t* result,
    ghostty_terminal_options_t opts);
void ghostty_terminal_free(ghostty_terminal_t terminal);
void ghostty_terminal_reset(ghostty_terminal_t terminal);
ghostty_result_t ghostty_terminal_resize(
    ghostty_terminal_t terminal,
    uint16_t cols,
    uint16_t rows,
    uint32_t cell_width_px,
    uint32_t cell_height_px);
ghostty_result_t ghostty_terminal_set(
    ghostty_terminal_t terminal,
    ghostty_terminal_option_t option,
    const void* value);
void ghostty_terminal_vt_write(ghostty_terminal_t terminal, const uint8_t* ptr, size_t len);
void ghostty_terminal_scroll_viewport(
    ghostty_terminal_t terminal,
    ghostty_terminal_scroll_viewport_t behavior);
ghostty_result_t ghostty_terminal_get(
    ghostty_terminal_t terminal,
    ghostty_terminal_data_t data,
    void* out);
ghostty_result_t ghostty_terminal_grid_ref(
    ghostty_terminal_t terminal,
    ghostty_point_t point,
    ghostty_grid_ref_t* out_ref);
ghostty_result_t ghostty_terminal_mode_get(ghostty_terminal_t terminal, uint16_t mode, bool* out_value);
ghostty_result_t ghostty_terminal_mode_set(ghostty_terminal_t terminal, uint16_t mode, bool value);

ghostty_result_t ghostty_cell_get(ghostty_cell_t cell, ghostty_cell_data_t data, void* out);
ghostty_result_t ghostty_grid_ref_cell(const ghostty_grid_ref_t* ref, ghostty_cell_t* out_cell);
ghostty_result_t ghostty_grid_ref_row(const ghostty_grid_ref_t* ref, ghostty_row_t* out_row);

ghostty_result_t ghostty_render_state_new(const void* alloc, ghostty_render_state_t* result);
void ghostty_render_state_free(ghostty_render_state_t state);
ghostty_result_t ghostty_render_state_update(ghostty_render_state_t state, ghostty_terminal_t terminal);
ghostty_result_t ghostty_render_state_get(
    ghostty_render_state_t state,
    ghostty_render_state_data_t data,
    void* out);
ghostty_result_t ghostty_render_state_colors_get(
    ghostty_render_state_t state,
    ghostty_render_state_colors_t* out_colors);

ghostty_result_t ghostty_render_state_row_iterator_new(const void* alloc, ghostty_row_iterator_t* result);
void ghostty_render_state_row_iterator_free(ghostty_row_iterator_t iterator);
bool ghostty_render_state_row_iterator_next(ghostty_row_iterator_t iterator);
ghostty_result_t ghostty_render_state_row_get(
    ghostty_row_iterator_t iterator,
    ghostty_render_state_row_data_t data,
    void* out);

ghostty_result_t ghostty_render_state_row_cells_new(const void* alloc, ghostty_row_cells_t* result);
void ghostty_render_state_row_cells_free(ghostty_row_cells_t cells);
bool ghostty_render_state_row_cells_next(ghostty_row_cells_t cells);
ghostty_result_t ghostty_render_state_row_cells_select(ghostty_row_cells_t cells, uint16_t x);
ghostty_result_t ghostty_render_state_row_cells_get(
    ghostty_row_cells_t cells,
    ghostty_render_state_row_cells_data_t data,
    void* out);

#ifdef __cplusplus
}
#endif

#endif
