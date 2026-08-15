#include "dashboard_ui.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "board_wt32.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lvgl.h"
#include "nvs.h"

LV_FONT_DECLARE(app_font_14);
LV_FONT_DECLARE(app_font_18);

#define PAGE_COUNT 11
#define CONTENT_HEIGHT 264
#define MAX_THEME_TEXT_OBJECTS 180
#define CLOCK_CANVAS_WIDTH 420
#define CLOCK_CANVAS_HEIGHT 126

#define COLOR_BG_DARK 0x10161A
#define COLOR_BG_LIGHT 0xEDF2F4
#define COLOR_SURFACE_DARK 0x1B242A
#define COLOR_SURFACE_LIGHT 0xFFFFFF
#define COLOR_SURFACE_ALT_DARK 0x26323A
#define COLOR_SURFACE_ALT_LIGHT 0xDDE7EB
#define COLOR_TEXT_DARK 0xF3F6F8
#define COLOR_TEXT_LIGHT 0x172027
#define COLOR_MUTED_DARK 0x9CA8B3
#define COLOR_MUTED_LIGHT 0x56656F
#define COLOR_LINE_DARK 0x344149
#define COLOR_LINE_LIGHT 0xC9D5DA
#define COLOR_BLUE 0x4AB1FF
#define COLOR_GREEN 0x44D290
#define COLOR_ORANGE 0xFFAD42
#define COLOR_RED 0xFF5C66
#define COLOR_PURPLE 0xAA84FF

typedef struct {
    lv_obj_t *root;
    lv_obj_t *status_bar;
    lv_obj_t *status_title;
    lv_obj_t *status_time;
    lv_obj_t *tileview;
    lv_obj_t *page_bar;
    lv_obj_t *pages[PAGE_COUNT];
    lv_obj_t *dots[PAGE_COUNT];

    lv_obj_t *time_canvas;
    lv_color_t *time_buffer;
    lv_obj_t *market_price;
    lv_obj_t *market_change;
    lv_obj_t *market_chart;
    lv_chart_series_t *market_series;
    lv_obj_t *weather_time;
    lv_obj_t *weather_temp;
    lv_obj_t *weather_humidity;
    lv_obj_t *pve_cpu;
    lv_obj_t *pve_memory;
    lv_obj_t *pve_storage;
    lv_obj_t *pve_cpu_bar;
    lv_obj_t *pve_memory_bar;
    lv_obj_t *pve_storage_bar;
    lv_obj_t *nas_temperature[4];
    lv_obj_t *quota_arc;
    lv_obj_t *quota_value;
    lv_obj_t *alert_counts;
    lv_obj_t *brightness_value;
    lv_obj_t *brightness_slider;
    lv_obj_t *theme_switch;
    lv_obj_t *theme_value;
    lv_obj_t *weather_page;
    lv_obj_t *home_page;
    lv_obj_t *room_tabs;
    lv_obj_t *home_toggle_buttons[2];
    lv_obj_t *home_toggle_labels[2];
    size_t home_toggle_count;

    lv_obj_t *photo_canvas;
    lv_color_t *photo_buffer;
    lv_obj_t *photo_title_shadow;
    lv_obj_t *photo_title;
    lv_obj_t *photo_counter;
    int photo_index;

    lv_obj_t *main_text_objects[MAX_THEME_TEXT_OBJECTS];
    size_t main_text_count;
    lv_obj_t *muted_text_objects[MAX_THEME_TEXT_OBJECTS];
    size_t muted_text_count;
    uint8_t saved_brightness;
    bool light_theme;
    bool scroll_in_progress;
    app_snapshot_t applied_snapshot;
    app_snapshot_t deferred_snapshot;
    bool has_applied_snapshot;
    bool has_deferred_snapshot;
} dashboard_context_t;

static const char *TAG = "dashboard_ui";
static dashboard_context_t s_ui;
static lv_style_t s_style_root;
static lv_style_t s_style_status;
static lv_style_t s_style_page;
static lv_style_t s_style_card;
static lv_style_t s_style_card_alt;
static lv_style_t s_style_button;

static uint32_t update_navigation(void);
static void draw_digital_clock(uint8_t hour, uint8_t minute);

static lv_color_t color(uint32_t value)
{
    return lv_color_hex(value);
}

static void track_main_text(lv_obj_t *object)
{
    if (s_ui.main_text_count < MAX_THEME_TEXT_OBJECTS) {
        s_ui.main_text_objects[s_ui.main_text_count++] = object;
    }
}

static void track_muted_text(lv_obj_t *object)
{
    if (s_ui.muted_text_count < MAX_THEME_TEXT_OBJECTS) {
        s_ui.muted_text_objects[s_ui.muted_text_count++] = object;
    }
}

static lv_obj_t *make_label_internal(lv_obj_t *parent, const char *text, int x, int y,
                                     int width, const lv_font_t *font, bool muted,
                                     bool track_for_theme)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_font(label, font != NULL ? font : &app_font_14, 0);
    const uint32_t text_color = muted
                                    ? (s_ui.light_theme ? COLOR_MUTED_LIGHT : COLOR_MUTED_DARK)
                                    : (s_ui.light_theme ? COLOR_TEXT_LIGHT : COLOR_TEXT_DARK);
    lv_obj_set_style_text_color(label, color(text_color), 0);
    if (track_for_theme) {
        if (muted) {
            track_muted_text(label);
        } else {
            track_main_text(label);
        }
    }
    return label;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, int x, int y,
                            int width, const lv_font_t *font, bool muted)
{
    return make_label_internal(parent, text, x, y, width, font, muted, true);
}

static lv_obj_t *make_accent_label(lv_obj_t *parent, const char *text, int x, int y,
                                   int width, const lv_font_t *font, uint32_t accent)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_font(label, font != NULL ? font : &app_font_14, 0);
    lv_obj_set_style_text_color(label, color(accent), 0);
    return label;
}

static lv_obj_t *make_card(lv_obj_t *parent, int x, int y, int width, int height)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_add_style(card, &s_style_card, 0);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, width, height);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static lv_obj_t *make_button_internal(lv_obj_t *parent, const char *text, int x, int y,
                                      int width, int height, bool track_label_for_theme)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_add_style(button, &s_style_button, LV_PART_MAIN);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, width, height);
    lv_obj_t *label = make_label_internal(button, text, 0, 0, width, &app_font_14,
                                          false, track_label_for_theme);
    lv_obj_center(label);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    return button;
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, int x, int y,
                             int width, int height)
{
    return make_button_internal(parent, text, x, y, width, height, true);
}

static lv_obj_t *make_bar(lv_obj_t *parent, int x, int y, int width, int value,
                          uint32_t indicator_color)
{
    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_pos(bar, x, y);
    lv_obj_set_size(bar, width, 6);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, value, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, color(COLOR_SURFACE_ALT_DARK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, color(indicator_color), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 3, LV_PART_INDICATOR);
    return bar;
}

static void close_modal_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        lv_obj_t *overlay = lv_event_get_user_data(event);
        lv_obj_del_async(overlay);
    }
}

static void show_modal(const char *title_text, const char *body_text)
{
    lv_obj_t *overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, WT32_LCD_WIDTH, WT32_LCD_HEIGHT);
    lv_obj_set_style_bg_color(overlay, color(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_60, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *dialog = make_card(overlay, 60, 62, 360, 196);
    lv_obj_set_style_border_width(dialog, 1, 0);
    lv_obj_set_style_border_color(dialog, color(COLOR_LINE_DARK), 0);
    lv_obj_t *title = make_label_internal(dialog, title_text, 18, 16, 324,
                                          &app_font_18, false, false);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *body = make_label_internal(dialog, body_text, 20, 58, 320,
                                         &app_font_14, true, false);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_height(body, 76);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *close = make_button_internal(dialog, "关闭", 125, 143, 110, 38, false);
    lv_obj_add_event_cb(close, close_modal_event, LV_EVENT_CLICKED, overlay);
}

static void detail_button_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    const char *const *detail = lv_event_get_user_data(event);
    show_modal(detail[0], detail[1]);
}

static void init_styles(void)
{
    lv_style_init(&s_style_root);
    lv_style_set_bg_color(&s_style_root, color(COLOR_BG_DARK));
    lv_style_set_bg_opa(&s_style_root, LV_OPA_COVER);
    lv_style_set_text_font(&s_style_root, &app_font_14);
    lv_style_set_text_color(&s_style_root, color(COLOR_TEXT_DARK));

    lv_style_init(&s_style_status);
    lv_style_set_bg_color(&s_style_status, color(COLOR_BG_DARK));
    lv_style_set_bg_opa(&s_style_status, LV_OPA_COVER);
    lv_style_set_border_color(&s_style_status, color(COLOR_LINE_DARK));
    lv_style_set_border_side(&s_style_status, LV_BORDER_SIDE_BOTTOM);
    lv_style_set_border_width(&s_style_status, 1);

    lv_style_init(&s_style_page);
    lv_style_set_bg_color(&s_style_page, color(COLOR_BG_DARK));
    lv_style_set_bg_opa(&s_style_page, LV_OPA_COVER);
    lv_style_set_border_width(&s_style_page, 0);
    lv_style_set_radius(&s_style_page, 0);
    lv_style_set_pad_all(&s_style_page, 0);

    lv_style_init(&s_style_card);
    lv_style_set_bg_color(&s_style_card, color(COLOR_SURFACE_DARK));
    lv_style_set_bg_opa(&s_style_card, LV_OPA_COVER);
    lv_style_set_border_width(&s_style_card, 0);
    lv_style_set_radius(&s_style_card, 6);
    lv_style_set_pad_all(&s_style_card, 0);

    lv_style_init(&s_style_card_alt);
    lv_style_set_bg_color(&s_style_card_alt, color(COLOR_SURFACE_ALT_DARK));
    lv_style_set_bg_opa(&s_style_card_alt, LV_OPA_COVER);
    lv_style_set_border_width(&s_style_card_alt, 0);
    lv_style_set_radius(&s_style_card_alt, 5);

    lv_style_init(&s_style_button);
    lv_style_set_bg_color(&s_style_button, color(COLOR_SURFACE_ALT_DARK));
    lv_style_set_bg_opa(&s_style_button, LV_OPA_COVER);
    lv_style_set_border_width(&s_style_button, 0);
    lv_style_set_radius(&s_style_button, 5);
    lv_style_set_pad_all(&s_style_button, 0);
}

static void apply_theme(bool light)
{
    s_ui.light_theme = light;
    const uint32_t background = light ? COLOR_BG_LIGHT : COLOR_BG_DARK;
    const uint32_t surface = light ? COLOR_SURFACE_LIGHT : COLOR_SURFACE_DARK;
    const uint32_t surface_alt = light ? COLOR_SURFACE_ALT_LIGHT : COLOR_SURFACE_ALT_DARK;
    const uint32_t text = light ? COLOR_TEXT_LIGHT : COLOR_TEXT_DARK;
    const uint32_t muted = light ? COLOR_MUTED_LIGHT : COLOR_MUTED_DARK;
    const uint32_t line = light ? COLOR_LINE_LIGHT : COLOR_LINE_DARK;

    lv_style_set_bg_color(&s_style_root, color(background));
    lv_style_set_text_color(&s_style_root, color(text));
    lv_style_set_bg_color(&s_style_status, color(background));
    lv_style_set_border_color(&s_style_status, color(line));
    lv_style_set_bg_color(&s_style_page, color(background));
    lv_style_set_bg_color(&s_style_card, color(surface));
    lv_style_set_bg_color(&s_style_card_alt, color(surface_alt));
    lv_style_set_bg_color(&s_style_button, color(surface_alt));
    lv_obj_report_style_change(&s_style_root);
    lv_obj_report_style_change(&s_style_status);
    lv_obj_report_style_change(&s_style_page);
    lv_obj_report_style_change(&s_style_card);
    lv_obj_report_style_change(&s_style_card_alt);
    lv_obj_report_style_change(&s_style_button);

    for (size_t i = 0; i < s_ui.main_text_count; ++i) {
        lv_obj_set_style_text_color(s_ui.main_text_objects[i], color(text), 0);
    }
    for (size_t i = 0; i < s_ui.muted_text_count; ++i) {
        lv_obj_set_style_text_color(s_ui.muted_text_objects[i], color(muted), 0);
    }
    if (s_ui.weather_page != NULL) {
        lv_obj_set_style_bg_color(s_ui.weather_page,
                                  color(light ? 0xDCEFF8 : 0x102B39), 0);
    }
    if (s_ui.home_page != NULL) {
        lv_obj_set_style_bg_color(s_ui.home_page,
                                  color(light ? 0xF7E8DF : 0x211611), 0);
    }
    if (s_ui.room_tabs != NULL) {
        for (uint32_t i = 0; i < lv_obj_get_child_cnt(s_ui.room_tabs); ++i) {
            lv_obj_set_style_bg_color(lv_obj_get_child(s_ui.room_tabs, i),
                                      color(surface_alt), LV_STATE_CHECKED);
        }
    }
    for (size_t i = 0; i < s_ui.home_toggle_count; ++i) {
        const bool enabled = lv_obj_has_state(s_ui.home_toggle_buttons[i], LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(s_ui.home_toggle_buttons[i],
                                  color(enabled ? 0x5B4023 : surface), 0);
        lv_obj_set_style_text_color(s_ui.home_toggle_labels[i],
                                    color(enabled ? COLOR_ORANGE : muted), 0);
    }
    if (s_ui.brightness_slider != NULL) {
        lv_obj_set_style_bg_color(s_ui.brightness_slider, color(surface_alt), LV_PART_MAIN);
    }
    if (s_ui.theme_value != NULL) {
        lv_label_set_text(s_ui.theme_value, light ? "浅色" : "深色");
    }
    if (s_ui.page_bar != NULL) {
        update_navigation();
    }
}

static lv_obj_t *add_tile(int column)
{
    lv_dir_t direction = LV_DIR_HOR;
    lv_obj_t *tile = lv_tileview_add_tile(s_ui.tileview, column, 0, direction);

    /* Removing all styles also removes the position and size assigned by
     * lv_tileview_add_tile(). Restore both before adding page content. */
    lv_obj_remove_style_all(tile);
    lv_obj_add_style(tile, &s_style_page, 0);
    lv_obj_set_pos(tile, column * WT32_LCD_WIDTH, 0);
    lv_obj_set_size(tile, WT32_LCD_WIDTH, CONTENT_HEIGHT);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    s_ui.pages[column] = tile;
    return tile;
}

enum {
    SEGMENT_A = 1U << 0,
    SEGMENT_B = 1U << 1,
    SEGMENT_C = 1U << 2,
    SEGMENT_D = 1U << 3,
    SEGMENT_E = 1U << 4,
    SEGMENT_F = 1U << 5,
    SEGMENT_G = 1U << 6,
};

static const uint8_t digit_segments[10] = {
    SEGMENT_A | SEGMENT_B | SEGMENT_C | SEGMENT_D | SEGMENT_E | SEGMENT_F,
    SEGMENT_B | SEGMENT_C,
    SEGMENT_A | SEGMENT_B | SEGMENT_D | SEGMENT_E | SEGMENT_G,
    SEGMENT_A | SEGMENT_B | SEGMENT_C | SEGMENT_D | SEGMENT_G,
    SEGMENT_B | SEGMENT_C | SEGMENT_F | SEGMENT_G,
    SEGMENT_A | SEGMENT_C | SEGMENT_D | SEGMENT_F | SEGMENT_G,
    SEGMENT_A | SEGMENT_C | SEGMENT_D | SEGMENT_E | SEGMENT_F | SEGMENT_G,
    SEGMENT_A | SEGMENT_B | SEGMENT_C,
    SEGMENT_A | SEGMENT_B | SEGMENT_C | SEGMENT_D | SEGMENT_E | SEGMENT_F | SEGMENT_G,
    SEGMENT_A | SEGMENT_B | SEGMENT_C | SEGMENT_D | SEGMENT_F | SEGMENT_G,
};

static void draw_horizontal_segment(int x, int y, lv_color_t segment_color)
{
    lv_draw_rect_dsc_t style;
    lv_draw_rect_dsc_init(&style);
    style.bg_opa = LV_OPA_COVER;
    style.bg_color = segment_color;
    lv_point_t points[] = {
        {x, y + 6}, {x + 6, y}, {x + 54, y},
        {x + 60, y + 6}, {x + 54, y + 12}, {x + 6, y + 12},
    };
    lv_canvas_draw_polygon(s_ui.time_canvas, points, 6, &style);
}

static void draw_vertical_segment(int x, int y, lv_color_t segment_color)
{
    lv_draw_rect_dsc_t style;
    lv_draw_rect_dsc_init(&style);
    style.bg_opa = LV_OPA_COVER;
    style.bg_color = segment_color;
    lv_point_t points[] = {
        {x + 6, y}, {x + 12, y + 6}, {x + 12, y + 33},
        {x + 6, y + 39}, {x, y + 33}, {x, y + 6},
    };
    lv_canvas_draw_polygon(s_ui.time_canvas, points, 6, &style);
}

static void draw_segment(int x, int y, uint8_t segment, lv_color_t segment_color)
{
    switch (segment) {
        case SEGMENT_A: draw_horizontal_segment(x + 6, y, segment_color); break;
        case SEGMENT_B: draw_vertical_segment(x + 60, y + 6, segment_color); break;
        case SEGMENT_C: draw_vertical_segment(x + 60, y + 51, segment_color); break;
        case SEGMENT_D: draw_horizontal_segment(x + 6, y + 90, segment_color); break;
        case SEGMENT_E: draw_vertical_segment(x, y + 51, segment_color); break;
        case SEGMENT_F: draw_vertical_segment(x, y + 6, segment_color); break;
        case SEGMENT_G: draw_horizontal_segment(x + 6, y + 45, segment_color); break;
        default: break;
    }
}

static void draw_digit(int x, int y, uint8_t digit)
{
    static const uint8_t segments[] = {
        SEGMENT_A, SEGMENT_B, SEGMENT_C, SEGMENT_D,
        SEGMENT_E, SEGMENT_F, SEGMENT_G,
    };
    const lv_color_t inactive = color(0x95A492);
    const lv_color_t active = color(0x18231D);
    const uint8_t mask = digit_segments[digit % 10];

    for (size_t i = 0; i < sizeof(segments); ++i) {
        draw_segment(x, y, segments[i], inactive);
    }
    for (size_t i = 0; i < sizeof(segments); ++i) {
        if ((mask & segments[i]) != 0) {
            draw_segment(x, y, segments[i], active);
        }
    }
}

static void draw_digital_clock(uint8_t hour, uint8_t minute)
{
    if (s_ui.time_canvas == NULL || s_ui.time_buffer == NULL) return;

    const lv_color_t lcd_background = color(0xB4C1AF);
    const lv_color_t lcd_ink = color(0x18231D);
    lv_canvas_fill_bg(s_ui.time_canvas, lcd_background, LV_OPA_COVER);

    lv_draw_rect_dsc_t panel;
    lv_draw_rect_dsc_init(&panel);
    panel.bg_opa = LV_OPA_COVER;
    panel.bg_color = lcd_background;
    panel.border_opa = LV_OPA_COVER;
    panel.border_color = color(0x566359);
    panel.border_width = 2;
    panel.radius = 5;
    lv_canvas_draw_rect(s_ui.time_canvas, 0, 0,
                        CLOCK_CANVAS_WIDTH, CLOCK_CANVAS_HEIGHT, &panel);

    const uint8_t digits[] = {hour / 10, hour % 10, minute / 10, minute % 10};
    const int positions[] = {24, 106, 242, 324};
    for (size_t i = 0; i < 4; ++i) {
        draw_digit(positions[i], 12, digits[i]);
    }

    lv_draw_rect_dsc_t colon;
    lv_draw_rect_dsc_init(&colon);
    colon.bg_opa = LV_OPA_COVER;
    colon.bg_color = lcd_ink;
    colon.radius = 3;
    lv_canvas_draw_rect(s_ui.time_canvas, 207, 39, 12, 12, &colon);
    lv_canvas_draw_rect(s_ui.time_canvas, 207, 75, 12, 12, &colon);
}

static void time_canvas_delete_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_DELETE || s_ui.time_buffer == NULL) return;
    heap_caps_free(s_ui.time_buffer);
    s_ui.time_buffer = NULL;
    s_ui.time_canvas = NULL;
}

static void create_time_page(lv_obj_t *page)
{
    const size_t buffer_size = LV_CANVAS_BUF_SIZE_TRUE_COLOR(
        CLOCK_CANVAS_WIDTH, CLOCK_CANVAS_HEIGHT);
    s_ui.time_buffer = heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_ui.time_buffer != NULL) {
        s_ui.time_canvas = lv_canvas_create(page);
        lv_obj_add_event_cb(s_ui.time_canvas, time_canvas_delete_event,
                            LV_EVENT_DELETE, NULL);
        lv_canvas_set_buffer(s_ui.time_canvas, s_ui.time_buffer,
                             CLOCK_CANVAS_WIDTH, CLOCK_CANVAS_HEIGHT,
                             LV_IMG_CF_TRUE_COLOR);
        lv_obj_set_pos(s_ui.time_canvas, 30, 8);
        draw_digital_clock(22, 18);
    } else {
        lv_obj_t *fallback = make_label(page, "22:18", 50, 30, 380,
                                        &lv_font_montserrat_48, false);
        lv_obj_set_style_text_align(fallback, LV_TEXT_ALIGN_CENTER, 0);
    }

    lv_obj_t *date = make_label(page, "8月14日 · 星期五", 70, 143, 340,
                                &app_font_18, true);
    lv_obj_set_style_text_align(date, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *footer = make_card(page, 30, 177, 420, 54);
    lv_obj_t *lunar = make_label(footer, "农历七月初二", 12, 10, 130, &app_font_14, true);
    lv_obj_set_style_text_align(lunar, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *sunrise = make_accent_label(footer, "日出 05:58", 145, 10, 125,
                                          &app_font_14, COLOR_ORANGE);
    lv_obj_set_style_text_align(sunrise, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *solar = make_label(footer, "处暑 8天后", 275, 10, 130, &app_font_14, true);
    lv_obj_set_style_text_align(solar, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *mock = make_accent_label(page, "LOCAL MOCK", 358, 238, 100,
                                       &lv_font_montserrat_14, COLOR_BLUE);
    lv_obj_set_style_text_align(mock, LV_TEXT_ALIGN_RIGHT, 0);
}

static void create_market_page(lv_obj_t *page)
{
    make_label(page, "比亚迪 · 002594", 18, 14, 200, &app_font_14, true);
    s_ui.market_price = make_label(page, "108.62", 18, 42, 195,
                                   &lv_font_montserrat_48, false);
    s_ui.market_change = make_accent_label(page, "+2.34  +2.20%", 20, 96, 190,
                                           &lv_font_montserrat_20, COLOR_RED);

    s_ui.market_chart = lv_chart_create(page);
    lv_obj_set_pos(s_ui.market_chart, 235, 18);
    lv_obj_set_size(s_ui.market_chart, 225, 105);
    lv_obj_set_style_bg_opa(s_ui.market_chart, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui.market_chart, 0, LV_PART_MAIN);
    lv_obj_set_style_line_color(s_ui.market_chart, color(COLOR_LINE_DARK), LV_PART_MAIN);
    lv_chart_set_type(s_ui.market_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_ui.market_chart, 10);
    lv_chart_set_range(s_ui.market_chart, LV_CHART_AXIS_PRIMARY_Y, 103, 112);
    lv_chart_set_div_line_count(s_ui.market_chart, 3, 0);
    s_ui.market_series = lv_chart_add_series(s_ui.market_chart, color(COLOR_RED),
                                              LV_CHART_AXIS_PRIMARY_Y);
    const int values[10] = {105, 106, 105, 108, 107, 109, 108, 110, 109, 111};
    for (int i = 0; i < 10; ++i) {
        lv_chart_set_next_value(s_ui.market_chart, s_ui.market_series, values[i]);
    }

    const char *fuel_names[] = {"92#", "95#", "98#"};
    const char *fuel_prices[] = {"7.34", "7.85", "8.99"};
    for (int i = 0; i < 3; ++i) {
        lv_obj_t *card = make_card(page, 18 + i * 150, 139, 140, 74);
        make_accent_label(card, fuel_names[i], 10, 8, 45, &lv_font_montserrat_14,
                          COLOR_ORANGE);
        lv_obj_t *price = make_label(card, fuel_prices[i], 52, 10, 78,
                                     &lv_font_montserrat_20, false);
        lv_obj_set_style_text_align(price, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_t *unit = make_label(card, "元/升", 10, 46, 120, &app_font_14, true);
        lv_obj_set_style_text_align(unit, LV_TEXT_ALIGN_RIGHT, 0);
    }
    lv_obj_t *updated = make_label(page, "深圳 · 行情 5秒前 · 油价 今日", 18, 230, 444,
                                   &app_font_14, true);
    lv_obj_set_style_text_align(updated, LV_TEXT_ALIGN_RIGHT, 0);
}

static void calendar_nav_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    intptr_t direction = (intptr_t)lv_event_get_user_data(event);
    show_modal(direction < 0 ? "上一月" : "下一月",
               "月历切换交互已启用。Gateway 接入后将同步对应年份的节假日与调休表。");
}

static void create_calendar_page(lv_obj_t *page)
{
    lv_obj_t *previous = make_button(page, "<", 14, 8, 40, 36);
    lv_obj_add_event_cb(previous, calendar_nav_event, LV_EVENT_CLICKED,
                        (void *)(intptr_t)-1);
    lv_obj_t *month = make_label(page, "2026年 8月", 120, 11, 240, &app_font_18, false);
    lv_obj_set_style_text_align(month, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *next = make_button(page, ">", 426, 8, 40, 36);
    lv_obj_add_event_cb(next, calendar_nav_event, LV_EVENT_CLICKED,
                        (void *)(intptr_t)1);
    make_label(page, "丙午年 · 甲申月", 120, 35, 240, &app_font_14, true);

    const char *weekdays[] = {"一", "二", "三", "四", "五", "六", "日"};
    for (int column = 0; column < 7; ++column) {
        lv_obj_t *label = make_label(page, weekdays[column], 15 + column * 65, 59,
                                     60, &app_font_14, true);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    }

    const int days[42] = {
        27, 28, 29, 30, 31, 1, 2, 3, 4, 5, 6, 7, 8, 9,
        10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
        24, 25, 26, 27, 28, 29, 30, 31, 1, 2, 3, 4, 5, 6,
    };
    char value[6];
    for (int index = 0; index < 42; ++index) {
        snprintf(value, sizeof(value), "%d", days[index]);
        const int row = index / 7;
        const int column = index % 7;
        lv_obj_t *day = make_label(page, value, 18 + column * 65, 83 + row * 27,
                                   54, &lv_font_montserrat_14,
                                   index < 5 || index > 35);
        lv_obj_set_style_text_align(day, LV_TEXT_ALIGN_CENTER, 0);
        if (index == 18) {
            lv_obj_set_style_bg_color(day, color(COLOR_BLUE), 0);
            lv_obj_set_style_bg_opa(day, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(day, 4, 0);
            lv_obj_set_style_text_color(day, color(0xFFFFFF), 0);
        }
    }
    make_accent_label(page, "14 初二", 365, 236, 94, &app_font_14, COLOR_BLUE);
    make_label(page, "本月无法定节假日", 18, 236, 220, &app_font_14, true);
}

static lv_obj_t *create_weather_icon(lv_obj_t *page)
{
    lv_obj_t *icon = lv_obj_create(page);
    lv_obj_remove_style_all(icon);
    lv_obj_set_pos(icon, 22, 19);
    lv_obj_set_size(icon, 105, 105);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE);

    const int circles[3][4] = {{13, 35, 48, 48}, {40, 18, 58, 58}, {65, 39, 34, 34}};
    for (int i = 0; i < 3; ++i) {
        lv_obj_t *part = lv_obj_create(icon);
        lv_obj_remove_style_all(part);
        lv_obj_set_pos(part, circles[i][0], circles[i][1]);
        lv_obj_set_size(part, circles[i][2], circles[i][3]);
        lv_obj_set_style_bg_color(part, color(COLOR_BLUE), 0);
        lv_obj_set_style_bg_opa(part, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(part, LV_RADIUS_CIRCLE, 0);
    }
    for (int i = 0; i < 3; ++i) {
        lv_obj_t *rain = lv_obj_create(icon);
        lv_obj_remove_style_all(rain);
        lv_obj_set_pos(rain, 29 + i * 22, 76);
        lv_obj_set_size(rain, 5, 21);
        lv_obj_set_style_bg_color(rain, color(COLOR_BLUE), 0);
        lv_obj_set_style_bg_opa(rain, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(rain, 3, 0);
    }
    return icon;
}

static void weather_day_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    const char *const *detail = lv_event_get_user_data(event);
    show_modal(detail[0], detail[1]);
}

static void create_weather_page(lv_obj_t *page)
{
    s_ui.weather_page = page;
    lv_obj_set_style_bg_color(page, color(0x102B39), 0);
    create_weather_icon(page);
    make_accent_label(page, "大雨", 27, 120, 96, &app_font_14, COLOR_BLUE);
    s_ui.weather_time = make_label(page, "22:18", 139, 28, 165,
                                   &lv_font_montserrat_48, false);
    s_ui.weather_temp = make_label(page, "29° · 深圳", 310, 32, 154,
                                   &app_font_18, false);
    make_label(page, "体感 32°", 310, 65, 150, &app_font_14, true);
    make_label(page, "最高 31° / 最低 26°", 310, 91, 158, &app_font_14, true);

    const char *metric_names[] = {"湿度", "东南风", "降雨", "空气"};
    const char *metric_values[] = {"80%", "3级", "90%", "优 28"};
    for (int i = 0; i < 4; ++i) {
        lv_obj_t *card = make_card(page, 13 + i * 117, 144, 108, 48);
        lv_obj_t *value = make_accent_label(card, metric_values[i], 6, 5, 96,
                                            &lv_font_montserrat_14, COLOR_BLUE);
        lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_t *name = make_label(card, metric_names[i], 6, 27, 96,
                                    &app_font_14, true);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
        if (i == 0) s_ui.weather_humidity = value;
    }

    static const char *today_detail[] = {"今天", "大雨，31/26°C，降雨概率 90%，东南风 3级。"};
    static const char *tomorrow_detail[] = {"明天", "多云间阵雨，32/27°C，降雨概率 45%。"};
    static const char *sunday_detail[] = {"周日", "晴到多云，33/27°C，适合短时户外活动。"};
    const char *days[] = {"今天 31/26°", "明天 32/27°", "周日 33/27°"};
    const char *const *details[] = {today_detail, tomorrow_detail, sunday_detail};
    for (int i = 0; i < 3; ++i) {
        lv_obj_t *button = make_button(page, days[i], 16 + i * 151, 208, 145, 42);
        lv_obj_add_event_cb(button, weather_day_event, LV_EVENT_CLICKED, (void *)details[i]);
    }
}

static void create_pve_page(lv_obj_t *page)
{
    make_accent_label(page, "•", 16, 12, 24, &lv_font_montserrat_14, COLOR_GREEN);
    make_label(page, "pve-01", 40, 10, 100, &lv_font_montserrat_20, false);
    make_label(page, "已运行 26天", 143, 14, 110, &app_font_14, true);

    lv_obj_t *metrics = make_card(page, 270, 8, 192, 54);
    make_label(metrics, "CPU", 8, 7, 42, &lv_font_montserrat_14, true);
    s_ui.pve_cpu = make_label(metrics, "24%", 48, 7, 45,
                              &lv_font_montserrat_14, false);
    s_ui.pve_cpu_bar = make_bar(metrics, 8, 35, 78, 24, COLOR_BLUE);
    make_label(metrics, "RAM", 100, 7, 45, &lv_font_montserrat_14, true);
    s_ui.pve_memory = make_label(metrics, "61%", 142, 7, 44,
                                 &lv_font_montserrat_14, false);
    s_ui.pve_memory_bar = make_bar(metrics, 100, 35, 82, 61, COLOR_PURPLE);

    static const char *ha_detail[] = {"Home Assistant", "VM 101 · CPU 8% · RAM 3.2 GB · 已运行 18天"};
    static const char *docker_detail[] = {"Docker", "VM 102 · CPU 31% · RAM 6.8 GB · 已运行 9天"};
    static const char *windows_detail[] = {"Windows 11", "VM 110 · 磁盘 96 GB · 当前已关机"};
    const char *names[] = {"101  Home Assistant", "102  Docker", "110  Windows 11"};
    const char *status[] = {"CPU 8% · RAM 3.2G", "CPU 31% · RAM 6.8G", "已关机 · 磁盘 96G"};
    const char *const *details[] = {ha_detail, docker_detail, windows_detail};
    for (int i = 0; i < 3; ++i) {
        lv_obj_t *row = make_card(page, 16, 74 + i * 50, 446, 44);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, detail_button_event, LV_EVENT_CLICKED, (void *)details[i]);
        make_label(row, names[i], 12, 4, 235, &app_font_14, false);
        lv_obj_t *sub = make_label(row, status[i], 250, 5, 180, &app_font_14, true);
        lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_RIGHT, 0);
        make_accent_label(row, i == 2 ? "停止" : "运行", 362, 24, 68,
                          &app_font_14, i == 2 ? COLOR_MUTED_DARK : COLOR_GREEN);
    }
    make_label(page, "local-zfs", 18, 230, 100, &lv_font_montserrat_14, true);
    s_ui.pve_storage = make_label(page, "72% · 1.42 / 1.96 TB", 235, 230, 225,
                                  &lv_font_montserrat_14, false);
    lv_obj_set_style_text_align(s_ui.pve_storage, LV_TEXT_ALIGN_RIGHT, 0);
    s_ui.pve_storage_bar = make_bar(page, 18, 253, 444, 72, COLOR_GREEN);
}

static void create_nas_page(lv_obj_t *page)
{
    make_accent_label(page, "•", 16, 13, 24, &lv_font_montserrat_14, COLOR_GREEN);
    make_label(page, "DS923+", 40, 10, 120, &lv_font_montserrat_20, false);
    make_label(page, "存储池 1 · 正常", 163, 14, 150, &app_font_14, true);
    lv_obj_t *capacity = make_label(page, "12.8 TB", 324, 8, 138,
                                    &lv_font_montserrat_20, false);
    lv_obj_set_style_text_align(capacity, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_t *available = make_label(page, "可用 5.6 TB", 326, 35, 136,
                                     &app_font_14, true);
    lv_obj_set_style_text_align(available, LV_TEXT_ALIGN_RIGHT, 0);

    static const char *disk_details[][2] = {
        {"硬盘 1", "8 TB · SMART 正常 · 通电 11246 小时"},
        {"硬盘 2", "8 TB · SMART 正常 · 通电 11239 小时"},
        {"硬盘 3", "8 TB · 温度监控演示 · 通电 10982 小时"},
        {"硬盘 4", "8 TB · SMART 正常 · 通电 10978 小时"},
    };
    for (int i = 0; i < 4; ++i) {
        lv_obj_t *card = make_card(page, 14 + i * 116, 76, 106, 128);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, detail_button_event, LV_EVENT_CLICKED,
                            (void *)disk_details[i]);
        char disk_name[16];
        snprintf(disk_name, sizeof(disk_name), "硬盘 %d", i + 1);
        lv_obj_t *name = make_label(card, disk_name, 6, 9, 94, &app_font_14, true);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
        s_ui.nas_temperature[i] = make_label(card, "36°C", 5, 44, 96,
                                             &lv_font_montserrat_20, false);
        lv_obj_set_style_text_align(s_ui.nas_temperature[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_t *health = make_accent_label(card, "8 TB · 正常", 5, 88, 96,
                                             &app_font_14, COLOR_GREEN);
        lv_obj_set_style_text_align(health, LV_TEXT_ALIGN_CENTER, 0);
    }
    make_bar(page, 18, 224, 444, 64, COLOR_BLUE);
    make_label(page, "已使用 64%", 18, 238, 120, &app_font_14, true);
}

static void create_quota_page(lv_obj_t *page)
{
    s_ui.quota_arc = lv_arc_create(page);
    lv_obj_set_pos(s_ui.quota_arc, 28, 38);
    lv_obj_set_size(s_ui.quota_arc, 170, 170);
    lv_arc_set_range(s_ui.quota_arc, 0, 100);
    lv_arc_set_value(s_ui.quota_arc, 68);
    lv_arc_set_bg_angles(s_ui.quota_arc, 0, 360);
    lv_obj_remove_style(s_ui.quota_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(s_ui.quota_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_ui.quota_arc, 14, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_ui.quota_arc, color(COLOR_SURFACE_ALT_DARK), LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_ui.quota_arc, 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_ui.quota_arc, color(COLOR_PURPLE), LV_PART_INDICATOR);
    s_ui.quota_value = make_label(page, "68%", 58, 91, 110,
                                  &lv_font_montserrat_28, false);
    lv_obj_set_style_text_align(s_ui.quota_value, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *remaining = make_label(page, "本周期剩余", 57, 130, 112,
                                     &app_font_14, true);
    lv_obj_set_style_text_align(remaining, LV_TEXT_ALIGN_CENTER, 0);

    const char *names[] = {"Gemini 3.1 Pro", "Gemini 3.5 Flash", "AI Credits"};
    const int values[] = {72, 61, 48};
    const char *values_text[] = {"72%", "61%", "1,240"};
    for (int i = 0; i < 3; ++i) {
        make_label(page, names[i], 230, 36 + i * 58, 165,
                   &lv_font_montserrat_14, true);
        lv_obj_t *value = make_label(page, values_text[i], 395, 36 + i * 58, 62,
                                     &lv_font_montserrat_14, false);
        lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
        make_bar(page, 230, 62 + i * 58, 227, values[i], COLOR_PURPLE);
    }
    make_accent_label(page, "MOCK · 暂无官方机器接口", 230, 224, 228,
                      &app_font_14, COLOR_PURPLE);
}

static void home_toggle_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    lv_obj_t *button = lv_event_get_target(event);
    lv_obj_t *state = lv_event_get_user_data(event);
    const bool enabled = lv_obj_has_state(button, LV_STATE_CHECKED);
    lv_label_set_text(state, enabled ? "已开启" : "已关闭");
    lv_obj_set_style_bg_color(button,
                              color(enabled ? 0x5B4023 :
                                    (s_ui.light_theme ? COLOR_SURFACE_LIGHT : COLOR_SURFACE_DARK)), 0);
    lv_obj_set_style_text_color(state,
                                color(enabled ? COLOR_ORANGE :
                                      (s_ui.light_theme ? COLOR_MUTED_LIGHT : COLOR_MUTED_DARK)), 0);
}

static void home_detail_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    intptr_t type = (intptr_t)lv_event_get_user_data(event);
    if (type == 0) {
        show_modal("空调", "当前 26°C · 自动模式\n温度和模式控制将在下一轮交互中细化。");
    } else {
        show_modal("窗帘", "当前打开 70%\nGateway 接入后将通过 Home Assistant 返回执行确认。");
    }
}

static void room_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    lv_obj_t *selected = lv_event_get_target(event);
    lv_obj_t *parent = lv_obj_get_parent(selected);
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(parent); ++i) {
        lv_obj_t *child = lv_obj_get_child(parent, i);
        lv_obj_clear_state(child, LV_STATE_CHECKED);
    }
    lv_obj_add_state(selected, LV_STATE_CHECKED);
}

static void create_home_page(lv_obj_t *page)
{
    s_ui.home_page = page;
    lv_obj_set_style_bg_color(page, color(0x211611), 0);
    make_label(page, "星期五 · 家中", 16, 9, 160, &app_font_14, true);
    make_label(page, "22:18", 15, 33, 160, &lv_font_montserrat_28, false);
    make_accent_label(page, "29°", 386, 13, 74, &lv_font_montserrat_28, COLOR_ORANGE);
    lv_obj_t *weather = make_label(page, "深圳 · 多云", 322, 50, 138, &app_font_14, true);
    lv_obj_set_style_text_align(weather, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t *tabs = lv_obj_create(page);
    s_ui.room_tabs = tabs;
    lv_obj_remove_style_all(tabs);
    lv_obj_set_pos(tabs, 15, 78);
    lv_obj_set_size(tabs, 220, 34);
    const char *rooms[] = {"客厅", "主卧", "书房"};
    for (int i = 0; i < 3; ++i) {
        lv_obj_t *button = make_button(tabs, rooms[i], i * 73, 0, 68, 30);
        lv_obj_add_flag(button, LV_OBJ_FLAG_CHECKABLE);
        if (i == 0) lv_obj_add_state(button, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(button, color(COLOR_SURFACE_ALT_DARK),
                                  LV_STATE_CHECKED);
        lv_obj_add_event_cb(button, room_event, LV_EVENT_CLICKED, NULL);
    }

    const char *names[] = {"客厅灯", "空调", "窗帘", "电视插座"};
    const char *states[] = {"已开启", "26°C · 自动", "打开 70%", "已开启"};
    for (int i = 0; i < 4; ++i) {
        lv_obj_t *button = lv_btn_create(page);
        lv_obj_remove_style_all(button);
        lv_obj_add_style(button, &s_style_card, 0);
        lv_obj_set_pos(button, 14 + i * 116, 124);
        lv_obj_set_size(button, 106, 117);
        make_accent_label(button, i == 0 ? "•" : i == 1 ? "A/C" : i == 2 ? "||" : "TV",
                          10, 10, 86, &lv_font_montserrat_20,
                          i == 0 || i == 3 ? COLOR_ORANGE : COLOR_BLUE);
        make_label(button, names[i], 9, 57, 88, &app_font_14, false);
        lv_obj_t *state = make_label(button, states[i], 9, 84, 88, &app_font_14, true);
        if (i == 0 || i == 3) {
            lv_obj_add_flag(button, LV_OBJ_FLAG_CHECKABLE);
            lv_obj_add_state(button, LV_STATE_CHECKED);
            lv_obj_set_style_bg_color(button, color(0x5B4023), 0);
            lv_obj_set_style_text_color(state, color(COLOR_ORANGE), 0);
            lv_obj_add_event_cb(button, home_toggle_event, LV_EVENT_CLICKED, state);
            if (s_ui.home_toggle_count < 2) {
                s_ui.home_toggle_buttons[s_ui.home_toggle_count] = button;
                s_ui.home_toggle_labels[s_ui.home_toggle_count] = state;
                s_ui.home_toggle_count++;
            }
        } else {
            lv_obj_add_event_cb(button, home_detail_event, LV_EVENT_CLICKED,
                                (void *)(intptr_t)(i == 1 ? 0 : 1));
        }
    }
}

static void draw_photo_scene(void)
{
    if (s_ui.photo_canvas == NULL || s_ui.photo_buffer == NULL) return;

    static const uint32_t sky_colors[] = {0x66B2D0, 0xEDB56D, 0x647D9B};
    static const uint32_t lake_colors[] = {0x2D7F9B, 0x7C7993, 0x3E607B};
    static const uint32_t mountain_colors[] = {0x315F59, 0x6C554A, 0x354B55};
    static const char *titles[] = {"青海湖 · 夏日", "深圳湾 · 黄昏", "阿勒泰 · 清晨"};

    const int index = s_ui.photo_index % 3;
    lv_canvas_fill_bg(s_ui.photo_canvas, color(sky_colors[index]), LV_OPA_COVER);

    lv_draw_rect_dsc_t rect;
    lv_draw_rect_dsc_init(&rect);
    rect.bg_opa = LV_OPA_COVER;
    rect.bg_color = color(0xFFD36A);
    rect.radius = LV_RADIUS_CIRCLE;
    lv_canvas_draw_rect(s_ui.photo_canvas, 355, 26, 56, 56, &rect);

    lv_draw_rect_dsc_init(&rect);
    rect.bg_opa = LV_OPA_COVER;
    rect.bg_color = color(lake_colors[index]);
    lv_canvas_draw_rect(s_ui.photo_canvas, 0, 158, WT32_LCD_WIDTH, 84, &rect);

    lv_draw_rect_dsc_t polygon;
    lv_draw_rect_dsc_init(&polygon);
    polygon.bg_opa = LV_OPA_COVER;
    polygon.bg_color = color(mountain_colors[index]);
    lv_point_t back[] = {{180, 165}, {305, 50}, {430, 165}};
    lv_canvas_draw_polygon(s_ui.photo_canvas, back, 3, &polygon);
    polygon.bg_color = color(index == 1 ? 0x473F3C : 0x234A49);
    lv_point_t front[] = {{-20, 170}, {145, 58}, {286, 170}};
    lv_canvas_draw_polygon(s_ui.photo_canvas, front, 3, &polygon);

    lv_label_set_text(s_ui.photo_title_shadow, titles[index]);
    lv_label_set_text(s_ui.photo_title, titles[index]);
    lv_label_set_text_fmt(s_ui.photo_counter, "%d / 2,436", 128 + index);
}

static void photo_canvas_delete_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_DELETE || s_ui.photo_buffer == NULL) return;
    heap_caps_free(s_ui.photo_buffer);
    s_ui.photo_buffer = NULL;
    s_ui.photo_canvas = NULL;
}

static void photo_nav_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    s_ui.photo_index += (int)(intptr_t)lv_event_get_user_data(event);
    if (s_ui.photo_index < 0) s_ui.photo_index = 2;
    if (s_ui.photo_index > 2) s_ui.photo_index = 0;
    draw_photo_scene();
}

static void create_gallery_page(lv_obj_t *page)
{
    const size_t buffer_size = LV_CANVAS_BUF_SIZE_TRUE_COLOR(WT32_LCD_WIDTH, 220);
    s_ui.photo_buffer = heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_ui.photo_buffer == NULL) {
        make_label(page, "相册画布内存不足", 90, 100, 300, &app_font_18, false);
        return;
    }
    s_ui.photo_canvas = lv_canvas_create(page);
    lv_obj_add_event_cb(s_ui.photo_canvas, photo_canvas_delete_event, LV_EVENT_DELETE, NULL);
    lv_canvas_set_buffer(s_ui.photo_canvas, s_ui.photo_buffer,
                         WT32_LCD_WIDTH, 220, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(s_ui.photo_canvas, 0, 0);

    s_ui.photo_title_shadow = make_accent_label(page, "青海湖 · 夏日", 20, 176, 270,
                                                &app_font_18, 0x000000);
    lv_obj_set_style_text_opa(s_ui.photo_title_shadow, LV_OPA_60, 0);
    s_ui.photo_title = make_accent_label(page, "青海湖 · 夏日", 18, 174, 270,
                                         &app_font_18, 0xFFFFFF);
    make_accent_label(page, "2026.08.02 · Synology Photos Mock", 18, 202, 330,
                      &lv_font_montserrat_14, 0xFFFFFF);
    s_ui.photo_counter = make_accent_label(page, "128 / 2,436", 350, 202, 112,
                                           &lv_font_montserrat_14, 0xFFFFFF);
    lv_obj_set_style_text_align(s_ui.photo_counter, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t *previous = make_button(page, "<", 14, 224, 52, 36);
    lv_obj_add_event_cb(previous, photo_nav_event, LV_EVENT_CLICKED,
                        (void *)(intptr_t)-1);
    lv_obj_t *favorite = make_button(page, "收藏", 187, 224, 106, 36);
    lv_obj_add_flag(favorite, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_t *next = make_button(page, ">", 414, 224, 52, 36);
    lv_obj_add_event_cb(next, photo_nav_event, LV_EVENT_CLICKED,
                        (void *)(intptr_t)1);
    draw_photo_scene();
}

static void alert_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    const char *const *detail = lv_event_get_user_data(event);
    show_modal(detail[0], detail[1]);
}

static void create_alert_page(lv_obj_t *page)
{
    s_ui.alert_counts = make_label(page, "严重 1   警告 1   信息 1", 16, 10, 300,
                                   &app_font_18, false);
    make_accent_label(page, "MOCK 告警流", 350, 13, 112, &app_font_14, COLOR_RED);

    static const char *details[][2] = {
        {"PVE 节点离线", "来源 pve-02 · 严重\n连续 3 次探测失败，最后在线时间 22:15。"},
        {"NAS 硬盘温度偏高", "来源 DS923+ · 警告\n硬盘 3 当前 46°C，阈值为 45°C。"},
        {"智能家居状态恢复", "来源 Home Assistant · 信息\n书房温湿度传感器已重新上线。"},
    };
    const char *source[] = {"PVE", "NAS", "HA"};
    const char *titles[] = {"节点 pve-02 离线", "硬盘 3 温度偏高", "书房传感器已恢复"};
    const char *times[] = {"22:15", "22:08", "21:56"};
    const uint32_t colors[] = {COLOR_RED, COLOR_ORANGE, COLOR_BLUE};
    for (int i = 0; i < 3; ++i) {
        lv_obj_t *row = make_card(page, 14, 50 + i * 61, 452, 53);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, alert_event, LV_EVENT_CLICKED, (void *)details[i]);
        make_accent_label(row, source[i], 9, 8, 50, &lv_font_montserrat_14, colors[i]);
        make_label(row, titles[i], 66, 7, 284, &app_font_14, false);
        lv_obj_t *time = make_label(row, times[i], 370, 8, 68,
                                    &lv_font_montserrat_14, true);
        lv_obj_set_style_text_align(time, LV_TEXT_ALIGN_RIGHT, 0);
        make_label(row, i == 2 ? "已恢复" : "未确认", 66, 29, 130,
                   &app_font_14, true);
    }
    make_label(page, "点击告警查看详情和确认状态", 14, 239, 330, &app_font_14, true);
}

static esp_err_t save_setting(const char *key, uint8_t value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("dashboard", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_u8(handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

static uint8_t load_setting(const char *key, uint8_t fallback)
{
    nvs_handle_t handle;
    uint8_t value = fallback;
    if (nvs_open("dashboard", NVS_READONLY, &handle) == ESP_OK) {
        nvs_get_u8(handle, key, &value);
        nvs_close(handle);
    }
    return value;
}

static void brightness_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
    int value = lv_slider_get_value(lv_event_get_target(event));
    wt32_board_set_brightness((uint8_t)value);
    lv_label_set_text_fmt(s_ui.brightness_value, "%d%%", value);
}

static void brightness_commit_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_RELEASED) return;
    const uint8_t value = (uint8_t)lv_slider_get_value(lv_event_get_target(event));
    if (value == s_ui.saved_brightness) return;

    esp_err_t err = save_setting("brightness", value);
    if (err == ESP_OK) {
        s_ui.saved_brightness = value;
    } else {
        ESP_LOGW(TAG, "Unable to save brightness: %s", esp_err_to_name(err));
    }
}

static void theme_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
    bool light = lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED);
    apply_theme(light);
    esp_err_t err = save_setting("light", light ? 1 : 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Unable to save theme: %s", esp_err_to_name(err));
    }
}

static void create_setting_tile(lv_obj_t *page, const char *title, const char *value,
                                int x, int y, int width, uint32_t accent,
                                lv_event_cb_t callback, const void *user_data)
{
    lv_obj_t *button = lv_btn_create(page);
    lv_obj_remove_style_all(button);
    lv_obj_add_style(button, &s_style_card, 0);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, width, 76);
    make_accent_label(button, "•", 9, 8, 20, &lv_font_montserrat_14, accent);
    make_label(button, title, 31, 7, width - 38, &app_font_14, false);
    lv_obj_t *value_label = make_label(button, value, 10, 44, width - 20,
                                       &app_font_14, true);
    if (callback != NULL) {
        lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, (void *)user_data);
    }
    if (strcmp(title, "亮度") == 0) s_ui.brightness_value = value_label;
    if (strcmp(title, "主题") == 0) s_ui.theme_value = value_label;
}

static void create_settings_page(lv_obj_t *page)
{
    static const char *wifi_detail[] = {"Wi-Fi", "当前为界面验证阶段，网络尚未连接。\n接入 Gateway 时再配置 SSID 与设备凭据。"};
    static const char *city_detail[] = {"城市", "当前城市：深圳\n后续可由 Gateway 下发城市、时区和天气位置。"};
    static const char *service_detail[] = {"数据服务", "Local Mock 正常\nDashboard Gateway 尚未绑定到此设备。"};
    static const char *ota_detail[] = {"OTA", "当前版本 v0.1.0-mock\n双 OTA 分区已经预留，检查更新仅作演示。"};

    create_setting_tile(page, "Wi-Fi", "未连接", 14, 12, 144, COLOR_BLUE,
                        detail_button_event, wifi_detail);
    create_setting_tile(page, "亮度", "72%", 168, 12, 144, COLOR_ORANGE, NULL, NULL);
    create_setting_tile(page, "主题", "深色", 322, 12, 144, COLOR_PURPLE, NULL, NULL);
    create_setting_tile(page, "城市", "深圳", 14, 98, 144, COLOR_RED,
                        detail_button_event, city_detail);
    create_setting_tile(page, "服务", "Local Mock", 168, 98, 144, COLOR_GREEN,
                        detail_button_event, service_detail);
    create_setting_tile(page, "OTA", "v0.1.0", 322, 98, 144, COLOR_BLUE,
                        detail_button_event, ota_detail);

    s_ui.brightness_slider = lv_slider_create(page);
    lv_obj_set_pos(s_ui.brightness_slider, 38, 200);
    lv_obj_set_size(s_ui.brightness_slider, 285, 12);
    lv_slider_set_range(s_ui.brightness_slider, 10, 100);
    lv_obj_set_style_bg_color(s_ui.brightness_slider, color(COLOR_SURFACE_ALT_DARK),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.brightness_slider, color(COLOR_ORANGE),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_ui.brightness_slider, color(COLOR_ORANGE),
                              LV_PART_KNOB);
    lv_obj_add_event_cb(s_ui.brightness_slider, brightness_event,
                        LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_ui.brightness_slider, brightness_commit_event,
                        LV_EVENT_RELEASED, NULL);
    make_label(page, "背光", 38, 222, 80, &app_font_14, true);

    s_ui.theme_switch = lv_switch_create(page);
    lv_obj_clear_flag(s_ui.theme_switch, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_set_pos(s_ui.theme_switch, 380, 190);
    lv_obj_set_size(s_ui.theme_switch, 68, 34);
    lv_obj_set_style_bg_color(s_ui.theme_switch, color(COLOR_PURPLE),
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_ui.theme_switch, theme_event, LV_EVENT_VALUE_CHANGED, NULL);
    make_label(page, "浅色", 378, 228, 74, &app_font_14, true);

    uint8_t brightness = load_setting("brightness", 72);
    uint8_t light = load_setting("light", 0);
    if (brightness < 10 || brightness > 100) brightness = 72;
    s_ui.saved_brightness = brightness;
    lv_slider_set_value(s_ui.brightness_slider, brightness, LV_ANIM_OFF);
    wt32_board_set_brightness(brightness);
    lv_label_set_text_fmt(s_ui.brightness_value, "%u%%", brightness);
    if (light) lv_obj_add_state(s_ui.theme_switch, LV_STATE_CHECKED);
    apply_theme(light != 0);
}

static const char *page_titles[PAGE_COUNT] = {
    "时间", "资讯", "日历", "天气", "PVE", "群晖 NAS",
    "Antigravity", "智能家居", "相册", "告警", "设置",
};

static uint32_t get_active_page_index(void)
{
    lv_obj_t *active_tile = lv_tileview_get_tile_act(s_ui.tileview);
    for (uint32_t index = 0; index < PAGE_COUNT; ++index) {
        if (s_ui.pages[index] == active_tile) {
            return index;
        }
    }
    return 0;
}

static uint32_t update_navigation(void)
{
    const uint32_t column = get_active_page_index();
    lv_label_set_text(s_ui.status_title, page_titles[column]);
    for (int i = 0; i < PAGE_COUNT; ++i) {
        lv_obj_t *dot = s_ui.dots[i];
        const bool active = (uint32_t)i == column;
        lv_obj_set_size(dot, active ? 8 : 6, active ? 8 : 6);
        lv_obj_set_style_bg_opa(dot, active ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(dot, color(s_ui.light_theme ? COLOR_TEXT_LIGHT : COLOR_TEXT_DARK), 0);
        lv_obj_set_style_border_color(dot, color(s_ui.light_theme ? COLOR_MUTED_LIGHT : COLOR_MUTED_DARK), 0);
    }
    return column;
}

static void tileview_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED) {
        const uint32_t page = update_navigation();
        ESP_LOGI(TAG, "Page changed: %" PRIu32 "/%d (%s)",
                 page + 1, PAGE_COUNT, page_titles[page]);
    }
}

static void tileview_scroll_event(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_SCROLL_BEGIN || code == LV_EVENT_SCROLL) {
        s_ui.scroll_in_progress = true;
        return;
    }
    if (code != LV_EVENT_SCROLL_END) {
        return;
    }
    lv_indev_t *indev = lv_indev_get_act();
    if (indev != NULL && indev->proc.state == LV_INDEV_STATE_PRESSED) return;

    s_ui.scroll_in_progress = false;
    if (!s_ui.has_deferred_snapshot) return;

    const app_snapshot_t snapshot = s_ui.deferred_snapshot;
    s_ui.has_deferred_snapshot = false;
    dashboard_ui_update(&snapshot);
}

static void dot_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    int column = (int)(intptr_t)lv_event_get_user_data(event);
    lv_obj_set_tile_id(s_ui.tileview, column, 0, LV_ANIM_ON);
}

static void create_navigation(void)
{
    s_ui.page_bar = lv_obj_create(s_ui.root);
    lv_obj_remove_style_all(s_ui.page_bar);
    lv_obj_add_style(s_ui.page_bar, &s_style_status, 0);
    lv_obj_set_pos(s_ui.page_bar, 0, 294);
    lv_obj_set_size(s_ui.page_bar, WT32_LCD_WIDTH, 26);
    lv_obj_clear_flag(s_ui.page_bar, LV_OBJ_FLAG_SCROLLABLE);

    const int start_x = (WT32_LCD_WIDTH - PAGE_COUNT * 24) / 2;
    for (int i = 0; i < PAGE_COUNT; ++i) {
        lv_obj_t *hit = lv_btn_create(s_ui.page_bar);
        lv_obj_remove_style_all(hit);
        lv_obj_set_pos(hit, start_x + i * 24, 1);
        lv_obj_set_size(hit, 24, 24);
        lv_obj_set_style_bg_opa(hit, LV_OPA_TRANSP, 0);
        lv_obj_add_event_cb(hit, dot_event, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *dot = lv_obj_create(hit);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 6, 6);
        lv_obj_center(dot);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot, 1, 0);
        lv_obj_set_style_border_color(dot, color(COLOR_MUTED_DARK), 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        s_ui.dots[i] = dot;
    }
}

esp_err_t dashboard_ui_create(void)
{
    if (s_ui.root != NULL) return ESP_ERR_INVALID_STATE;
    memset(&s_ui, 0, sizeof(s_ui));
    init_styles();

    s_ui.root = lv_scr_act();
    lv_obj_remove_style_all(s_ui.root);
    lv_obj_add_style(s_ui.root, &s_style_root, 0);
    lv_obj_clear_flag(s_ui.root, LV_OBJ_FLAG_SCROLLABLE);

    s_ui.status_bar = lv_obj_create(s_ui.root);
    lv_obj_remove_style_all(s_ui.status_bar);
    lv_obj_add_style(s_ui.status_bar, &s_style_status, 0);
    lv_obj_set_pos(s_ui.status_bar, 0, 0);
    lv_obj_set_size(s_ui.status_bar, WT32_LCD_WIDTH, 30);
    lv_obj_clear_flag(s_ui.status_bar, LV_OBJ_FLAG_SCROLLABLE);
    make_accent_label(s_ui.status_bar, "•", 12, 6, 20,
                      &lv_font_montserrat_14, COLOR_GREEN);
    s_ui.status_title = make_label(s_ui.status_bar, "时间", 32, 5, 150,
                                   &app_font_14, false);
    s_ui.status_time = make_label(s_ui.status_bar, "22:18", 354, 5, 70,
                                  &lv_font_montserrat_14, true);
    lv_obj_set_style_text_align(s_ui.status_time, LV_TEXT_ALIGN_RIGHT, 0);
    make_accent_label(s_ui.status_bar, "MOCK", 428, 5, 45,
                      &lv_font_montserrat_14, COLOR_BLUE);

    s_ui.tileview = lv_tileview_create(s_ui.root);
    lv_obj_remove_style_all(s_ui.tileview);
    lv_obj_set_pos(s_ui.tileview, 0, 30);
    lv_obj_set_size(s_ui.tileview, WT32_LCD_WIDTH, CONTENT_HEIGHT);
    lv_obj_set_scrollbar_mode(s_ui.tileview, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_snap_x(s_ui.tileview, LV_SCROLL_SNAP_CENTER);
    lv_obj_add_event_cb(s_ui.tileview, tileview_event, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_ui.tileview, tileview_scroll_event, LV_EVENT_SCROLL_BEGIN, NULL);
    lv_obj_add_event_cb(s_ui.tileview, tileview_scroll_event, LV_EVENT_SCROLL, NULL);
    lv_obj_add_event_cb(s_ui.tileview, tileview_scroll_event, LV_EVENT_SCROLL_END, NULL);

    for (int i = 0; i < PAGE_COUNT; ++i) {
        add_tile(i);
    }
    create_time_page(s_ui.pages[0]);
    create_market_page(s_ui.pages[1]);
    create_calendar_page(s_ui.pages[2]);
    create_weather_page(s_ui.pages[3]);
    create_pve_page(s_ui.pages[4]);
    create_nas_page(s_ui.pages[5]);
    create_quota_page(s_ui.pages[6]);
    create_home_page(s_ui.pages[7]);
    create_gallery_page(s_ui.pages[8]);
    create_alert_page(s_ui.pages[9]);
    create_settings_page(s_ui.pages[10]);
    create_navigation();
    lv_obj_set_tile_id(s_ui.tileview, 0, 0, LV_ANIM_OFF);
    update_navigation();

    ESP_LOGI(TAG, "Created %d LVGL pages; music replaced by alert center", PAGE_COUNT);
    return ESP_OK;
}

void dashboard_ui_update(const app_snapshot_t *snapshot)
{
    if (snapshot == NULL || s_ui.root == NULL) return;

    if (s_ui.scroll_in_progress || lv_obj_is_scrolling(s_ui.tileview)) {
        s_ui.deferred_snapshot = *snapshot;
        s_ui.has_deferred_snapshot = true;
        return;
    }

    const bool first = !s_ui.has_applied_snapshot;
    const app_snapshot_t *previous = &s_ui.applied_snapshot;
    const bool time_changed = first || snapshot->hour != previous->hour ||
                              snapshot->minute != previous->minute;

    if (time_changed) {
        draw_digital_clock(snapshot->hour, snapshot->minute);
        lv_label_set_text_fmt(s_ui.status_time, "%02u:%02u",
                              snapshot->hour, snapshot->minute);
        lv_label_set_text_fmt(s_ui.weather_time, "%02u:%02u",
                              snapshot->hour, snapshot->minute);
    }
    if (first || snapshot->byd_price != previous->byd_price) {
        lv_label_set_text_fmt(s_ui.market_price, "%.2f", snapshot->byd_price);
        lv_chart_set_next_value(s_ui.market_chart, s_ui.market_series,
                                (lv_coord_t)snapshot->byd_price);
    }
    if (first || snapshot->byd_change_percent != previous->byd_change_percent) {
        lv_label_set_text_fmt(s_ui.market_change, "%+.2f%%",
                              snapshot->byd_change_percent);
    }
    if (first || snapshot->weather_temperature != previous->weather_temperature) {
        lv_label_set_text_fmt(s_ui.weather_temp, "%d° · 深圳",
                              snapshot->weather_temperature);
    }
    if (first || snapshot->weather_humidity != previous->weather_humidity) {
        lv_label_set_text_fmt(s_ui.weather_humidity, "%d%%",
                              snapshot->weather_humidity);
    }
    if (first || snapshot->pve_cpu != previous->pve_cpu) {
        lv_label_set_text_fmt(s_ui.pve_cpu, "%d%%", snapshot->pve_cpu);
        lv_bar_set_value(s_ui.pve_cpu_bar, snapshot->pve_cpu, LV_ANIM_OFF);
    }
    if (first || snapshot->pve_memory != previous->pve_memory) {
        lv_label_set_text_fmt(s_ui.pve_memory, "%d%%", snapshot->pve_memory);
        lv_bar_set_value(s_ui.pve_memory_bar, snapshot->pve_memory, LV_ANIM_OFF);
    }
    if (first || snapshot->pve_storage != previous->pve_storage) {
        lv_label_set_text_fmt(s_ui.pve_storage, "%d%% · 1.42 / 1.96 TB",
                              snapshot->pve_storage);
        lv_bar_set_value(s_ui.pve_storage_bar, snapshot->pve_storage, LV_ANIM_OFF);
    }
    for (int i = 0; i < 4; ++i) {
        if (first || snapshot->nas_temperatures[i] != previous->nas_temperatures[i]) {
            lv_label_set_text_fmt(s_ui.nas_temperature[i], "%d°C",
                                  snapshot->nas_temperatures[i]);
        }
    }
    if (first || snapshot->antigravity_remaining != previous->antigravity_remaining) {
        lv_arc_set_value(s_ui.quota_arc, snapshot->antigravity_remaining);
        lv_label_set_text_fmt(s_ui.quota_value, "%d%%",
                              snapshot->antigravity_remaining);
    }
    if (first || snapshot->alert_critical != previous->alert_critical ||
        snapshot->alert_warning != previous->alert_warning ||
        snapshot->alert_info != previous->alert_info) {
        lv_label_set_text_fmt(s_ui.alert_counts, "严重 %d   警告 %d   信息 %d",
                              snapshot->alert_critical, snapshot->alert_warning,
                              snapshot->alert_info);
    }

    s_ui.applied_snapshot = *snapshot;
    s_ui.has_applied_snapshot = true;
    s_ui.has_deferred_snapshot = false;
}
