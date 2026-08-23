#include "dashboard_ui.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "board_wt32.h"
#include "dashboard_icons.h"
#include "device_settings.h"
#include "esp_log.h"
#include "lvgl.h"
#include "nas_disk_sort.h"
#include "network_manager.h"

LV_FONT_DECLARE(app_font_14);
LV_FONT_DECLARE(app_font_18);

#define PAGE_COUNT 3
#define VM_VISIBLE 4
#define PVE_IDENTITY_HEIGHT 40
#define PVE_METRIC_ROW_HEIGHT 64
#define PVE_PROCESSOR_ROW_HEIGHT 42
#define PVE_NARROW_WIDTH 168
#define PVE_VM_BUTTON_Y 224
#define PVE_VM_HIT_HEIGHT 56
#define PVE_VM_VISUAL_HEIGHT 40
#define PVE_RULE_COUNT 6
#define PVE_VM_LIST_RULE_COUNT 9
#define PVE_VM_DETAIL_RULE_COUNT 4
#define NAS_VISIBLE 4
#define NAS_DISK_VISIBLE 4
#define NAS_METRICS_HEIGHT 52
#define NAS_FOOTER_Y 236
#define NAS_METRIC_CELL_WIDTH 55
#define NAS_FOOTER_NETWORK_WIDTH 80
#define NAS_FOOTER_IP_WIDTH 130
#define NAS_FOOTER_UPTIME_WIDTH 58
#define NAS_FOOTER_STATIC_Y 18
#define NAS_POOL_ROW_HEIGHT 52
#define NAS_POOL_ROW_STEP 55
#define NAS_POOL_ICON_Y 7
#define NAS_POOL_STATUS_Y 11
#define NAS_POOL_TEXT_Y 7
#define NAS_POOL_BAR_Y 38
#define NAS_POOL_ROW_WIDTH_PAGED 420
#define NAS_POOL_ROW_WIDTH_FULL 464
#define NAS_POOL_BAR_WIDTH_PAGED 402
#define NAS_POOL_BAR_WIDTH_FULL 446
#define TOAST_DURATION_MS 2500
#define THEME_COUNT 5
#define REFRESH_OPTION_COUNT 4

typedef enum {
    THEME_DEEP_OCEAN,
    THEME_HIGH_CONTRAST,
    THEME_MIST_GRAY,
    THEME_GRAPHITE,
    THEME_CHARCOAL_CORAL,
} dashboard_theme_t;

typedef struct {
    const char *name;
    uint32_t background;
    uint32_t surface;
    uint32_t surface_alt;
    uint32_t line;
    uint32_t text;
    uint32_t muted;
    uint32_t primary;
    uint32_t selected_text;
    uint32_t positive;
    uint32_t warning;
    uint32_t used;
    uint32_t free;
} dashboard_palette_t;

static const dashboard_palette_t s_palettes[THEME_COUNT] = {
    [THEME_DEEP_OCEAN] = {"深海蓝", 0x071B24, 0x0D2B36, 0x123D49, 0x246070,
                          0xECF8FA, 0x90B8BF, 0x20BFD4, 0x031416, 0x4DE3AE,
                          0xFFC857, 0xFF6B6B, 0x2B5D68},
    [THEME_HIGH_CONTRAST] = {"高对比", 0x000000, 0x111111, 0x252525, 0xFFFFFF,
                             0xFFFFFF, 0xD0D0D0, 0xFFE600, 0x000000, 0x35F06F,
                             0xFFE600, 0xFF3B30, 0x4A4A4A},
    [THEME_MIST_GRAY] = {"雾灰白", 0xE8EDF0, 0xFFFFFF, 0xDCE4E8, 0xAAB8BF,
                         0x152126, 0x5F7078, 0x147D9A, 0xFFFFFF, 0x18885B,
                         0xA86800, 0xD84A4A, 0xC9D5DA},
    [THEME_GRAPHITE] = {"石墨青", 0x10161A, 0x1B242A, 0x26323A, 0x344149,
                        0xF3F6F8, 0x9CA8B3, 0x31B6A2, 0x071411, 0x44D290,
                        0xF0B44D, 0xFF5C66, 0x405159},
    [THEME_CHARCOAL_CORAL] = {"炭黑珊瑚", 0x171719, 0x242427, 0x323237, 0x48484F,
                              0xF8F4F2, 0xB8AFAC, 0xFF806F, 0x21100D, 0x67D6B1,
                              0xF4C95D, 0xFF806F, 0x53535A},
};

typedef enum {
    COLOR_BG,
    COLOR_SURFACE,
    COLOR_SURFACE_ALT,
    COLOR_LINE,
    COLOR_TEXT,
    COLOR_MUTED,
    COLOR_BLUE,
    COLOR_GREEN,
    COLOR_RED,
    COLOR_GRAY,
} color_role_t;

static uint8_t s_theme_id = THEME_GRAPHITE;
static uint8_t s_refresh_seconds = 5;
static uint8_t s_homepage = 0;

static const dashboard_palette_t *palette(void)
{
    return &s_palettes[s_theme_id < THEME_COUNT ? s_theme_id : THEME_GRAPHITE];
}

#define PVE_VM_SCROLL_UP "PVE_VM_SCROLL_UP"
#define PVE_VM_SCROLL_DOWN "PVE_VM_SCROLL_DOWN"

typedef enum {
    PVE_VIEW_NODE = 0,
    PVE_VIEW_VM_LIST,
    PVE_VIEW_VM_DETAIL,
} pve_view_t;

typedef struct {
    lv_obj_t *root;
    lv_obj_t *top_bar;
    lv_obj_t *pages[PAGE_COUNT];
    lv_obj_t *pve_nav_button;
    lv_obj_t *nas_nav_button;
    lv_obj_t *settings_nav_button;
    lv_obj_t *time_label;
    lv_obj_t *ip_label;
    lv_obj_t *pve_name;
    lv_obj_t *pve_version;
    lv_obj_t *pve_host;
    lv_obj_t *pve_uptime;
    lv_obj_t *pve_cpu_model;
    lv_obj_t *pve_cpu_cores_value;
    lv_obj_t *pve_rules[PVE_RULE_COUNT];
    lv_obj_t *pve_status_dot;
    lv_obj_t *pve_cpu_value;
    lv_obj_t *pve_memory_value;
    lv_obj_t *pve_storage_value;
    lv_obj_t *pve_load_values[3];
    lv_obj_t *pve_cpu_bar;
    lv_obj_t *pve_memory_bar;
    lv_obj_t *pve_storage_bar;
    pve_view_t pve_view;
    lv_obj_t *pve_node_view;
    lv_obj_t *pve_vm_list;
    lv_obj_t *pve_vm_list_button;
    lv_obj_t *pve_vm_list_visual;
    lv_obj_t *pve_vm_list_label;
    lv_obj_t *vm_list_rules[PVE_VM_LIST_RULE_COUNT];
    lv_obj_t *vm_detail;
    lv_obj_t *vm_detail_rules[PVE_VM_DETAIL_RULE_COUNT];
    lv_obj_t *vm_detail_title;
    lv_obj_t *vm_detail_status_dot;
    lv_obj_t *vm_detail_status;
    lv_obj_t *vm_detail_ip;
    lv_obj_t *vm_detail_cpu;
    lv_obj_t *vm_detail_memory;
    lv_obj_t *vm_detail_disk;
    lv_obj_t *vm_detail_uptime;
    lv_obj_t *vm_detail_agent;
    lv_obj_t *vm_title;
    lv_obj_t *vm_header_ip;
    lv_obj_t *vm_header_cpu;
    lv_obj_t *vm_header_memory;
    lv_obj_t *vm_list_pager;
    lv_obj_t *vm_list_previous;
    lv_obj_t *vm_list_next;
    lv_obj_t *vm_list_page;
    lv_obj_t *vm_list_empty;
    lv_obj_t *vm_rows[VM_VISIBLE];
    lv_obj_t *vm_name_buttons[VM_VISIBLE];
    lv_obj_t *vm_row_labels[VM_VISIBLE];
    lv_obj_t *vm_row_dots[VM_VISIBLE];
    lv_obj_t *vm_row_ips[VM_VISIBLE];
    lv_obj_t *vm_row_cpu[VM_VISIBLE];
    lv_obj_t *vm_row_memory[VM_VISIBLE];
    int vm_list_offset;
    int selected_vm_index;
    lv_obj_t *nas_overview;
    lv_obj_t *nas_overview_message;
    lv_obj_t *nas_rows[NAS_VISIBLE];
    lv_obj_t *nas_row_status_dots[NAS_VISIBLE];
    lv_obj_t *nas_row_labels[NAS_VISIBLE];
    lv_obj_t *nas_row_descriptions[NAS_VISIBLE];
    lv_obj_t *nas_row_values[NAS_VISIBLE];
    lv_obj_t *nas_row_bars[NAS_VISIBLE];
    lv_obj_t *nas_pool_previous;
    lv_obj_t *nas_pool_next;
    lv_obj_t *nas_pool_page;
    size_t nas_pool_offset;
    lv_obj_t *nas_detail;
    lv_obj_t *nas_disk_empty;
    lv_obj_t *nas_disk_previous;
    lv_obj_t *nas_disk_next;
    lv_obj_t *nas_disk_page;
    lv_obj_t *nas_disk_pager;
    lv_obj_t *nas_disk_sort_buttons[3];
    lv_obj_t *nas_disk_rows[NAS_DISK_VISIBLE];
    lv_obj_t *nas_disk_dots[NAS_DISK_VISIBLE];
    lv_obj_t *nas_disk_ids[NAS_DISK_VISIBLE];
    lv_obj_t *nas_disk_models[NAS_DISK_VISIBLE];
    lv_obj_t *nas_disk_temperatures[NAS_DISK_VISIBLE];
    lv_obj_t *nas_cpu_value;
    lv_obj_t *nas_memory_value;
    lv_obj_t *nas_temperature_value;
    lv_obj_t *nas_upload_value;
    lv_obj_t *nas_download_value;
    lv_obj_t *nas_ip_value;
    lv_obj_t *nas_uptime_value;
    bool nas_disks_visible;
    size_t nas_disk_offset;
    nas_disk_sort_key_t nas_disk_sort_key;
    bool nas_disk_sort_descending;
    lv_obj_t *wifi_dropdown;
    lv_obj_t *wifi_status;
    lv_obj_t *brightness_value;
    lv_obj_t *brightness_slider;
    lv_obj_t *rotation_0_button;
    lv_obj_t *rotation_180_button;
    lv_obj_t *theme_name;
    lv_obj_t *refresh_buttons[REFRESH_OPTION_COUNT];
    lv_obj_t *homepage_buttons[2];
    lv_obj_t *password_overlay;
    lv_obj_t *password_textarea;
    lv_obj_t *portal_overlay;
    lv_obj_t *portal_status_label;
    lv_timer_t *settings_timer;
    lv_timer_t *clock_timer;
    lv_obj_t *toast;
    bool scan_pending;
    network_scan_record_t scan_records[NETWORK_MAX_SCAN_RESULTS];
    size_t scan_count;
    int active_page;
    app_snapshot_t snapshot;
    bool has_snapshot;
    bool has_pve_snapshot;
    bool has_nas_snapshot;
} dashboard_context_t;

static dashboard_context_t s_ui;
static lv_style_t s_root_style;
static lv_style_t s_surface_style;
static lv_style_t s_button_style;
static lv_style_t s_muted_button_style;
static lv_style_t s_text_styles[4];

static void update_pve(void);
static void update_nas(void);
static void update_nas_disks(void);
static void update_vm_detail(void);
static void show_pve_node(void);
static void show_pve_vm_list(void);
static void vm_name_event(lv_event_t *event);
static void set_rotation_button_state(void);
static void set_refresh_button_state(void);
static void set_homepage_button_state(void);
static void nas_disk_sort_event(lv_event_t *event);
static void nas_disk_pager_event(lv_event_t *event);

static uint32_t role_color(color_role_t role)
{
    switch (role) {
        case COLOR_BG: return palette()->background;
        case COLOR_SURFACE: return palette()->surface;
        case COLOR_SURFACE_ALT: return palette()->surface_alt;
        case COLOR_LINE: return palette()->line;
        case COLOR_TEXT: return palette()->text;
        case COLOR_MUTED: return palette()->muted;
        case COLOR_BLUE: return palette()->primary;
        case COLOR_GREEN: return palette()->positive;
        case COLOR_RED: return palette()->warning;
        case COLOR_GRAY: return palette()->muted;
        default: return palette()->text;
    }
}

static lv_color_t color(color_role_t role) { return lv_color_hex(role_color(role)); }

static void set_hidden(lv_obj_t *object, bool hidden)
{
    if (object == NULL) return;
    if (hidden) lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(object, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, int x, int y, int width,
                            const lv_font_t *font, color_role_t text_color)
{
    lv_obj_t *object = lv_label_create(parent);
    lv_label_set_text(object, text != NULL ? text : "");
    lv_label_set_long_mode(object, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_width(object, width);
    lv_obj_set_style_text_font(object, font != NULL ? font : &app_font_14, 0);
    const int style_index = text_color == COLOR_MUTED || text_color == COLOR_GRAY ? 1 :
                            text_color == COLOR_GREEN ? 2 : text_color == COLOR_RED ? 3 : 0;
    lv_obj_add_style(object, &s_text_styles[style_index], 0);
    return object;
}

static lv_obj_t *make_status_dot(lv_obj_t *parent, int x, int y, int diameter, color_role_t role)
{
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_remove_style_all(dot);
    lv_obj_set_pos(dot, x, y);
    lv_obj_set_size(dot, diameter, diameter);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, color(role), 0);
    return dot;
}

static lv_obj_t *make_surface(lv_obj_t *parent, int x, int y, int width, int height)
{
    lv_obj_t *object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_add_style(object, &s_surface_style, 0);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    return object;
}

static lv_obj_t *make_icon(lv_obj_t *parent, const lv_img_dsc_t *source,
                           int x, int y, uint32_t recolor)
{
    lv_obj_t *image = lv_img_create(parent);
    lv_img_set_src(image, source);
    lv_obj_set_pos(image, x, y);
    lv_obj_set_style_img_recolor(image, lv_color_hex(recolor), 0);
    lv_obj_set_style_img_recolor_opa(image, LV_OPA_COVER, 0);
    lv_obj_clear_flag(image, LV_OBJ_FLAG_CLICKABLE);
    return image;
}

static lv_obj_t *make_brand_icon(lv_obj_t *parent, const lv_img_dsc_t *source,
                                 int x, int y)
{
    lv_obj_t *image = lv_img_create(parent);
    lv_img_set_src(image, source);
    lv_obj_set_pos(image, x, y);
    lv_obj_set_style_img_recolor_opa(image, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(image, LV_OBJ_FLAG_CLICKABLE);
    return image;
}

static lv_obj_t *make_rule(lv_obj_t *parent, int x, int y, int width, int height)
{
    lv_obj_t *rule = lv_obj_create(parent);
    lv_obj_remove_style_all(rule);
    lv_obj_set_pos(rule, x, y);
    lv_obj_set_size(rule, width, height);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(rule, color(COLOR_LINE), 0);
    lv_obj_clear_flag(rule, LV_OBJ_FLAG_CLICKABLE);
    return rule;
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, int x, int y, int width,
                             int height, lv_event_cb_t callback, void *user_data)
{
    lv_obj_t *object = lv_btn_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_add_style(object, &s_button_style, 0);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    if (callback != NULL) lv_obj_add_event_cb(object, callback, LV_EVENT_CLICKED, user_data);
    lv_obj_t *text_label = make_label(object, text, 0, 0, width, &app_font_14, COLOR_TEXT);
    lv_obj_center(text_label);
    lv_obj_set_style_text_align(text_label, LV_TEXT_ALIGN_CENTER, 0);
    return object;
}

static lv_obj_t *make_icon_button(lv_obj_t *parent, const char *text,
                                  const lv_img_dsc_t *icon,
                                  int x, int y, int width, int height,
                                  lv_event_cb_t callback, void *user_data)
{
    lv_obj_t *button = make_button(parent, text, x, y, width, height, callback, user_data);
    lv_obj_t *label = lv_obj_get_child(button, 0);
    lv_obj_set_x(label, 10);
    lv_obj_set_width(label, width - 24);
    make_brand_icon(button, icon, 6, (height - 16) / 2);
    return button;
}

static lv_obj_t *make_network_metric_row(lv_obj_t *parent,
                                         const lv_img_dsc_t *icon,
                                         int x, int y)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_pos(row, x, y);
    lv_obj_set_size(row, NAS_FOOTER_NETWORK_WIDTH, 26);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 4, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
    make_icon(row, icon, 0, 0, palette()->muted);
    lv_obj_t *value = make_label(row, "--", 0, 0, 48, &app_font_14, COLOR_TEXT);
    lv_obj_set_width(value, LV_SIZE_CONTENT);
    lv_obj_clear_flag(value, LV_OBJ_FLAG_CLICKABLE);
    return value;
}

static void set_button_selected(lv_obj_t *button, bool selected)
{
    if (button == NULL) return;
    lv_obj_set_style_bg_color(button, color(selected ? COLOR_BLUE : COLOR_SURFACE_ALT), 0);
    lv_obj_t *label = lv_obj_get_child(button, 0);
    if (label != NULL) {
        lv_obj_set_style_text_color(label,
                                    lv_color_hex(selected ? palette()->selected_text : palette()->text), 0);
    }
}

static lv_obj_t *make_split_bar(lv_obj_t *parent, int x, int y, int width, int height,
                                uint64_t used, uint64_t total)
{
    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_pos(bar, x, y);
    lv_obj_set_size(bar, width, height);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, total == 0 ? 0 : (int)((used > total ? total : used) * 100ULL / total), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, lv_color_hex(palette()->free), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(palette()->used), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 2, LV_PART_INDICATOR);
    return bar;
}

static void format_bytes(char *out, size_t size, uint64_t bytes)
{
    if (bytes == 0) snprintf(out, size, "0B");
    else if (bytes >= (1ULL << 40)) snprintf(out, size, "%.1fT", (double)bytes / (1ULL << 40));
    else if (bytes >= (1ULL << 30)) snprintf(out, size, "%.1fG", (double)bytes / (1ULL << 30));
    else if (bytes >= (1ULL << 20)) snprintf(out, size, "%.1fM", (double)bytes / (1ULL << 20));
    else snprintf(out, size, "%" PRIu64 "B", bytes);
}

static void format_capacity(char *out, size_t size, uint64_t bytes)
{
    if (bytes >= (100ULL << 40))
        snprintf(out, size, "%.0fT", (double)bytes / (1ULL << 40));
    else if (bytes >= (1ULL << 40))
        snprintf(out, size, "%.1fT", (double)bytes / (1ULL << 40));
    else if (bytes >= (100ULL << 30))
        snprintf(out, size, "%.0fG", (double)bytes / (1ULL << 30));
    else if (bytes >= (1ULL << 30))
        snprintf(out, size, "%.1fG", (double)bytes / (1ULL << 30));
    else
        format_bytes(out, size, bytes);
}

static void format_uptime(char *out, size_t size, uint32_t seconds)
{
    const uint32_t days = seconds / 86400U;
    const uint32_t hours = seconds / 3600U % 24U;
    const uint32_t minutes = seconds / 60U % 60U;
    if (days > 999)
        snprintf(out, size, "%.1f年", (double)days / 365.0);
    else if (days > 0)
        snprintf(out, size, "%" PRIu32 "天", days);
    else if (hours > 0)
        snprintf(out, size, "%" PRIu32 "时", hours);
    else
        snprintf(out, size, "%" PRIu32 "分", minutes);
}

static void format_percent(char *out, size_t size, float value)
{
    if (value < 0) value = 0;
    if (value > 100) value = 100;
    snprintf(out, size, "%.0f%%", value);
}

static void format_rate(char *out, size_t size, uint64_t bytes_per_second)
{
    static const char *const units[] = {"B", "K", "M", "G", "T"};
    double value = (double)bytes_per_second;
    size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < sizeof(units) / sizeof(units[0])) {
        value /= 1024.0;
        unit++;
    }
    if (value >= 999.95 && unit + 1 < sizeof(units) / sizeof(units[0])) {
        value /= 1024.0;
        unit++;
    }
    if (unit == 0) snprintf(out, size, "%.0f%s", value, units[unit]);
    else snprintf(out, size, "%.1f%s", value, units[unit]);
}

static void init_styles(void)
{
    lv_style_init(&s_root_style);
    lv_style_set_bg_color(&s_root_style, color(COLOR_BG));
    lv_style_set_bg_opa(&s_root_style, LV_OPA_COVER);
    lv_style_set_text_color(&s_root_style, color(COLOR_TEXT));
    lv_style_set_text_font(&s_root_style, &app_font_14);

    lv_style_init(&s_surface_style);
    lv_style_set_bg_color(&s_surface_style, color(COLOR_SURFACE));
    lv_style_set_bg_opa(&s_surface_style, LV_OPA_COVER);
    lv_style_set_border_color(&s_surface_style, color(COLOR_LINE));
    lv_style_set_border_width(&s_surface_style, 1);
    lv_style_set_radius(&s_surface_style, 5);
    lv_style_set_pad_all(&s_surface_style, 0);

    lv_style_init(&s_button_style);
    lv_style_set_bg_color(&s_button_style, color(COLOR_SURFACE_ALT));
    lv_style_set_bg_opa(&s_button_style, LV_OPA_COVER);
    lv_style_set_border_width(&s_button_style, 0);
    lv_style_set_radius(&s_button_style, 4);
    lv_style_set_pad_all(&s_button_style, 0);

    lv_style_init(&s_muted_button_style);
    lv_style_set_bg_color(&s_muted_button_style, color(COLOR_SURFACE));
    lv_style_set_bg_opa(&s_muted_button_style, LV_OPA_COVER);
    lv_style_set_border_color(&s_muted_button_style, color(COLOR_LINE));
    lv_style_set_border_width(&s_muted_button_style, 1);
    lv_style_set_radius(&s_muted_button_style, 4);
    lv_style_set_pad_all(&s_muted_button_style, 0);

    for (size_t i = 0; i < sizeof(s_text_styles) / sizeof(s_text_styles[0]); ++i)
        lv_style_init(&s_text_styles[i]);
    lv_style_set_text_color(&s_text_styles[0], color(COLOR_TEXT));
    lv_style_set_text_color(&s_text_styles[1], color(COLOR_MUTED));
    lv_style_set_text_color(&s_text_styles[2], color(COLOR_GREEN));
    lv_style_set_text_color(&s_text_styles[3], color(COLOR_RED));
}

static void set_nav_button_state(void)
{
    if (s_ui.nas_nav_button == NULL) return;
    set_button_selected(s_ui.nas_nav_button, s_ui.active_page == 0);
    set_button_selected(s_ui.pve_nav_button, s_ui.active_page == 1);
    set_button_selected(s_ui.settings_nav_button, s_ui.active_page == 2);
}

static void apply_theme(void)
{
    lv_style_set_bg_color(&s_root_style, color(COLOR_BG));
    lv_style_set_text_color(&s_root_style, color(COLOR_TEXT));
    lv_style_set_bg_color(&s_surface_style, color(COLOR_SURFACE));
    lv_style_set_border_color(&s_surface_style, color(COLOR_LINE));
    lv_style_set_bg_color(&s_button_style, color(COLOR_SURFACE_ALT));
    lv_style_set_bg_color(&s_muted_button_style, color(COLOR_SURFACE));
    lv_style_set_border_color(&s_muted_button_style, color(COLOR_LINE));
    lv_style_set_text_color(&s_text_styles[0], color(COLOR_TEXT));
    lv_style_set_text_color(&s_text_styles[1], color(COLOR_MUTED));
    lv_style_set_text_color(&s_text_styles[2], color(COLOR_GREEN));
    lv_style_set_text_color(&s_text_styles[3], color(COLOR_RED));
    if (s_ui.top_bar != NULL)
        lv_obj_set_style_bg_color(s_ui.top_bar, color(COLOR_SURFACE), 0);
    for (size_t i = 0; i < PVE_RULE_COUNT; ++i) {
        if (s_ui.pve_rules[i] != NULL)
            lv_obj_set_style_bg_color(s_ui.pve_rules[i], color(COLOR_LINE), 0);
    }
    for (size_t i = 0; i < PVE_VM_LIST_RULE_COUNT; ++i) {
        if (s_ui.vm_list_rules[i] != NULL)
            lv_obj_set_style_bg_color(s_ui.vm_list_rules[i], color(COLOR_LINE), 0);
    }
    for (size_t i = 0; i < PVE_VM_DETAIL_RULE_COUNT; ++i) {
        if (s_ui.vm_detail_rules[i] != NULL)
            lv_obj_set_style_bg_color(s_ui.vm_detail_rules[i], color(COLOR_LINE), 0);
    }
    for (int i = 0; i < VM_VISIBLE; ++i) {
        if (s_ui.vm_name_buttons[i] != NULL)
            lv_obj_set_style_bg_color(s_ui.vm_name_buttons[i], color(COLOR_BLUE),
                                      LV_STATE_PRESSED);
    }
    set_nav_button_state();
    set_rotation_button_state();
    set_refresh_button_state();
    set_homepage_button_state();
    if (s_ui.theme_name != NULL) lv_label_set_text(s_ui.theme_name, palette()->name);
    if (s_ui.has_snapshot) {
        update_pve();
        update_nas();
    }
    if (s_ui.root != NULL) lv_obj_invalidate(s_ui.root);
}

static void page_event(lv_event_t *event)
{
    const int page = (int)(intptr_t)lv_event_get_user_data(event);
    s_ui.active_page = page;
    for (int i = 0; i < PAGE_COUNT; ++i) set_hidden(s_ui.pages[i], i != page);
    if (page == 1) show_pve_node();
    set_nav_button_state();
    app_model_set_active_monitor(page == 0 ? APP_MONITOR_NAS :
                                 page == 1 ? APP_MONITOR_PVE : APP_MONITOR_NONE);
}

static void set_pve_view(pve_view_t view)
{
    s_ui.pve_view = view;
    set_hidden(s_ui.pve_node_view, view != PVE_VIEW_NODE);
    set_hidden(s_ui.pve_vm_list, view != PVE_VIEW_VM_LIST);
    set_hidden(s_ui.vm_detail, view != PVE_VIEW_VM_DETAIL);
}

static void show_pve_node(void)
{
    s_ui.selected_vm_index = -1;
    set_pve_view(PVE_VIEW_NODE);
}

static void show_pve_vm_list(void)
{
    s_ui.selected_vm_index = -1;
    set_pve_view(PVE_VIEW_VM_LIST);
    if (s_ui.has_snapshot) update_pve();
}

static void update_vm_detail(void)
{
    if (s_ui.selected_vm_index < 0 ||
        s_ui.selected_vm_index >= (int)s_ui.snapshot.pve_guest_count) {
        show_pve_vm_list();
        return;
    }

    const pve_guest_t *guest = &s_ui.snapshot.pve_guests[s_ui.selected_vm_index];
    char value[96], used[24], total[24], uptime[32];
    lv_label_set_text_fmt(s_ui.vm_detail_title, "VM %" PRIu32 "  %s", guest->vmid, guest->name);
    lv_label_set_text(s_ui.vm_detail_status, guest->running ? "运行中" : "已停止");
    lv_obj_set_style_text_color(s_ui.vm_detail_status,
                                color(guest->running ? COLOR_GREEN : COLOR_GRAY), 0);
    lv_obj_set_style_bg_color(s_ui.vm_detail_status_dot,
                              color(guest->running ? COLOR_GREEN : COLOR_GRAY), 0);
    lv_label_set_text(s_ui.vm_detail_ip,
                      guest->ipv4_address[0] != '\0' ? guest->ipv4_address : "--");
    format_percent(value, sizeof(value), guest->cpu_percent);
    lv_label_set_text_fmt(s_ui.vm_detail_cpu, "%s / %" PRIu32 " 核",
                          value, guest->cpu_cores);
    if (guest->memory_total == 0) {
        lv_label_set_text(s_ui.vm_detail_memory, "--");
    } else {
        format_bytes(used, sizeof(used), guest->memory_used);
        format_bytes(total, sizeof(total), guest->memory_total);
        lv_label_set_text_fmt(s_ui.vm_detail_memory, "%s / %s", used, total);
    }
    if (guest->disk_total == 0) {
        lv_label_set_text(s_ui.vm_detail_disk, "--");
    } else {
        format_bytes(used, sizeof(used), guest->disk_used);
        format_bytes(total, sizeof(total), guest->disk_total);
        lv_label_set_text_fmt(s_ui.vm_detail_disk, "%s / %s", used, total);
    }
    format_uptime(uptime, sizeof(uptime), guest->uptime_seconds);
    lv_label_set_text(s_ui.vm_detail_uptime, uptime);
    lv_label_set_text(s_ui.vm_detail_agent, guest->guest_agent ? "在线" : "未报告");
    lv_obj_set_style_text_color(s_ui.vm_detail_agent,
                                color(guest->guest_agent ? COLOR_GREEN : COLOR_MUTED), 0);
}

static void show_vm_detail(size_t index)
{
    if (!s_ui.has_snapshot || index >= s_ui.snapshot.pve_guest_count) return;
    s_ui.selected_vm_index = (int)index;
    set_pve_view(PVE_VIEW_VM_DETAIL);
    update_vm_detail();
}

static void pve_vm_list_button_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) show_pve_vm_list();
}

static void vm_name_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    lv_event_stop_bubbling(event);
    show_vm_detail((size_t)(intptr_t)lv_event_get_user_data(event));
}

static void vm_list_return_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) show_pve_node();
}

static void vm_detail_return_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    lv_event_stop_bubbling(event);
    show_pve_vm_list();
}

static void vm_list_page_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    lv_event_stop_bubbling(event);
    const int direction = (int)(intptr_t)lv_event_get_user_data(event);
    if (direction < 0 && s_ui.vm_list_offset > 0) s_ui.vm_list_offset -= VM_VISIBLE;
    if (direction > 0 &&
        s_ui.vm_list_offset + VM_VISIBLE < (int)s_ui.snapshot.pve_guest_count)
        s_ui.vm_list_offset += VM_VISIBLE;
    update_pve();
}

static void vm_list_pager_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED)
        lv_event_stop_bubbling(event);
}

static void create_pve_page(lv_obj_t *page)
{
    s_ui.pve_node_view = lv_obj_create(page);
    lv_obj_remove_style_all(s_ui.pve_node_view);
    lv_obj_set_pos(s_ui.pve_node_view, 0, 0);
    lv_obj_set_size(s_ui.pve_node_view, WT32_LCD_WIDTH, 288);
    lv_obj_clear_flag(s_ui.pve_node_view, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *identity = make_surface(s_ui.pve_node_view, 8, 4, 464,
                                      PVE_IDENTITY_HEIGHT);
    s_ui.pve_status_dot = make_status_dot(identity, 10, 16, 8, COLOR_GRAY);
    s_ui.pve_name = make_label(identity, "p330", 24, 11, 68,
                               &app_font_14, COLOR_TEXT);
    s_ui.pve_rules[0] = make_rule(identity, 98, 8, 1, 24);
    s_ui.pve_version = make_label(identity, "version --", 106, 11, 96,
                                  &app_font_14, COLOR_TEXT);
    s_ui.pve_rules[1] = make_rule(identity, 208, 8, 1, 24);
    s_ui.pve_host = make_label(identity, "IP --", 216, 11, 140,
                               &app_font_14, COLOR_TEXT);
    s_ui.pve_rules[2] = make_rule(identity, 362, 8, 1, 24);
    s_ui.pve_uptime = make_label(identity, "运行 --", 370, 11, 82,
                                 &app_font_14, COLOR_TEXT);

    lv_obj_t *metrics = make_surface(
        s_ui.pve_node_view, 8, 48, 464,
        PVE_METRIC_ROW_HEIGHT * 2 + PVE_PROCESSOR_ROW_HEIGHT + 2);
    s_ui.pve_rules[3] = make_rule(metrics, PVE_NARROW_WIDTH, 0, 1,
                                  PVE_METRIC_ROW_HEIGHT * 2);
    s_ui.pve_rules[4] = make_rule(metrics, 0, PVE_METRIC_ROW_HEIGHT, 464, 1);
    s_ui.pve_rules[5] = make_rule(metrics, 0, PVE_METRIC_ROW_HEIGHT * 2, 464, 1);

    make_label(metrics, "CPU", 12, 8, 38, &app_font_14, COLOR_MUTED);
    s_ui.pve_cpu_value = make_label(metrics, "--", 56, 5, 100,
                                    &app_font_18, COLOR_TEXT);
    s_ui.pve_cpu_bar = make_split_bar(metrics, 56, 49, 96, 5, 0, 100);
    make_label(metrics, "系统负载", 180, 10, 70, &app_font_14, COLOR_MUTED);
    static const char *const load_labels[] = {"1分", "5分", "15分"};
    for (int i = 0; i < 3; ++i) {
        const int x = 252 + i * 66;
        lv_obj_t *label = make_label(metrics, load_labels[i], x, 7, 58,
                                     &app_font_14, COLOR_MUTED);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        s_ui.pve_load_values[i] = make_label(metrics, "--", x, 30, 58,
                                              &app_font_18, COLOR_TEXT);
        lv_obj_set_style_text_align(s_ui.pve_load_values[i], LV_TEXT_ALIGN_CENTER, 0);
    }

    make_label(metrics, "存储", 12, PVE_METRIC_ROW_HEIGHT + 10, 38,
               &app_font_14, COLOR_MUTED);
    s_ui.pve_storage_value = make_label(metrics, "--", 56,
                                        PVE_METRIC_ROW_HEIGHT + 8, 100,
                                        &app_font_14, COLOR_TEXT);
    s_ui.pve_storage_bar = make_split_bar(metrics, 56,
                                          PVE_METRIC_ROW_HEIGHT + 49,
                                          96, 5, 0, 100);
    make_label(metrics, "内存", 180, PVE_METRIC_ROW_HEIGHT + 10, 70,
               &app_font_14, COLOR_MUTED);
    s_ui.pve_memory_value = make_label(metrics, "--", 252,
                                       PVE_METRIC_ROW_HEIGHT + 8, 198,
                                       &app_font_18, COLOR_TEXT);
    s_ui.pve_memory_bar = make_split_bar(metrics, 252,
                                         PVE_METRIC_ROW_HEIGHT + 49,
                                         198, 5, 0, 100);

    const int processor_y = PVE_METRIC_ROW_HEIGHT * 2;
    make_label(metrics, "处理器", 12, processor_y + 13, 52,
               &app_font_14, COLOR_MUTED);
    s_ui.pve_cpu_cores_value = make_label(metrics, "-- 核", 72,
                                          processor_y + 12, 56,
                                          &app_font_14, COLOR_TEXT);
    s_ui.pve_cpu_model = make_label(metrics, "--", 136,
                                    processor_y + 13, 314,
                                    &app_font_14, COLOR_MUTED);
    s_ui.pve_vm_list_button = lv_btn_create(s_ui.pve_node_view);
    lv_obj_remove_style_all(s_ui.pve_vm_list_button);
    lv_obj_set_pos(s_ui.pve_vm_list_button, 8, PVE_VM_BUTTON_Y);
    lv_obj_set_size(s_ui.pve_vm_list_button, 464, PVE_VM_HIT_HEIGHT);
    lv_obj_add_event_cb(s_ui.pve_vm_list_button, pve_vm_list_button_event,
                        LV_EVENT_CLICKED, NULL);
    s_ui.pve_vm_list_visual = lv_obj_create(s_ui.pve_vm_list_button);
    lv_obj_remove_style_all(s_ui.pve_vm_list_visual);
    lv_obj_add_style(s_ui.pve_vm_list_visual, &s_button_style, 0);
    lv_obj_set_pos(s_ui.pve_vm_list_visual, 0,
                   (PVE_VM_HIT_HEIGHT - PVE_VM_VISUAL_HEIGHT) / 2);
    lv_obj_set_size(s_ui.pve_vm_list_visual, 464, PVE_VM_VISUAL_HEIGHT);
    lv_obj_clear_flag(s_ui.pve_vm_list_visual, LV_OBJ_FLAG_CLICKABLE);
    s_ui.pve_vm_list_label = make_label(s_ui.pve_vm_list_visual, "虚拟机列表",
                                        0, 0, 464, &app_font_14, COLOR_TEXT);
    lv_obj_center(s_ui.pve_vm_list_label);
    lv_obj_set_style_text_align(s_ui.pve_vm_list_label, LV_TEXT_ALIGN_CENTER, 0);

    s_ui.pve_vm_list = make_surface(page, 8, 4, 464, 276);
    lv_obj_add_flag(s_ui.pve_vm_list, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ui.pve_vm_list, vm_list_return_event, LV_EVENT_CLICKED, NULL);
    s_ui.vm_list_rules[0] = make_rule(s_ui.pve_vm_list, 166, 0, 1, 276);
    s_ui.vm_list_rules[1] = make_rule(s_ui.pve_vm_list, 280, 0, 1, 276);
    s_ui.vm_list_rules[2] = make_rule(s_ui.pve_vm_list, 328, 0, 1, 276);
    s_ui.vm_list_rules[3] = make_rule(s_ui.pve_vm_list, 420, 0, 1, 276);
    s_ui.vm_list_rules[4] = make_rule(s_ui.pve_vm_list, 0, 30, 420, 1);
    s_ui.vm_list_rules[5] = make_rule(s_ui.pve_vm_list, 0, 88, 420, 1);
    s_ui.vm_list_rules[6] = make_rule(s_ui.pve_vm_list, 0, 146, 420, 1);
    s_ui.vm_list_rules[7] = make_rule(s_ui.pve_vm_list, 0, 204, 420, 1);
    s_ui.vm_list_rules[8] = make_rule(s_ui.pve_vm_list, 0, 262, 420, 1);
    s_ui.vm_title = make_label(s_ui.pve_vm_list, "虚拟机", 8, 6, 150,
                               &app_font_14, COLOR_TEXT);
    s_ui.vm_header_ip = make_label(s_ui.pve_vm_list, "IP", 174, 6, 98,
                                   &app_font_14, COLOR_MUTED);
    s_ui.vm_header_cpu = make_label(s_ui.pve_vm_list, "CPU", 288, 6, 32,
                                    &app_font_14, COLOR_MUTED);
    s_ui.vm_header_memory = make_label(s_ui.pve_vm_list, "内存", 336, 6, 76,
                                       &app_font_14, COLOR_MUTED);
    s_ui.vm_list_empty = make_label(s_ui.pve_vm_list, "未发现虚拟机", 12, 126, 396,
                                    &app_font_14, COLOR_MUTED);
    lv_obj_set_style_text_align(s_ui.vm_list_empty, LV_TEXT_ALIGN_CENTER, 0);
    s_ui.vm_list_pager = lv_obj_create(s_ui.pve_vm_list);
    lv_obj_remove_style_all(s_ui.vm_list_pager);
    lv_obj_set_pos(s_ui.vm_list_pager, 420, 0);
    lv_obj_set_size(s_ui.vm_list_pager, 44, 276);
    lv_obj_clear_flag(s_ui.vm_list_pager, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_ui.vm_list_pager, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_ui.vm_list_pager, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(s_ui.vm_list_pager, vm_list_pager_event, LV_EVENT_CLICKED, NULL);
    s_ui.vm_list_previous = make_button(s_ui.vm_list_pager, "^", 4, 0, 36, 102,
                                        vm_list_page_event, (void *)(intptr_t)-1);
    s_ui.vm_list_page = make_label(s_ui.vm_list_pager, "1/1", 4, 125, 36,
                                   &app_font_14, COLOR_MUTED);
    lv_obj_set_style_text_align(s_ui.vm_list_page, LV_TEXT_ALIGN_CENTER, 0);
    s_ui.vm_list_next = make_button(s_ui.vm_list_pager, "v", 4, 154, 36, 122,
                                    vm_list_page_event, (void *)(intptr_t)1);
    lv_obj_add_flag(s_ui.vm_list_previous, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(s_ui.vm_list_next, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_opa(s_ui.vm_list_previous, LV_OPA_30, LV_STATE_DISABLED);
    lv_obj_set_style_opa(s_ui.vm_list_next, LV_OPA_30, LV_STATE_DISABLED);
    for (int i = 0; i < VM_VISIBLE; ++i) {
        const int y = 30 + i * 58;
        s_ui.vm_rows[i] = lv_obj_create(s_ui.pve_vm_list);
        lv_obj_remove_style_all(s_ui.vm_rows[i]);
        lv_obj_set_pos(s_ui.vm_rows[i], 0, y);
        lv_obj_set_size(s_ui.vm_rows[i], 420, 58);
        lv_obj_clear_flag(s_ui.vm_rows[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(s_ui.vm_rows[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(s_ui.vm_rows[i], vm_list_return_event, LV_EVENT_CLICKED, NULL);
        s_ui.vm_name_buttons[i] = lv_btn_create(s_ui.vm_rows[i]);
        lv_obj_remove_style_all(s_ui.vm_name_buttons[i]);
        lv_obj_set_pos(s_ui.vm_name_buttons[i], 0, 0);
        lv_obj_set_size(s_ui.vm_name_buttons[i], 166, 58);
        lv_obj_set_style_bg_color(s_ui.vm_name_buttons[i], color(COLOR_BLUE),
                                  LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(s_ui.vm_name_buttons[i], LV_OPA_30,
                                LV_STATE_PRESSED);
        lv_obj_add_flag(s_ui.vm_name_buttons[i], LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_event_cb(s_ui.vm_name_buttons[i], vm_name_event, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
        s_ui.vm_row_dots[i] = make_status_dot(s_ui.vm_name_buttons[i], 8, 25, 8,
                                               COLOR_GRAY);
        lv_obj_clear_flag(s_ui.vm_row_dots[i], LV_OBJ_FLAG_CLICKABLE);
        s_ui.vm_row_labels[i] = make_label(s_ui.vm_name_buttons[i], "", 22, 20, 122,
                                            &app_font_14, COLOR_TEXT);
        make_label(s_ui.vm_name_buttons[i], ">", 150, 20, 12, &app_font_14, COLOR_MUTED);
        s_ui.vm_row_ips[i] = make_label(s_ui.vm_rows[i], "--", 174, 20, 98,
                                        &app_font_14, COLOR_MUTED);
        s_ui.vm_row_cpu[i] = make_label(s_ui.vm_rows[i], "--", 288, 20, 32,
                                        &app_font_14, COLOR_MUTED);
        s_ui.vm_row_memory[i] = make_label(s_ui.vm_rows[i], "--", 336, 20, 76,
                                           &app_font_14, COLOR_MUTED);
        set_hidden(s_ui.vm_rows[i], true);
    }

    s_ui.vm_detail = make_surface(page, 8, 4, 464, 276);
    lv_obj_add_flag(s_ui.vm_detail, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ui.vm_detail, vm_detail_return_event, LV_EVENT_CLICKED, NULL);
    s_ui.vm_detail_rules[0] = make_rule(s_ui.vm_detail, 0, 54, 464, 1);
    s_ui.vm_detail_rules[1] = make_rule(s_ui.vm_detail, 231, 54, 1, 222);
    s_ui.vm_detail_rules[2] = make_rule(s_ui.vm_detail, 0, 127, 464, 1);
    s_ui.vm_detail_rules[3] = make_rule(s_ui.vm_detail, 0, 200, 464, 1);
    lv_obj_t *vm_detail_back = make_button(s_ui.vm_detail, "<", 8, 12, 30, 30,
                                           vm_detail_return_event, NULL);
    lv_obj_add_flag(vm_detail_back, LV_OBJ_FLAG_EVENT_BUBBLE);
    s_ui.vm_detail_title = make_label(s_ui.vm_detail, "VM --", 48, 17, 296,
                                      &app_font_18, COLOR_TEXT);
    s_ui.vm_detail_status_dot = make_status_dot(s_ui.vm_detail, 356, 23, 8, COLOR_GRAY);
    lv_obj_clear_flag(s_ui.vm_detail_status_dot, LV_OBJ_FLAG_CLICKABLE);
    s_ui.vm_detail_status = make_label(s_ui.vm_detail, "--", 372, 18, 76,
                                       &app_font_14, COLOR_MUTED);
    make_label(s_ui.vm_detail, "IP", 18, 67, 195, &app_font_14, COLOR_MUTED);
    s_ui.vm_detail_ip = make_label(s_ui.vm_detail, "--", 18, 91, 195,
                                   &app_font_18, COLOR_TEXT);
    make_label(s_ui.vm_detail, "CPU", 249, 67, 195, &app_font_14, COLOR_MUTED);
    s_ui.vm_detail_cpu = make_label(s_ui.vm_detail, "--", 249, 91, 195,
                                    &app_font_18, COLOR_TEXT);
    make_label(s_ui.vm_detail, "内存", 18, 140, 195, &app_font_14, COLOR_MUTED);
    s_ui.vm_detail_memory = make_label(s_ui.vm_detail, "--", 18, 164, 195,
                                       &app_font_18, COLOR_TEXT);
    make_label(s_ui.vm_detail, "磁盘", 249, 140, 195, &app_font_14, COLOR_MUTED);
    s_ui.vm_detail_disk = make_label(s_ui.vm_detail, "--", 249, 164, 195,
                                     &app_font_18, COLOR_TEXT);
    make_label(s_ui.vm_detail, "运行时间", 18, 213, 195, &app_font_14, COLOR_MUTED);
    s_ui.vm_detail_uptime = make_label(s_ui.vm_detail, "--", 18, 237, 195,
                                       &app_font_14, COLOR_TEXT);
    make_label(s_ui.vm_detail, "Guest Agent", 249, 213, 195,
               &app_font_14, COLOR_MUTED);
    s_ui.vm_detail_agent = make_label(s_ui.vm_detail, "--", 249, 237, 195,
                                      &app_font_18, COLOR_MUTED);
    show_pve_node();
}

static void show_nas_pools(void)
{
    s_ui.nas_disks_visible = false;
    set_hidden(s_ui.nas_overview, false);
    set_hidden(s_ui.nas_detail, true);
}

static void show_nas_disks(void)
{
    s_ui.nas_disks_visible = true;
    s_ui.nas_disk_sort_key = NAS_DISK_SORT_ID;
    s_ui.nas_disk_sort_descending = false;
    s_ui.nas_disk_offset = 0;
    set_hidden(s_ui.nas_overview, true);
    set_hidden(s_ui.nas_detail, false);
    update_nas_disks();
}

static void nas_pool_list_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) show_nas_disks();
}

static void nas_pool_page_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    lv_event_stop_bubbling(event);
    const int direction = (int)(intptr_t)lv_event_get_user_data(event);
    const size_t count = s_ui.snapshot.nas_pool_count;
    if (direction < 0 && s_ui.nas_pool_offset >= NAS_VISIBLE) {
        s_ui.nas_pool_offset -= NAS_VISIBLE;
    } else if (direction > 0 && s_ui.nas_pool_offset + NAS_VISIBLE < count) {
        s_ui.nas_pool_offset += NAS_VISIBLE;
    }
    update_nas();
}

static void nas_disk_list_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    lv_obj_t *target = lv_event_get_target(event);
    if (target == s_ui.nas_disk_previous || target == s_ui.nas_disk_next) return;
    show_nas_pools();
}

static void update_nas_disk_sort_buttons(void)
{
    static const char *const names[] = {"硬盘", "型号", "温度"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        lv_obj_t *button = s_ui.nas_disk_sort_buttons[i];
        if (button == NULL) continue;
        lv_obj_t *label = lv_obj_get_child(button, 0);
        const bool active = (nas_disk_sort_key_t)i == s_ui.nas_disk_sort_key;
        if (active)
            lv_label_set_text_fmt(label, "%s %s", names[i],
                                  s_ui.nas_disk_sort_descending ? "v" : "^");
        else
            lv_label_set_text(label, names[i]);
        set_button_selected(button, active);
    }
}

static void nas_disk_sort_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    lv_event_stop_bubbling(event);
    const nas_disk_sort_key_t key =
        (nas_disk_sort_key_t)(intptr_t)lv_event_get_user_data(event);
    if (key == s_ui.nas_disk_sort_key)
        s_ui.nas_disk_sort_descending = !s_ui.nas_disk_sort_descending;
    else {
        s_ui.nas_disk_sort_key = key;
        s_ui.nas_disk_sort_descending = false;
    }
    s_ui.nas_disk_offset = 0;
    update_nas_disks();
}

static void nas_disk_page_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    lv_event_stop_bubbling(event);
    const int direction = (int)(intptr_t)lv_event_get_user_data(event);
    const size_t snapshot_disk_count = s_ui.snapshot.nas_disk_count;
    if (direction < 0 && s_ui.nas_disk_offset >= NAS_DISK_VISIBLE) {
        s_ui.nas_disk_offset -= NAS_DISK_VISIBLE;
    } else if (direction > 0 &&
               s_ui.nas_disk_offset + NAS_DISK_VISIBLE < snapshot_disk_count) {
        s_ui.nas_disk_offset += NAS_DISK_VISIBLE;
    }
    update_nas_disks();
}

static void nas_disk_pager_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED)
        lv_event_stop_bubbling(event);
}

static void update_nas_disks(void)
{
    app_snapshot_t *snapshot = &s_ui.snapshot;
    const bool nas_live = snapshot->nas_online;
    char value[96];
    update_nas_disk_sort_buttons();
    if (!nas_live) {
        lv_label_set_text(s_ui.nas_disk_empty, "NAS 离线");
        set_hidden(s_ui.nas_disk_empty, false);
        set_hidden(s_ui.nas_disk_previous, true);
        set_hidden(s_ui.nas_disk_next, true);
        set_hidden(s_ui.nas_disk_page, true);
        s_ui.nas_disk_offset = 0;
        for (int i = 0; i < NAS_DISK_VISIBLE; ++i) set_hidden(s_ui.nas_disk_rows[i], true);
        return;
    }

    const size_t snapshot_disk_count = snapshot->nas_disk_count;
    nas_disk_sort(snapshot->nas_disks, snapshot_disk_count,
                  s_ui.nas_disk_sort_key, s_ui.nas_disk_sort_descending);
    const size_t last_page_offset = snapshot_disk_count > NAS_DISK_VISIBLE ?
        ((snapshot_disk_count - 1) / NAS_DISK_VISIBLE) * NAS_DISK_VISIBLE : 0;
    if (s_ui.nas_disk_offset > last_page_offset) s_ui.nas_disk_offset = last_page_offset;
    const size_t remaining_disks = snapshot_disk_count - s_ui.nas_disk_offset;
    const size_t visible_disks = remaining_disks < NAS_DISK_VISIBLE ?
                              remaining_disks : NAS_DISK_VISIBLE;
    const size_t page_count = snapshot_disk_count > 0 ?
                           (snapshot_disk_count + NAS_DISK_VISIBLE - 1) / NAS_DISK_VISIBLE : 1;
    set_hidden(s_ui.nas_disk_empty, visible_disks > 0);
    if (visible_disks == 0) lv_label_set_text(s_ui.nas_disk_empty, "未获取到物理盘信息");
    lv_label_set_text_fmt(s_ui.nas_disk_page, "%zu/%zu",
                          s_ui.nas_disk_offset / NAS_DISK_VISIBLE + 1, page_count);
    const bool has_disks = snapshot_disk_count > 0;
    set_hidden(s_ui.nas_disk_page, !has_disks);
    set_hidden(s_ui.nas_disk_previous, !has_disks);
    set_hidden(s_ui.nas_disk_next, !has_disks);
    if (s_ui.nas_disk_offset == 0)
        lv_obj_add_state(s_ui.nas_disk_previous, LV_STATE_DISABLED);
    else
        lv_obj_clear_state(s_ui.nas_disk_previous, LV_STATE_DISABLED);
    if (s_ui.nas_disk_offset + NAS_DISK_VISIBLE >= snapshot_disk_count)
        lv_obj_add_state(s_ui.nas_disk_next, LV_STATE_DISABLED);
    else
        lv_obj_clear_state(s_ui.nas_disk_next, LV_STATE_DISABLED);
    for (int i = 0; i < NAS_DISK_VISIBLE; ++i) {
        const bool visible = (size_t)i < visible_disks;
        set_hidden(s_ui.nas_disk_rows[i], !visible);
        if (!visible) continue;
        const nas_disk_t *disk = &snapshot->nas_disks[s_ui.nas_disk_offset + i];
        lv_obj_set_style_bg_color(s_ui.nas_disk_dots[i],
                                  color(disk->healthy ?
                                        COLOR_GREEN : COLOR_RED), 0);
        lv_label_set_text(s_ui.nas_disk_ids[i], disk->id[0] ? disk->id : "--");
        lv_label_set_text(s_ui.nas_disk_models[i], disk->model[0] ? disk->model : "--");
        if (disk->temperature_valid) {
            snprintf(value, sizeof(value), "%d°C", disk->temperature_c);
            lv_label_set_text(s_ui.nas_disk_temperatures[i], value);
        } else {
            lv_label_set_text(s_ui.nas_disk_temperatures[i], "--");
        }
    }
}

static void create_nas_page(lv_obj_t *page)
{
    s_ui.nas_overview = lv_obj_create(page);
    lv_obj_remove_style_all(s_ui.nas_overview);
    lv_obj_set_pos(s_ui.nas_overview, 0, 0);
    lv_obj_set_size(s_ui.nas_overview, 480, NAS_FOOTER_Y);
    lv_obj_clear_flag(s_ui.nas_overview, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_ui.nas_overview, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ui.nas_overview, nas_pool_list_event, LV_EVENT_CLICKED, NULL);
    s_ui.nas_overview_message = make_label(s_ui.nas_overview, "数据加载中", 16, 92,
                                            448, &app_font_14, COLOR_MUTED);
    lv_obj_set_style_text_align(s_ui.nas_overview_message, LV_TEXT_ALIGN_CENTER, 0);
    for (int i = 0; i < NAS_VISIBLE; ++i) {
        s_ui.nas_rows[i] = lv_btn_create(s_ui.nas_overview);
        lv_obj_remove_style_all(s_ui.nas_rows[i]);
        lv_obj_add_style(s_ui.nas_rows[i], &s_muted_button_style, 0);
        lv_obj_set_pos(s_ui.nas_rows[i], 8, 4 + i * NAS_POOL_ROW_STEP);
        lv_obj_set_size(s_ui.nas_rows[i], NAS_POOL_ROW_WIDTH_PAGED,
                        NAS_POOL_ROW_HEIGHT);
        lv_obj_add_event_cb(s_ui.nas_rows[i], nas_pool_list_event, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);
        s_ui.nas_row_status_dots[i] = make_status_dot(s_ui.nas_rows[i], 9,
                                                       NAS_POOL_STATUS_Y, 8,
                                                       COLOR_RED);
        make_icon(s_ui.nas_rows[i], &dashboard_icon_pool, 22, NAS_POOL_ICON_Y,
                  palette()->muted);
        s_ui.nas_row_labels[i] = make_label(s_ui.nas_rows[i], "", 42,
                                             NAS_POOL_TEXT_Y, 82,
                                             &app_font_14, COLOR_TEXT);
        s_ui.nas_row_descriptions[i] = make_label(s_ui.nas_rows[i], "", 126,
                                                   NAS_POOL_TEXT_Y, 70,
                                                   &app_font_14, COLOR_MUTED);
        s_ui.nas_row_values[i] = make_label(s_ui.nas_rows[i], "", 198,
                                             NAS_POOL_TEXT_Y, 212,
                                             &app_font_14, COLOR_TEXT);
        lv_obj_set_style_text_align(s_ui.nas_row_values[i], LV_TEXT_ALIGN_RIGHT, 0);
        s_ui.nas_row_bars[i] = make_split_bar(s_ui.nas_rows[i], 9, NAS_POOL_BAR_Y,
                                              NAS_POOL_BAR_WIDTH_PAGED, 6, 0, 100);
        lv_obj_clear_flag(s_ui.nas_row_status_dots[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(s_ui.nas_row_bars[i], LV_OBJ_FLAG_CLICKABLE);
        set_hidden(s_ui.nas_rows[i], true);
    }
    s_ui.nas_pool_previous = make_button(s_ui.nas_overview, "^", 436, 4, 36, 82,
                                         nas_pool_page_event, (void *)(intptr_t)-1);
    s_ui.nas_pool_page = make_label(s_ui.nas_overview, "1/1", 436, 98, 36,
                                    &app_font_14, COLOR_MUTED);
    lv_obj_set_style_text_align(s_ui.nas_pool_page, LV_TEXT_ALIGN_CENTER, 0);
    s_ui.nas_pool_next = make_button(s_ui.nas_overview, "v", 436, 124, 36, 98,
                                     nas_pool_page_event, (void *)(intptr_t)1);
    set_hidden(s_ui.nas_pool_previous, true);
    set_hidden(s_ui.nas_pool_page, true);
    set_hidden(s_ui.nas_pool_next, true);

    s_ui.nas_detail = make_surface(page, 8, 4, 464, 228);
    lv_obj_add_flag(s_ui.nas_detail, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ui.nas_detail, nas_disk_list_event, LV_EVENT_CLICKED, NULL);
    s_ui.nas_disk_pager = lv_obj_create(s_ui.nas_detail);
    lv_obj_remove_style_all(s_ui.nas_disk_pager);
    lv_obj_set_pos(s_ui.nas_disk_pager, 420, 0);
    lv_obj_set_size(s_ui.nas_disk_pager, 44, 228);
    lv_obj_clear_flag(s_ui.nas_disk_pager, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_ui.nas_disk_pager, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ui.nas_disk_pager, nas_disk_pager_event,
                        LV_EVENT_CLICKED, NULL);
    s_ui.nas_disk_previous = make_button(s_ui.nas_disk_pager, "^", 4, 0, 36, 82,
                                         nas_disk_page_event, (void *)(intptr_t)-1);
    s_ui.nas_disk_page = make_label(s_ui.nas_disk_pager, "1/1", 4, 94, 36,
                                    &app_font_14, COLOR_MUTED);
    lv_obj_set_style_text_align(s_ui.nas_disk_page, LV_TEXT_ALIGN_CENTER, 0);
    s_ui.nas_disk_next = make_button(s_ui.nas_disk_pager, "v", 4, 120, 36, 96,
                                     nas_disk_page_event, (void *)(intptr_t)1);
    lv_obj_set_style_opa(s_ui.nas_disk_previous, LV_OPA_30, LV_STATE_DISABLED);
    lv_obj_set_style_opa(s_ui.nas_disk_next, LV_OPA_30, LV_STATE_DISABLED);
    set_hidden(s_ui.nas_disk_previous, true);
    set_hidden(s_ui.nas_disk_next, true);
    set_hidden(s_ui.nas_disk_page, true);
    s_ui.nas_disk_sort_buttons[NAS_DISK_SORT_ID] = make_button(
        s_ui.nas_detail, "硬盘", 22, 4, 72, 26, nas_disk_sort_event,
        (void *)(intptr_t)NAS_DISK_SORT_ID);
    s_ui.nas_disk_sort_buttons[NAS_DISK_SORT_MODEL] = make_button(
        s_ui.nas_detail, "型号", 102, 4, 208, 26, nas_disk_sort_event,
        (void *)(intptr_t)NAS_DISK_SORT_MODEL);
    s_ui.nas_disk_sort_buttons[NAS_DISK_SORT_TEMPERATURE] = make_button(
        s_ui.nas_detail, "温度", 318, 4, 86, 26, nas_disk_sort_event,
        (void *)(intptr_t)NAS_DISK_SORT_TEMPERATURE);
    s_ui.nas_disk_empty = make_label(s_ui.nas_detail, "未获取到物理盘信息", 12, 96,
                                      440, &app_font_14, COLOR_MUTED);
    lv_obj_set_style_text_align(s_ui.nas_disk_empty, LV_TEXT_ALIGN_CENTER, 0);
    for (int i = 0; i < NAS_DISK_VISIBLE; ++i) {
        lv_obj_t *row = lv_obj_create(s_ui.nas_detail);
        s_ui.nas_disk_rows[i] = row;
        lv_obj_remove_style_all(row);
        lv_obj_set_pos(row, 8, 34 + i * 46);
        lv_obj_set_size(row, 412, 46);
        lv_obj_set_style_border_color(row, color(COLOR_LINE), 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, nas_disk_list_event, LV_EVENT_CLICKED, NULL);
        s_ui.nas_disk_dots[i] = make_status_dot(row, 3, 19, 7, COLOR_RED);
        make_icon(row, &dashboard_icon_hdd, 14, 15, palette()->muted);
        s_ui.nas_disk_ids[i] = make_label(row, "--", 34, 14, 60, &app_font_14, COLOR_TEXT);
        s_ui.nas_disk_models[i] = make_label(row, "--", 94, 14, 216, &app_font_14, COLOR_TEXT);
        s_ui.nas_disk_temperatures[i] = make_label(row, "--", 310, 14, 86,
                                                    &app_font_14, COLOR_TEXT);
        lv_obj_clear_flag(s_ui.nas_disk_dots[i], LV_OBJ_FLAG_CLICKABLE);
        set_hidden(row, true);
    }
    set_hidden(s_ui.nas_detail, true);

    lv_obj_t *footer = make_surface(page, 8, NAS_FOOTER_Y, 464, NAS_METRICS_HEIGHT);
    const int ip_x = 8;
    const int uptime_x = ip_x + NAS_FOOTER_IP_WIDTH + 2;
    const int divider_x = uptime_x + NAS_FOOTER_UPTIME_WIDTH + 2;
    const int dynamic_x = divider_x + 3;
    make_icon(footer, &dashboard_icon_globe, ip_x, NAS_FOOTER_STATIC_Y,
              palette()->muted);
    s_ui.nas_ip_value = make_label(footer, "--", ip_x + 19, NAS_FOOTER_STATIC_Y,
                                   NAS_FOOTER_IP_WIDTH - 21, &app_font_14, COLOR_TEXT);
    make_icon(footer, &dashboard_icon_uptime, uptime_x, NAS_FOOTER_STATIC_Y,
              palette()->muted);
    s_ui.nas_uptime_value = make_label(footer, "--", uptime_x + 18,
                                       NAS_FOOTER_STATIC_Y,
                                       NAS_FOOTER_UPTIME_WIDTH - 18,
                                       &app_font_14, COLOR_TEXT);
    make_rule(footer, divider_x, 5, 1, 42);
    make_icon(footer, &dashboard_icon_processor, dynamic_x + 2, 18, palette()->muted);
    s_ui.nas_cpu_value = make_label(footer, "--", dynamic_x + 20, 17, 34,
                                    &app_font_14, COLOR_TEXT);
    make_icon(footer, &dashboard_icon_memory, dynamic_x + NAS_METRIC_CELL_WIDTH + 2,
              18, palette()->muted);
    s_ui.nas_memory_value = make_label(footer, "--",
                                       dynamic_x + NAS_METRIC_CELL_WIDTH + 20, 17, 34,
                                       &app_font_14, COLOR_TEXT);
    make_rule(footer, dynamic_x + NAS_METRIC_CELL_WIDTH, 5, 1, 42);
    make_icon(footer, &dashboard_icon_temperature,
              dynamic_x + NAS_METRIC_CELL_WIDTH * 2 + 2, 18, palette()->muted);
    s_ui.nas_temperature_value = make_label(footer, "--",
                                            dynamic_x + NAS_METRIC_CELL_WIDTH * 2 + 20,
                                            17, 34, &app_font_14, COLOR_TEXT);
    make_rule(footer, dynamic_x + NAS_METRIC_CELL_WIDTH * 2, 5, 1, 42);
    make_rule(footer, dynamic_x + NAS_METRIC_CELL_WIDTH * 3, 5, 1, 42);
    const int network_x = dynamic_x + NAS_METRIC_CELL_WIDTH * 3;
    s_ui.nas_upload_value = make_network_metric_row(
        footer, &dashboard_icon_upload, network_x, 0);
    s_ui.nas_download_value = make_network_metric_row(
        footer, &dashboard_icon_download, network_x, 26);
    lv_obj_set_style_text_align(s_ui.nas_cpu_value, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_align(s_ui.nas_memory_value, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_align(s_ui.nas_temperature_value, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_align(s_ui.nas_upload_value, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_align(s_ui.nas_download_value, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_align(s_ui.nas_ip_value, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_align(s_ui.nas_uptime_value, LV_TEXT_ALIGN_LEFT, 0);
    update_nas_disk_sort_buttons();
    show_nas_pools();
}

static void set_nas_pool_pager_layout(bool paged)
{
    const int row_width = paged ? NAS_POOL_ROW_WIDTH_PAGED : NAS_POOL_ROW_WIDTH_FULL;
    const int bar_width = paged ? NAS_POOL_BAR_WIDTH_PAGED : NAS_POOL_BAR_WIDTH_FULL;
    const int value_width = row_width - 208;
    for (int i = 0; i < NAS_VISIBLE; ++i) {
        lv_obj_set_width(s_ui.nas_rows[i], row_width);
        lv_obj_set_width(s_ui.nas_row_bars[i], bar_width);
        lv_obj_set_width(s_ui.nas_row_values[i], value_width);
    }
}

static void set_rotation_button_state(void)
{
    const bool rotated = wt32_board_get_rotation_180();
    set_button_selected(s_ui.rotation_0_button, !rotated);
    set_button_selected(s_ui.rotation_180_button, rotated);
}

static void rotation_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    const bool enabled = (bool)(intptr_t)lv_event_get_user_data(event);
    if (wt32_board_set_rotation_180(enabled) == ESP_OK &&
        device_settings_set_u8(DEVICE_KEY_ROTATE_180, enabled ? 1 : 0) == ESP_OK) {
        set_rotation_button_state();
        lv_obj_invalidate(lv_scr_act());
    }
}

static void brightness_event(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    const uint8_t value = (uint8_t)lv_slider_get_value(s_ui.brightness_slider);
    if (code == LV_EVENT_VALUE_CHANGED) {
        wt32_board_set_brightness(value);
        lv_label_set_text_fmt(s_ui.brightness_value, "%u%%", value);
    } else if (code == LV_EVENT_RELEASED) {
        device_settings_set_u8(DEVICE_KEY_BRIGHTNESS, value);
    }
}

static void scan_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || s_ui.scan_pending) return;
    if (network_manager_start_scan() == ESP_OK) {
        s_ui.scan_pending = true;
        lv_label_set_text(s_ui.wifi_status, "正在扫描 Wi-Fi...");
    }
}

static void sanitize_dynamic_text(const char *source, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) return;
    size_t used = 0;
    const unsigned char *cursor = (const unsigned char *)(source != NULL ? source : "");
    while (*cursor != '\0' && used + 1 < out_size) {
        if (*cursor < 0x80) {
            out[used++] = (*cursor == '\n' || *cursor == '\r') ? ' ' : (char)*cursor;
            cursor++;
            continue;
        }
        out[used++] = '?';
        if ((*cursor & 0xE0) == 0xC0) cursor += cursor[1] != '\0' ? 2 : 1;
        else if ((*cursor & 0xF0) == 0xE0)
            cursor += cursor[1] != '\0' && cursor[2] != '\0' ? 3 : 1;
        else if ((*cursor & 0xF8) == 0xF0)
            cursor += cursor[1] != '\0' && cursor[2] != '\0' && cursor[3] != '\0' ? 4 : 1;
        else cursor++;
    }
    out[used] = '\0';
}

static void close_password_overlay(void)
{
    if (s_ui.password_overlay == NULL) return;
    lv_obj_t *overlay = s_ui.password_overlay;
    s_ui.password_overlay = NULL;
    s_ui.password_textarea = NULL;
    lv_obj_del_async(overlay);
}

static void keyboard_event(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_CANCEL) {
        close_password_overlay();
        return;
    }
    if (code != LV_EVENT_READY || s_ui.password_textarea == NULL) return;
    const uint16_t selected = lv_dropdown_get_selected(s_ui.wifi_dropdown);
    if (selected >= s_ui.scan_count) {
        close_password_overlay();
        return;
    }
    const char *ssid = s_ui.scan_records[selected].ssid;
    const char *password = lv_textarea_get_text(s_ui.password_textarea);
    esp_err_t err = network_manager_configure_wifi(ssid, password != NULL ? password : "");
    if (err == ESP_OK) lv_label_set_text(s_ui.wifi_status, "正在连接 Wi-Fi...");
    else lv_label_set_text(s_ui.wifi_status, "Wi-Fi 配置无效");
    close_password_overlay();
}

static void password_cancel_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) close_password_overlay();
}

static void wifi_connect_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || s_ui.password_overlay != NULL) return;
    char ssid[33] = {0};
    lv_dropdown_get_selected_str(s_ui.wifi_dropdown, ssid, sizeof(ssid));
    const uint16_t selected = lv_dropdown_get_selected(s_ui.wifi_dropdown);
    if (selected >= s_ui.scan_count || ssid[0] == '\0' || strcmp(ssid, "请先扫描 Wi-Fi") == 0) {
        lv_label_set_text(s_ui.wifi_status, "请先扫描并选择 Wi-Fi");
        return;
    }
    lv_obj_t *overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, WT32_LCD_WIDTH, WT32_LCD_HEIGHT);
    lv_obj_set_style_bg_color(overlay, color(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_70, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *dialog = make_surface(overlay, 24, 16, 432, 288);
    make_label(dialog, ssid, 14, 10, 260, &app_font_18, COLOR_TEXT);
    s_ui.password_textarea = lv_textarea_create(dialog);
    lv_obj_set_pos(s_ui.password_textarea, 14, 42);
    lv_obj_set_size(s_ui.password_textarea, 300, 38);
    lv_textarea_set_password_mode(s_ui.password_textarea, true);
    lv_textarea_set_one_line(s_ui.password_textarea, true);
    lv_textarea_set_max_length(s_ui.password_textarea, 64);
    lv_textarea_set_placeholder_text(s_ui.password_textarea, "Wi-Fi 密码");
    make_button(dialog, "取消", 324, 42, 92, 38, password_cancel_event, NULL);
    lv_obj_t *keyboard = lv_keyboard_create(dialog);
    lv_obj_set_pos(keyboard, 8, 88);
    lv_obj_set_size(keyboard, 416, 192);
    lv_keyboard_set_textarea(keyboard, s_ui.password_textarea);
    lv_obj_add_event_cb(keyboard, keyboard_event, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(keyboard, keyboard_event, LV_EVENT_CANCEL, NULL);
    s_ui.password_overlay = overlay;
}

static void close_portal_overlay(bool stop_portal)
{
    if (stop_portal) network_manager_stop_pve_portal();
    if (s_ui.portal_overlay == NULL) return;
    lv_obj_t *overlay = s_ui.portal_overlay;
    s_ui.portal_overlay = NULL;
    s_ui.portal_status_label = NULL;
    lv_obj_del_async(overlay);
}

static void portal_cancel_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) close_portal_overlay(true);
}

static void token_portal_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || s_ui.portal_overlay != NULL) return;
    esp_err_t err = network_manager_start_pve_portal();
    if (err != ESP_OK) {
        lv_label_set_text(s_ui.wifi_status, "配置 AP 启动失败");
        return;
    }
    network_portal_status_t status = {0};
    network_manager_get_portal_status(&status);
    lv_obj_t *overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, WT32_LCD_WIDTH, WT32_LCD_HEIGHT);
    lv_obj_set_style_bg_color(overlay, color(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_70, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *dialog = make_surface(overlay, 42, 42, 396, 236);
    make_label(dialog, "监控配置 AP 已开启", 18, 16, 360, &app_font_18, COLOR_TEXT);
    s_ui.portal_status_label = make_label(dialog, "", 18, 54, 360, &app_font_14, COLOR_TEXT);
    lv_label_set_long_mode(s_ui.portal_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_height(s_ui.portal_status_label, 120);
    lv_label_set_text_fmt(s_ui.portal_status_label,
                          "AP: %s\n密码: %s\n地址: 192.168.4.1\n剩余: %u 秒",
                          status.ssid, status.password, status.seconds_remaining);
    make_button(dialog, "关闭 AP", 246, 184, 132, 38, portal_cancel_event, NULL);
    s_ui.portal_overlay = overlay;
}

static void settings_timer_event(lv_timer_t *timer)
{
    (void)timer;
    char connected_ssid[33] = {0};
    network_manager_get_connected_ssid(connected_ssid, sizeof(connected_ssid));
    if (!s_ui.scan_pending && s_ui.wifi_status != NULL) {
        if (network_manager_is_connected()) {
            lv_label_set_text_fmt(s_ui.wifi_status, "已连接: %s", connected_ssid);
        }
    }
    if (s_ui.scan_pending) {
        bool complete = false;
        size_t count = network_manager_get_scan_results(
            s_ui.scan_records, NETWORK_MAX_SCAN_RESULTS, &complete);
        if (complete) {
            char options[512] = {0};
            size_t used = 0;
            s_ui.scan_count = count;
            for (size_t i = 0; i < count; ++i) {
                char safe_ssid[33];
                sanitize_dynamic_text(s_ui.scan_records[i].ssid, safe_ssid, sizeof(safe_ssid));
                int written = snprintf(options + used, sizeof(options) - used, "%s%s",
                                       i == 0 ? "" : "\n", safe_ssid);
                if (written < 0 || (size_t)written >= sizeof(options) - used) break;
                used += (size_t)written;
            }
            lv_dropdown_set_options(s_ui.wifi_dropdown,
                                    count > 0 ? options : "未发现 Wi-Fi");
            lv_label_set_text_fmt(s_ui.wifi_status, "扫描完成: %u 个网络", (unsigned)count);
            s_ui.scan_pending = false;
        }
    }
    if (s_ui.portal_overlay != NULL) {
        network_portal_status_t status = {0};
        network_manager_get_portal_status(&status);
        if (!status.active) {
            close_portal_overlay(false);
        } else if (s_ui.portal_status_label != NULL) {
            lv_label_set_text_fmt(s_ui.portal_status_label,
                                  "AP: %s\n密码: %s\n地址: 192.168.4.1\n剩余: %u 秒",
                                  status.ssid, status.password, status.seconds_remaining);
        }
    }
}

static void set_refresh_button_state(void)
{
    static const uint8_t intervals[REFRESH_OPTION_COUNT] = {5, 10, 30, 60};
    for (size_t i = 0; i < REFRESH_OPTION_COUNT; ++i) {
        if (s_ui.refresh_buttons[i] != NULL) {
            set_button_selected(s_ui.refresh_buttons[i], intervals[i] == s_refresh_seconds);
        }
    }
}

static void set_homepage_button_state(void)
{
    for (size_t i = 0; i < 2; ++i) {
        if (s_ui.homepage_buttons[i] != NULL)
            set_button_selected(s_ui.homepage_buttons[i], s_homepage == i);
    }
}

static void homepage_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    const uint8_t requested = (uint8_t)(uintptr_t)lv_event_get_user_data(event);
    if (requested > 1 || device_settings_set_u8(DEVICE_KEY_HOME_PAGE, requested) != ESP_OK) {
        ESP_LOGE("dashboard_ui", "Unable to persist homepage=%u", requested);
        return;
    }
    s_homepage = requested;
    s_ui.active_page = requested == 0 ? 0 : 1;
    for (int i = 0; i < PAGE_COUNT; ++i)
        set_hidden(s_ui.pages[i], i != s_ui.active_page);
    set_homepage_button_state();
    set_nav_button_state();
    app_model_set_active_monitor(requested == 0 ? APP_MONITOR_NAS : APP_MONITOR_PVE);
}

static void theme_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    const int direction = (int)(intptr_t)lv_event_get_user_data(event);
    s_theme_id = (uint8_t)((s_theme_id + THEME_COUNT + direction) % THEME_COUNT);
    if (device_settings_set_u8(DEVICE_KEY_THEME, s_theme_id) == ESP_OK) apply_theme();
}

static void refresh_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    const uint8_t seconds = (uint8_t)(uintptr_t)lv_event_get_user_data(event);
    if (device_settings_set_u8(DEVICE_KEY_REFRESH, seconds) != ESP_OK) return;
    s_refresh_seconds = seconds;
    app_model_set_refresh_seconds(seconds);
    set_refresh_button_state();
}

static void clock_timer_event(lv_timer_t *timer)
{
    (void)timer;
    if (s_ui.time_label == NULL) return;
    time_t now = time(NULL);
    struct tm local_now = {0};
    localtime_r(&now, &local_now);
    lv_label_set_text_fmt(s_ui.time_label, "%02d:%02d", local_now.tm_hour, local_now.tm_min);
    char ip_address[16] = {0};
    network_manager_get_ip(ip_address, sizeof(ip_address));
    lv_label_set_text(s_ui.ip_label,
                      network_manager_is_connected() && ip_address[0] != '\0' ?
                      ip_address : "WiFi未连接，请到设置里面设置");
}

static void create_settings_page(lv_obj_t *page)
{
    lv_obj_t *wifi = make_surface(page, 8, 4, 464, 70);
    make_label(wifi, "Wi-Fi", 10, 7, 52, &app_font_18, COLOR_TEXT);
    s_ui.wifi_dropdown = lv_dropdown_create(wifi);
    lv_obj_set_pos(s_ui.wifi_dropdown, 64, 5);
    lv_obj_set_size(s_ui.wifi_dropdown, 208, 30);
    lv_dropdown_set_options(s_ui.wifi_dropdown, "请先扫描 Wi-Fi");
    make_button(wifi, "扫描", 280, 5, 78, 30, scan_event, NULL);
    make_button(wifi, "连接", 366, 5, 86, 30, wifi_connect_event, NULL);
    s_ui.wifi_status = make_label(wifi, "WiFi未连接，请到设置里面设置",
                                  10, 43, 442, &app_font_14, COLOR_MUTED);

    lv_obj_t *display = make_surface(page, 8, 78, 464, 94);
    make_label(display, "屏幕", 10, 8, 50, &app_font_18, COLOR_TEXT);
    make_label(display, "旋转", 66, 10, 42, &app_font_14, COLOR_MUTED);
    s_ui.rotation_0_button = make_button(display, "0°", 110, 6, 50, 30,
                                         rotation_event, (void *)(intptr_t)false);
    s_ui.rotation_180_button = make_button(display, "180°", 166, 6, 58, 30,
                                           rotation_event, (void *)(intptr_t)true);
    make_label(display, "亮度", 232, 10, 42, &app_font_14, COLOR_MUTED);
    s_ui.brightness_slider = lv_slider_create(display);
    lv_obj_set_pos(s_ui.brightness_slider, 278, 15);
    lv_obj_set_size(s_ui.brightness_slider, 118, 10);
    lv_slider_set_range(s_ui.brightness_slider, 10, 100);
    lv_slider_set_value(s_ui.brightness_slider, wt32_board_get_brightness(), LV_ANIM_OFF);
    lv_obj_add_event_cb(s_ui.brightness_slider, brightness_event, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_ui.brightness_slider, brightness_event, LV_EVENT_RELEASED, NULL);
    s_ui.brightness_value = make_label(display, "", 404, 9, 48, &app_font_14, COLOR_TEXT);
    lv_label_set_text_fmt(s_ui.brightness_value, "%u%%", wt32_board_get_brightness());
    make_label(display, "主题", 10, 57, 42, &app_font_14, COLOR_MUTED);
    make_button(display, "<", 58, 46, 42, 36, theme_event, (void *)(intptr_t)-1);
    s_ui.theme_name = make_label(display, palette()->name, 108, 56, 116, &app_font_14, COLOR_TEXT);
    lv_obj_set_style_text_align(s_ui.theme_name, LV_TEXT_ALIGN_CENTER, 0);
    make_button(display, ">", 232, 46, 42, 36, theme_event, (void *)(intptr_t)1);
    make_label(display, "默认 石墨青", 292, 56, 150, &app_font_14, COLOR_MUTED);
    set_rotation_button_state();

    lv_obj_t *monitor = make_surface(page, 8, 176, 464, 108);
    make_label(monitor, "主页", 10, 8, 42, &app_font_14, COLOR_MUTED);
    s_ui.homepage_buttons[0] = make_icon_button(
        monitor, "NAS", &dashboard_icon_dsm, 58, 3, 82, 30,
        homepage_event, (void *)(uintptr_t)0);
    s_ui.homepage_buttons[1] = make_icon_button(
        monitor, "PVE", &dashboard_icon_proxmox, 146, 3, 82, 30,
        homepage_event, (void *)(uintptr_t)1);
    set_homepage_button_state();
    make_label(monitor, "刷新", 10, 43, 42, &app_font_14, COLOR_MUTED);
    static const char *const labels[REFRESH_OPTION_COUNT] = {"5 秒", "10 秒", "30 秒", "60 秒"};
    static const uint8_t intervals[REFRESH_OPTION_COUNT] = {5, 10, 30, 60};
    for (size_t i = 0; i < REFRESH_OPTION_COUNT; ++i) {
        s_ui.refresh_buttons[i] = make_button(monitor, labels[i], 58 + (int)i * 72, 37, 66, 30,
                                              refresh_event, (void *)(uintptr_t)intervals[i]);
    }
    set_refresh_button_state();
    make_label(monitor, "监控配置", 10, 82, 82, &app_font_14, COLOR_MUTED);
    make_label(monitor, "PVE / 群晖只读访问", 96, 82, 196, &app_font_14, COLOR_TEXT);
    make_button(monitor, "PVE Token 设置", 314, 72, 136, 30,
                token_portal_event, NULL);
    s_ui.settings_timer = lv_timer_create(settings_timer_event, 500, NULL);
}

static void create_top_bar(void)
{
    lv_obj_t *bar = lv_obj_create(s_ui.root);
    s_ui.top_bar = bar;
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, WT32_LCD_WIDTH, 32);
    lv_obj_set_style_bg_color(bar, color(COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    s_ui.nas_nav_button = make_icon_button(bar, "NAS", &dashboard_icon_dsm,
                                           4, 3, 76, 26, page_event,
                                           (void *)(intptr_t)0);
    s_ui.time_label = make_label(bar, "--:--", 84, 7, 52, &app_font_14, COLOR_TEXT);
    lv_obj_set_style_text_align(s_ui.time_label, LV_TEXT_ALIGN_CENTER, 0);
    s_ui.ip_label = make_label(bar, "WiFi未连接", 140, 7, 166, &app_font_14, COLOR_MUTED);
    lv_obj_set_style_text_align(s_ui.ip_label, LV_TEXT_ALIGN_CENTER, 0);
    s_ui.pve_nav_button = make_icon_button(bar, "PVE", &dashboard_icon_proxmox,
                                           310, 3, 76, 26, page_event,
                                           (void *)(intptr_t)1);
    s_ui.settings_nav_button = make_button(bar, "设置", 390, 3, 86, 26,
                                           page_event, (void *)(intptr_t)2);
}

esp_err_t dashboard_ui_create(void)
{
    if (s_ui.root != NULL) return ESP_ERR_INVALID_STATE;
    uint8_t saved_theme = THEME_GRAPHITE;
    uint8_t saved_refresh = 5;
    uint8_t saved_homepage = 0;
    if (device_settings_get_u8(DEVICE_KEY_THEME, &saved_theme) == ESP_OK && saved_theme < THEME_COUNT)
        s_theme_id = saved_theme;
    else s_theme_id = THEME_GRAPHITE;
    if (device_settings_get_u8(DEVICE_KEY_REFRESH, &saved_refresh) == ESP_OK &&
        (saved_refresh == 5 || saved_refresh == 10 ||
         saved_refresh == 30 || saved_refresh == 60))
        s_refresh_seconds = saved_refresh;
    else s_refresh_seconds = 5;
    if (device_settings_get_u8(DEVICE_KEY_HOME_PAGE, &saved_homepage) != ESP_OK ||
        saved_homepage > 1) saved_homepage = 0;
    s_homepage = saved_homepage;
    memset(&s_ui, 0, sizeof(s_ui));
    s_ui.active_page = saved_homepage == 0 ? 0 : 1;
    s_ui.selected_vm_index = -1;
    init_styles();
    s_ui.root = lv_scr_act();
    lv_obj_remove_style_all(s_ui.root);
    lv_obj_add_style(s_ui.root, &s_root_style, 0);
    lv_obj_clear_flag(s_ui.root, LV_OBJ_FLAG_SCROLLABLE);
    create_top_bar();
    for (int i = 0; i < PAGE_COUNT; ++i) {
        s_ui.pages[i] = lv_obj_create(s_ui.root);
        lv_obj_remove_style_all(s_ui.pages[i]);
        lv_obj_add_style(s_ui.pages[i], &s_root_style, 0);
        lv_obj_set_pos(s_ui.pages[i], 0, 32);
        lv_obj_set_size(s_ui.pages[i], WT32_LCD_WIDTH, WT32_LCD_HEIGHT - 32);
        lv_obj_clear_flag(s_ui.pages[i], LV_OBJ_FLAG_SCROLLABLE);
    }
    create_nas_page(s_ui.pages[0]);
    create_pve_page(s_ui.pages[1]);
    create_settings_page(s_ui.pages[2]);
    for (int i = 0; i < PAGE_COUNT; ++i)
        set_hidden(s_ui.pages[i], i != s_ui.active_page);
    set_nav_button_state();
    app_model_set_active_monitor(saved_homepage == 0 ? APP_MONITOR_NAS : APP_MONITOR_PVE);
    app_model_set_refresh_seconds(s_refresh_seconds);
    s_ui.clock_timer = lv_timer_create(clock_timer_event, 1000, NULL);
    clock_timer_event(s_ui.clock_timer);
    ESP_LOGI("dashboard_ui", "Created NAS/PVE/Settings LVGL pages");
    return ESP_OK;
}

static void update_pve(void)
{
    const app_snapshot_t *snapshot = &s_ui.snapshot;
    char buffer[200], total[24], used[24], uptime[32];
    if (snapshot->pve_online) {
        lv_label_set_text_fmt(s_ui.pve_name, "%s",
                              snapshot->pve_name[0] ? snapshot->pve_name : "p330");
        lv_label_set_text_fmt(s_ui.pve_version, "version %s",
                              snapshot->pve_version[0] ? snapshot->pve_version : "--");
        lv_label_set_text_fmt(s_ui.pve_host, "IP %s",
                              snapshot->pve_host[0] ? snapshot->pve_host : "--");
        format_uptime(uptime, sizeof(uptime), snapshot->pve_uptime_seconds);
        lv_label_set_text_fmt(s_ui.pve_uptime, "运行 %s",
                              snapshot->pve_uptime_seconds > 0 ? uptime : "--");
        lv_obj_set_style_bg_color(s_ui.pve_status_dot, color(COLOR_GREEN), 0);
    } else {
        lv_label_set_text(s_ui.pve_name, snapshot->pve_configured ? "PVE 离线" : "PVE 未配置");
        lv_label_set_text(s_ui.pve_version, "--");
        lv_label_set_text(s_ui.pve_host, "--");
        lv_label_set_text(s_ui.pve_uptime, "--");
        lv_obj_set_style_bg_color(s_ui.pve_status_dot, color(COLOR_GRAY), 0);
    }
    format_percent(buffer, sizeof(buffer), snapshot->pve_cpu_percent);
    lv_label_set_text(s_ui.pve_cpu_value, buffer);
    format_bytes(used, sizeof(used), snapshot->pve_memory_used);
    format_bytes(total, sizeof(total), snapshot->pve_memory_total);
    lv_label_set_text_fmt(s_ui.pve_memory_value, "%s/%s", used, total);
    format_bytes(used, sizeof(used), snapshot->pve_storage_used);
    format_bytes(total, sizeof(total), snapshot->pve_storage_total);
    lv_label_set_text_fmt(s_ui.pve_storage_value, "%s/%s", used, total);
    for (int i = 0; i < 3; ++i) {
        snprintf(buffer, sizeof(buffer), "%.1f", snapshot->pve_load[i]);
        lv_label_set_text(s_ui.pve_load_values[i], buffer);
    }
    lv_bar_set_value(s_ui.pve_cpu_bar, (int)snapshot->pve_cpu_percent, LV_ANIM_OFF);
    const int memory_percent = snapshot->pve_memory_total == 0 ? 0 :
        (int)(snapshot->pve_memory_used * 100ULL / snapshot->pve_memory_total);
    const int storage_percent = snapshot->pve_storage_total == 0 ? 0 :
        (int)(snapshot->pve_storage_used * 100ULL / snapshot->pve_storage_total);
    lv_bar_set_value(s_ui.pve_memory_bar, memory_percent, LV_ANIM_OFF);
    lv_bar_set_value(s_ui.pve_storage_bar, storage_percent, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_ui.pve_cpu_bar, lv_color_hex(palette()->free), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.pve_cpu_bar, lv_color_hex(palette()->used), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_ui.pve_memory_bar, lv_color_hex(palette()->free), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.pve_memory_bar, lv_color_hex(palette()->used), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_ui.pve_storage_bar, lv_color_hex(palette()->free), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.pve_storage_bar, lv_color_hex(palette()->used), LV_PART_INDICATOR);
    if (snapshot->pve_cpu_cores > 0)
        lv_label_set_text_fmt(s_ui.pve_cpu_cores_value, "%" PRIu32 " 核",
                              snapshot->pve_cpu_cores);
    else
        lv_label_set_text(s_ui.pve_cpu_cores_value, "-- 核");
    lv_label_set_text(s_ui.pve_cpu_model,
                      snapshot->pve_cpu_model[0] ? snapshot->pve_cpu_model : "--");
    lv_label_set_text_fmt(s_ui.pve_vm_list_label, "虚拟机列表  %" PRIu32 "/%zu 运行",
                          snapshot->pve_running_count, snapshot->pve_guest_count);
    if (s_ui.selected_vm_index >= (int)snapshot->pve_guest_count &&
        s_ui.pve_view == PVE_VIEW_VM_DETAIL) show_pve_vm_list();
    const size_t last_page_offset = snapshot->pve_guest_count > VM_VISIBLE ?
        ((snapshot->pve_guest_count - 1) / VM_VISIBLE) * VM_VISIBLE : 0;
    if ((size_t)s_ui.vm_list_offset > last_page_offset)
        s_ui.vm_list_offset = (int)last_page_offset;
    const size_t page_count = snapshot->pve_guest_count > 0 ?
        (snapshot->pve_guest_count + VM_VISIBLE - 1) / VM_VISIBLE : 1;
    const bool paged = snapshot->pve_guest_count > VM_VISIBLE;
    set_hidden(s_ui.vm_list_pager, !paged);
    lv_label_set_text_fmt(s_ui.vm_list_page, "%d/%zu",
                          s_ui.vm_list_offset / VM_VISIBLE + 1, page_count);
    if (s_ui.vm_list_offset == 0)
        lv_obj_add_state(s_ui.vm_list_previous, LV_STATE_DISABLED);
    else
        lv_obj_clear_state(s_ui.vm_list_previous, LV_STATE_DISABLED);
    if (s_ui.vm_list_offset + VM_VISIBLE >= (int)snapshot->pve_guest_count)
        lv_obj_add_state(s_ui.vm_list_next, LV_STATE_DISABLED);
    else
        lv_obj_clear_state(s_ui.vm_list_next, LV_STATE_DISABLED);
    set_hidden(s_ui.vm_list_empty, snapshot->pve_guest_count > 0);
    if (snapshot->pve_guest_count == 0)
        lv_label_set_text(s_ui.vm_list_empty,
                          snapshot->pve_online ? "未发现虚拟机" : "PVE 离线");
    for (int i = 0; i < VM_VISIBLE; ++i) {
        const int index = s_ui.vm_list_offset + i;
        const bool visible = index < (int)snapshot->pve_guest_count;
        set_hidden(s_ui.vm_rows[i], !visible);
        if (!visible) continue;
        const pve_guest_t *guest = &snapshot->pve_guests[index];
        lv_obj_set_style_bg_color(s_ui.vm_row_dots[i], color(guest->running ? COLOR_GREEN : COLOR_GRAY), 0);
        lv_label_set_text_fmt(s_ui.vm_row_labels[i], "%" PRIu32 " %s", guest->vmid, guest->name);
        lv_label_set_text(s_ui.vm_row_ips[i], guest->ipv4_address[0] != '\0' ?
                          guest->ipv4_address : "--");
        char cpu[16], capacity[52];
        format_percent(cpu, sizeof(cpu), guest->cpu_percent);
        lv_label_set_text(s_ui.vm_row_cpu[i], cpu);
        if (guest->memory_total == 0) {
            lv_label_set_text(s_ui.vm_row_memory[i], "--");
        } else {
            format_bytes(used, sizeof(used), guest->memory_used);
            format_bytes(total, sizeof(total), guest->memory_total);
            snprintf(capacity, sizeof(capacity), "%s/%s", used, total);
            lv_label_set_text(s_ui.vm_row_memory[i], capacity);
        }
        lv_obj_remove_event_cb(s_ui.vm_name_buttons[i], vm_name_event);
        lv_obj_add_event_cb(s_ui.vm_name_buttons[i], vm_name_event, LV_EVENT_CLICKED,
                            (void *)(intptr_t)index);
    }
    if (s_ui.pve_view == PVE_VIEW_VM_DETAIL && s_ui.selected_vm_index >= 0)
        update_vm_detail();
}

static void update_nas(void)
{
    const app_snapshot_t *snapshot = &s_ui.snapshot;
    char used[24], total[24], free_space[24], uptime[32];
    const bool nas_live = snapshot->nas_online;
    const bool has_pools = snapshot->nas_pool_count > 0;
    set_hidden(s_ui.nas_overview_message, has_pools);
    if (!has_pools) {
        if (!snapshot->nas_configured)
            lv_label_set_text_fmt(s_ui.nas_overview_message, "NAS 未配置  %s",
                                  snapshot->nas_last_error);
        else if (nas_live)
            lv_label_set_text(s_ui.nas_overview_message, "NAS 在线  未发现存储池");
        else
            lv_label_set_text_fmt(s_ui.nas_overview_message, "NAS 离线  %s",
                                  snapshot->nas_last_error);
    }
    const size_t pool_last_offset = has_pools ?
        ((snapshot->nas_pool_count - 1) / NAS_VISIBLE) * NAS_VISIBLE : 0;
    if (s_ui.nas_pool_offset > pool_last_offset) s_ui.nas_pool_offset = pool_last_offset;
    const size_t pool_page_count = has_pools ?
        (snapshot->nas_pool_count + NAS_VISIBLE - 1) / NAS_VISIBLE : 1;
    const bool pool_paged = snapshot->nas_pool_count > NAS_VISIBLE;
    set_nas_pool_pager_layout(pool_paged);
    lv_label_set_text_fmt(s_ui.nas_pool_page, "%zu/%zu",
                          s_ui.nas_pool_offset / NAS_VISIBLE + 1, pool_page_count);
    set_hidden(s_ui.nas_pool_page, !pool_paged);
    set_hidden(s_ui.nas_pool_previous, !pool_paged || s_ui.nas_pool_offset == 0);
    set_hidden(s_ui.nas_pool_next,
               !pool_paged || s_ui.nas_pool_offset + NAS_VISIBLE >= snapshot->nas_pool_count);
    for (int i = 0; i < NAS_VISIBLE; ++i) {
        const size_t index = s_ui.nas_pool_offset + (size_t)i;
        const bool visible = index < snapshot->nas_pool_count;
        set_hidden(s_ui.nas_rows[i], !visible);
        if (!visible) continue;
        const nas_pool_t *pool = &snapshot->nas_pools[index];
        const bool healthy = nas_live && pool->healthy;
        lv_label_set_text_fmt(s_ui.nas_row_labels[i], "存储池%zu", index + 1);
        lv_obj_set_style_bg_color(s_ui.nas_row_status_dots[i],
                                  color(healthy ? COLOR_GREEN : COLOR_RED), 0);
        lv_label_set_text(s_ui.nas_row_descriptions[i], healthy ? "" :
                          (!nas_live ? "离线" :
                           (pool->description[0] ? pool->description : "异常")));
        lv_obj_set_style_text_color(s_ui.nas_row_descriptions[i],
                                    color(healthy ? COLOR_MUTED : COLOR_RED), 0);
        if (nas_live) {
            format_capacity(used, sizeof(used), pool->used_bytes);
            format_capacity(total, sizeof(total), pool->total_bytes);
            format_capacity(free_space, sizeof(free_space), pool->free_bytes);
            lv_label_set_text_fmt(s_ui.nas_row_values[i], "%s/%s 剩%s", used, total, free_space);
            lv_bar_set_value(s_ui.nas_row_bars[i], pool->total_bytes == 0 ? 0 :
                             (int)(pool->used_bytes * 100ULL / pool->total_bytes), LV_ANIM_OFF);
        } else {
            lv_label_set_text(s_ui.nas_row_values[i], "--/-- 剩--");
            lv_bar_set_value(s_ui.nas_row_bars[i], 0, LV_ANIM_OFF);
        }
        lv_obj_set_style_bg_color(s_ui.nas_row_bars[i], lv_color_hex(palette()->positive), LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_ui.nas_row_bars[i], lv_color_hex(palette()->used), LV_PART_INDICATOR);
    }
    if (nas_live && snapshot->nas_cpu_valid) {
        format_percent(used, sizeof(used), snapshot->nas_cpu_percent);
        lv_label_set_text(s_ui.nas_cpu_value, used);
    } else {
        lv_label_set_text(s_ui.nas_cpu_value, "--");
    }
    if (nas_live && snapshot->nas_memory_valid) {
        format_percent(used, sizeof(used), snapshot->nas_memory_percent);
        lv_label_set_text(s_ui.nas_memory_value, used);
    } else {
        lv_label_set_text(s_ui.nas_memory_value, "--");
    }
    if (nas_live && snapshot->nas_temperature_valid) {
        lv_label_set_text_fmt(s_ui.nas_temperature_value, "%d°", snapshot->nas_temperature);
    } else {
        lv_label_set_text(s_ui.nas_temperature_value, "--");
    }
    if (nas_live && snapshot->nas_network_rate_valid) {
        char upload[24], download[24];
        format_rate(upload, sizeof(upload), snapshot->nas_tx_bytes_per_second);
        format_rate(download, sizeof(download), snapshot->nas_rx_bytes_per_second);
        lv_label_set_text(s_ui.nas_upload_value, upload);
        lv_label_set_text(s_ui.nas_download_value, download);
    } else {
        lv_label_set_text(s_ui.nas_upload_value, "--");
        lv_label_set_text(s_ui.nas_download_value, "--");
    }
    lv_label_set_text_fmt(s_ui.nas_ip_value, "%s",
                          snapshot->nas_configured && snapshot->nas_host[0] ?
                          snapshot->nas_host : "--");
    if (nas_live && snapshot->nas_uptime_valid) {
        format_uptime(uptime, sizeof(uptime), snapshot->nas_uptime_seconds);
        lv_label_set_text(s_ui.nas_uptime_value, uptime);
    } else {
        lv_label_set_text(s_ui.nas_uptime_value, "--");
    }
    if (s_ui.nas_disks_visible) update_nas_disks();
}

static void toast_set_opa(void *object, int32_t opacity)
{
    lv_obj_set_style_opa(object, (lv_opa_t)opacity, 0);
}

static void toast_animation_ready(lv_anim_t *animation)
{
    lv_obj_t *toast = animation != NULL ? animation->var : NULL;
    if (toast == s_ui.toast) s_ui.toast = NULL;
    if (toast != NULL) lv_obj_del_async(toast);
}

static void show_refresh_toast(void)
{
    if (s_ui.toast != NULL) {
        lv_anim_del(s_ui.toast, NULL);
        lv_obj_del(s_ui.toast);
        s_ui.toast = NULL;
    }
    const int page = s_ui.active_page == 0 || s_ui.active_page == 1 ? s_ui.active_page : 0;
    s_ui.toast = make_surface(s_ui.pages[page], 0, 0, 258, 40);
    lv_obj_set_style_bg_color(s_ui.toast, color(COLOR_SURFACE_ALT), 0);
    lv_obj_set_style_border_color(s_ui.toast, color(COLOR_BLUE), 0);
    lv_obj_clear_flag(s_ui.toast, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(s_ui.toast);
    lv_obj_t *label = make_label(s_ui.toast, "刷新失败，已显示上次数据",
                                 8, 10, 242, &app_font_14, COLOR_TEXT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_opa(s_ui.toast, LV_OPA_TRANSP, 0);

    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, s_ui.toast);
    lv_anim_set_exec_cb(&animation, toast_set_opa);
    lv_anim_set_values(&animation, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&animation, 220);
    lv_anim_set_playback_delay(&animation, TOAST_DURATION_MS - 520);
    lv_anim_set_playback_time(&animation, 300);
    lv_anim_set_ready_cb(&animation, toast_animation_ready);
    lv_anim_start(&animation);
}

static void show_monitor_message(app_monitor_t monitor, const char *message)
{
    if (monitor == APP_MONITOR_NAS) {
        for (int i = 0; i < NAS_VISIBLE; ++i) set_hidden(s_ui.nas_rows[i], true);
        set_hidden(s_ui.nas_pool_previous, true);
        set_hidden(s_ui.nas_pool_next, true);
        set_hidden(s_ui.nas_pool_page, true);
        lv_label_set_text(s_ui.nas_overview_message, message);
        set_hidden(s_ui.nas_overview_message, false);
        lv_label_set_text(s_ui.nas_disk_empty, message);
        set_hidden(s_ui.nas_disk_empty, false);
    } else if (monitor == APP_MONITOR_PVE) {
        show_pve_node();
        lv_label_set_text(s_ui.pve_name, message);
        lv_label_set_text(s_ui.pve_version, "--");
        lv_label_set_text(s_ui.pve_host, "--");
        lv_label_set_text(s_ui.pve_uptime, "--");
        lv_label_set_text(s_ui.pve_cpu_value, "--");
        for (int i = 0; i < 3; ++i) lv_label_set_text(s_ui.pve_load_values[i], "--");
        lv_label_set_text(s_ui.pve_memory_value, "--");
        lv_label_set_text(s_ui.pve_storage_value, "--");
        lv_label_set_text(s_ui.pve_cpu_cores_value, "-- 核");
        lv_label_set_text(s_ui.pve_cpu_model, "--");
        lv_bar_set_value(s_ui.pve_cpu_bar, 0, LV_ANIM_OFF);
        lv_bar_set_value(s_ui.pve_memory_bar, 0, LV_ANIM_OFF);
        lv_bar_set_value(s_ui.pve_storage_bar, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(s_ui.pve_status_dot, color(COLOR_GRAY), 0);
        for (int i = 0; i < VM_VISIBLE; ++i) set_hidden(s_ui.vm_rows[i], true);
    }
}

void dashboard_ui_update(app_model_event_t *event)
{
    if (event == NULL || s_ui.root == NULL) return;
    const bool monitor_has_snapshot = event->monitor == APP_MONITOR_NAS ?
        s_ui.has_nas_snapshot : event->monitor == APP_MONITOR_PVE ?
        s_ui.has_pve_snapshot : s_ui.has_snapshot;
    if (event->kind == APP_MODEL_EVENT_LOADING) {
        if (!monitor_has_snapshot) show_monitor_message(event->monitor, "数据加载中");
        return;
    }
    if (event->kind == APP_MODEL_EVENT_REFRESH_FAILED) {
        if (monitor_has_snapshot) show_refresh_toast();
        else show_monitor_message(event->monitor, "设备离线");
        return;
    }
    if (event->kind == APP_MODEL_EVENT_OFFLINE) {
        if (monitor_has_snapshot) show_refresh_toast();
        else show_monitor_message(event->monitor, "设备离线");
        return;
    }
    if (event->kind != APP_MODEL_EVENT_SNAPSHOT || event->snapshot == NULL) return;
    app_snapshot_move(&s_ui.snapshot, event->snapshot);
    const app_snapshot_t *snapshot = &s_ui.snapshot;
    s_ui.has_snapshot = true;
    if (event->monitor == APP_MONITOR_NAS) s_ui.has_nas_snapshot = true;
    if (event->monitor == APP_MONITOR_PVE) s_ui.has_pve_snapshot = true;
    if (snapshot->pve_last_success_ms > 0) s_ui.has_pve_snapshot = true;
    if (snapshot->nas_last_success_ms > 0) s_ui.has_nas_snapshot = true;
    lv_label_set_text(s_ui.ip_label,
                      snapshot->wifi_connected && snapshot->ip_address[0] != '\0' ?
                      snapshot->ip_address : "WiFi未连接，请到设置里面设置");
    update_pve();
    update_nas();
}
