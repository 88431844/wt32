#include "dashboard_ui.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "board_wt32.h"
#include "device_settings.h"
#include "esp_log.h"
#include "lvgl.h"
#include "network_manager.h"

LV_FONT_DECLARE(app_font_14);
LV_FONT_DECLARE(app_font_18);

#define PAGE_COUNT 3
#define VM_VISIBLE 5
#define VM_NAV_VISIBLE 4
#define NAS_VISIBLE 4
#define NAS_DISK_VISIBLE 4
#define NAS_METRICS_HEIGHT 36
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

static const dashboard_palette_t *palette(void)
{
    return &s_palettes[s_theme_id < THEME_COUNT ? s_theme_id : THEME_GRAPHITE];
}

#define PVE_SUBNAV_LEFT "PVE_SUBNAV_LEFT"
#define PVE_SUBNAV_RIGHT "PVE_SUBNAV_RIGHT"
#define PVE_VM_SCROLL_UP "PVE_VM_SCROLL_UP"
#define PVE_VM_SCROLL_DOWN "PVE_VM_SCROLL_DOWN"

typedef struct {
    lv_obj_t *root;
    lv_obj_t *top_bar;
    lv_obj_t *pages[PAGE_COUNT];
    lv_obj_t *pve_nav_button;
    lv_obj_t *nas_nav_button;
    lv_obj_t *settings_nav_button;
    lv_obj_t *time_label;
    lv_obj_t *ip_label;
    lv_obj_t *pve_identity;
    lv_obj_t *pve_status_dot;
    lv_obj_t *pve_cpu_value;
    lv_obj_t *pve_memory_value;
    lv_obj_t *pve_storage_value;
    lv_obj_t *pve_load_value;
    lv_obj_t *pve_cpu_bar;
    lv_obj_t *pve_memory_bar;
    lv_obj_t *pve_storage_bar;
    lv_obj_t *pve_overview_button;
    lv_obj_t *pve_overview;
    lv_obj_t *vm_detail;
    lv_obj_t *vm_detail_title;
    lv_obj_t *vm_detail_status_dot;
    lv_obj_t *vm_detail_status;
    lv_obj_t *vm_detail_ip;
    lv_obj_t *vm_detail_cpu;
    lv_obj_t *vm_detail_memory;
    lv_obj_t *vm_detail_disk;
    lv_obj_t *vm_detail_uptime;
    lv_obj_t *vm_detail_agent;
    lv_obj_t *vm_detail_previous;
    lv_obj_t *vm_detail_next;
    lv_obj_t *vm_title;
    lv_obj_t *vm_header_ip;
    lv_obj_t *vm_header_cpu;
    lv_obj_t *vm_header_memory;
    lv_obj_t *vm_header_disk;
    lv_obj_t *vm_nav_buttons[VM_NAV_VISIBLE];
    lv_obj_t *vm_nav_labels[VM_NAV_VISIBLE];
    lv_obj_t *vm_nav_dots[VM_NAV_VISIBLE];
    lv_obj_t *vm_rows[VM_VISIBLE];
    lv_obj_t *vm_row_labels[VM_VISIBLE];
    lv_obj_t *vm_row_dots[VM_VISIBLE];
    lv_obj_t *vm_row_ips[VM_VISIBLE];
    lv_obj_t *vm_row_cpu[VM_VISIBLE];
    lv_obj_t *vm_row_memory[VM_VISIBLE];
    lv_obj_t *vm_row_disk[VM_VISIBLE];
    int vm_nav_offset;
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
    lv_obj_t *nas_detail;
    lv_obj_t *nas_disk_empty;
    lv_obj_t *nas_disk_previous;
    lv_obj_t *nas_disk_next;
    lv_obj_t *nas_disk_page;
    lv_obj_t *nas_disk_rows[NAS_DISK_VISIBLE];
    lv_obj_t *nas_disk_dots[NAS_DISK_VISIBLE];
    lv_obj_t *nas_disk_ids[NAS_DISK_VISIBLE];
    lv_obj_t *nas_disk_models[NAS_DISK_VISIBLE];
    lv_obj_t *nas_disk_temperatures[NAS_DISK_VISIBLE];
    lv_obj_t *nas_cpu_value;
    lv_obj_t *nas_memory_value;
    lv_obj_t *nas_temperature_value;
    lv_obj_t *nas_network_prefix;
    lv_obj_t *nas_upload_value;
    lv_obj_t *nas_download_value;
    lv_obj_t *nas_ip_value;
    lv_obj_t *nas_uptime_value;
    bool nas_disks_visible;
    int nas_disk_offset;
    lv_obj_t *wifi_dropdown;
    lv_obj_t *wifi_status;
    lv_obj_t *brightness_value;
    lv_obj_t *brightness_slider;
    lv_obj_t *rotation_0_button;
    lv_obj_t *rotation_180_button;
    lv_obj_t *theme_name;
    lv_obj_t *refresh_buttons[REFRESH_OPTION_COUNT];
    lv_obj_t *password_overlay;
    lv_obj_t *password_textarea;
    lv_obj_t *portal_overlay;
    lv_obj_t *portal_status_label;
    lv_timer_t *settings_timer;
    lv_timer_t *clock_timer;
    bool scan_pending;
    network_scan_record_t scan_records[NETWORK_MAX_SCAN_RESULTS];
    size_t scan_count;
    int active_page;
    app_snapshot_t snapshot;
    bool has_snapshot;
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
static void update_vm_navigation(void);
static void vm_row_event(lv_event_t *event);
static void set_rotation_button_state(void);
static void set_refresh_button_state(void);

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
    if (days > 0)
        snprintf(out, size, "%" PRIu32 "天%" PRIu32 "时", days, hours);
    else
        snprintf(out, size, "%" PRIu32 "时%" PRIu32 "分", hours, minutes);
}

static void format_percent(char *out, size_t size, float value)
{
    if (value < 0) value = 0;
    if (value > 100) value = 100;
    snprintf(out, size, "%.0f%%", value);
}

static void format_rate(char *out, size_t size, uint64_t bytes_per_second)
{
    if (bytes_per_second >= (1ULL << 30))
        snprintf(out, size, "%.1fG/s", (double)bytes_per_second / (1ULL << 30));
    else if (bytes_per_second >= (1ULL << 20))
        snprintf(out, size, "%.1fM/s", (double)bytes_per_second / (1ULL << 20));
    else if (bytes_per_second >= (1ULL << 10))
        snprintf(out, size, "%.1fK/s", (double)bytes_per_second / (1ULL << 10));
    else snprintf(out, size, "%" PRIu64 "B/s", bytes_per_second);
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
    set_nav_button_state();
    set_rotation_button_state();
    set_refresh_button_state();
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
    set_nav_button_state();
    app_model_set_active_monitor(page == 0 ? APP_MONITOR_NAS :
                                 page == 1 ? APP_MONITOR_PVE : APP_MONITOR_NONE);
}

static void show_pve_overview(void)
{
    s_ui.selected_vm_index = -1;
    set_hidden(s_ui.pve_overview, false);
    set_hidden(s_ui.vm_detail, true);
    update_vm_navigation();
}

static void update_vm_detail(void)
{
    if (s_ui.selected_vm_index < 0 ||
        s_ui.selected_vm_index >= (int)s_ui.snapshot.pve_guest_count) {
        show_pve_overview();
        return;
    }

    const pve_guest_t *guest = &s_ui.snapshot.pve_guests[s_ui.selected_vm_index];
    char value[96], used[24], total[24];
    lv_label_set_text_fmt(s_ui.vm_detail_title, "VM %" PRIu32 "  %s", guest->vmid, guest->name);
    lv_label_set_text(s_ui.vm_detail_status, guest->running ? "运行中" : "已停止");
    lv_obj_set_style_text_color(s_ui.vm_detail_status,
                                color(guest->running ? COLOR_GREEN : COLOR_GRAY), 0);
    lv_obj_set_style_bg_color(s_ui.vm_detail_status_dot,
                              color(guest->running ? COLOR_GREEN : COLOR_GRAY), 0);
    lv_label_set_text_fmt(s_ui.vm_detail_ip, "IP  %s",
                          guest->ipv4_address[0] != '\0' ? guest->ipv4_address : "--");
    format_percent(value, sizeof(value), guest->cpu_percent);
    lv_label_set_text_fmt(s_ui.vm_detail_cpu, "CPU  %s  /  %" PRIu32 " 核", value, guest->cpu_cores);
    if (guest->memory_total == 0) {
        lv_label_set_text(s_ui.vm_detail_memory, "内存  --");
    } else {
        format_bytes(used, sizeof(used), guest->memory_used);
        format_bytes(total, sizeof(total), guest->memory_total);
        lv_label_set_text_fmt(s_ui.vm_detail_memory, "内存  %s / %s", used, total);
    }
    if (guest->disk_total == 0) {
        lv_label_set_text(s_ui.vm_detail_disk, "磁盘  --");
    } else {
        format_bytes(used, sizeof(used), guest->disk_used);
        format_bytes(total, sizeof(total), guest->disk_total);
        lv_label_set_text_fmt(s_ui.vm_detail_disk, "磁盘  %s / %s", used, total);
    }
    lv_label_set_text_fmt(s_ui.vm_detail_uptime, "运行时间  %" PRIu32 " 秒", guest->uptime_seconds);
    lv_label_set_text(s_ui.vm_detail_agent,
                      guest->guest_agent ? "Guest Agent  在线" : "Guest Agent  未报告");
    lv_obj_set_style_text_color(s_ui.vm_detail_agent,
                                color(guest->guest_agent ? COLOR_GREEN : COLOR_MUTED), 0);
}

static void update_vm_navigation(void)
{
    const int guest_count = (int)s_ui.snapshot.pve_guest_count;
    const int max_offset = guest_count > VM_NAV_VISIBLE ? guest_count - VM_NAV_VISIBLE : 0;
    if (s_ui.vm_nav_offset < 0) s_ui.vm_nav_offset = 0;
    if (s_ui.vm_nav_offset > max_offset) s_ui.vm_nav_offset = max_offset;

    set_button_selected(s_ui.pve_overview_button, s_ui.selected_vm_index < 0);
    for (int i = 0; i < VM_NAV_VISIBLE; ++i) {
        const int index = s_ui.vm_nav_offset + i;
        const bool visible = index < guest_count;
        set_hidden(s_ui.vm_nav_buttons[i], !visible);
        if (!visible) continue;
        const pve_guest_t *guest = &s_ui.snapshot.pve_guests[index];
        const bool selected = s_ui.selected_vm_index == index;
        lv_label_set_text_fmt(s_ui.vm_nav_labels[i], "VM %" PRIu32, guest->vmid);
        lv_obj_set_style_bg_color(s_ui.vm_nav_dots[i],
                                  color(guest->running ? COLOR_GREEN : COLOR_GRAY), 0);
        lv_obj_set_style_bg_color(s_ui.vm_nav_buttons[i],
                                  color(selected ? COLOR_BLUE : COLOR_SURFACE_ALT), 0);
        lv_obj_set_style_text_color(s_ui.vm_nav_labels[i],
                                    lv_color_hex(selected ? palette()->selected_text : palette()->text), 0);
        lv_obj_remove_event_cb(s_ui.vm_nav_buttons[i], vm_row_event);
        lv_obj_add_event_cb(s_ui.vm_nav_buttons[i], vm_row_event, LV_EVENT_CLICKED,
                            (void *)(intptr_t)index);
    }

    const bool in_detail = s_ui.selected_vm_index >= 0 && s_ui.selected_vm_index < guest_count;
    const bool has_previous = in_detail && s_ui.selected_vm_index > 0;
    const bool has_next = in_detail && s_ui.selected_vm_index + 1 < guest_count;
    lv_obj_set_x(s_ui.vm_detail_previous, has_previous && has_next ? 378 : 424);
    set_hidden(s_ui.vm_detail_previous, !has_previous);
    set_hidden(s_ui.vm_detail_next, !has_next);
}

static void show_vm_detail(size_t index)
{
    if (!s_ui.has_snapshot || index >= s_ui.snapshot.pve_guest_count) return;
    s_ui.selected_vm_index = (int)index;
    if (s_ui.selected_vm_index < s_ui.vm_nav_offset) {
        s_ui.vm_nav_offset = s_ui.selected_vm_index;
    } else if (s_ui.selected_vm_index >= s_ui.vm_nav_offset + VM_NAV_VISIBLE) {
        s_ui.vm_nav_offset = s_ui.selected_vm_index - VM_NAV_VISIBLE + 1;
    }
    set_hidden(s_ui.pve_overview, true);
    set_hidden(s_ui.vm_detail, false);
    update_vm_navigation();
    update_vm_detail();
}

static void pve_overview_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) show_pve_overview();
}

static void vm_row_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED)
        show_vm_detail((size_t)(intptr_t)lv_event_get_user_data(event));
}

static void vm_detail_nav_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || s_ui.selected_vm_index < 0) return;
    const int direction = (int)(intptr_t)lv_event_get_user_data(event);
    const int index = s_ui.selected_vm_index + direction;
    if (index >= 0 && index < (int)s_ui.snapshot.pve_guest_count) show_vm_detail((size_t)index);
}

static void vm_scroll_event(lv_event_t *event)
{
    const int direction = (int)(intptr_t)lv_event_get_user_data(event);
    const int max_offset = s_ui.snapshot.pve_guest_count > VM_VISIBLE ?
                           (int)s_ui.snapshot.pve_guest_count - VM_VISIBLE : 0;
    if (direction < 0 && s_ui.vm_list_offset > 0) s_ui.vm_list_offset -= VM_VISIBLE;
    if (direction > 0 && s_ui.vm_list_offset < max_offset) s_ui.vm_list_offset += VM_VISIBLE;
    if (s_ui.vm_list_offset > max_offset) s_ui.vm_list_offset = max_offset;
    update_pve();
}

static void create_pve_page(lv_obj_t *page)
{
    s_ui.pve_overview_button = make_button(page, "PVE 总览", 8, 4, 94, 24,
                                           pve_overview_event, NULL);
    for (int i = 0; i < VM_NAV_VISIBLE; ++i) {
        const int x = 106 + i * 67;
        s_ui.vm_nav_buttons[i] = lv_btn_create(page);
        lv_obj_remove_style_all(s_ui.vm_nav_buttons[i]);
        lv_obj_add_style(s_ui.vm_nav_buttons[i], &s_button_style, 0);
        lv_obj_set_pos(s_ui.vm_nav_buttons[i], x, 4);
        lv_obj_set_size(s_ui.vm_nav_buttons[i], 64, 24);
        s_ui.vm_nav_dots[i] = make_status_dot(s_ui.vm_nav_buttons[i], 6, 8, 8, COLOR_GRAY);
        s_ui.vm_nav_labels[i] = make_label(s_ui.vm_nav_buttons[i], "", 18, 4, 43,
                                           &app_font_14, COLOR_TEXT);
        set_hidden(s_ui.vm_nav_buttons[i], true);
    }
    s_ui.vm_detail_previous = make_button(page, "<", 424, 4, 42, 24,
                                           vm_detail_nav_event, (void *)(intptr_t)-1);
    s_ui.vm_detail_next = make_button(page, ">", 424, 4, 42, 24,
                                      vm_detail_nav_event, (void *)(intptr_t)1);
    set_hidden(s_ui.vm_detail_previous, true);
    set_hidden(s_ui.vm_detail_next, true);

    s_ui.pve_overview = lv_obj_create(page);
    lv_obj_remove_style_all(s_ui.pve_overview);
    lv_obj_set_pos(s_ui.pve_overview, 0, 30);
    lv_obj_set_size(s_ui.pve_overview, WT32_LCD_WIDTH, 258);
    lv_obj_clear_flag(s_ui.pve_overview, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *summary = make_surface(s_ui.pve_overview, 8, 4, 464, 94);
    s_ui.pve_status_dot = make_status_dot(summary, 10, 13, 8, COLOR_GRAY);
    s_ui.pve_identity = make_label(summary, "PVE 未连接", 24, 8, 430, &app_font_14, COLOR_MUTED);
    make_label(summary, "CPU", 12, 32, 50, &app_font_14, COLOR_MUTED);
    make_label(summary, "内存", 125, 32, 55, &app_font_14, COLOR_MUTED);
    make_label(summary, "存储", 238, 32, 55, &app_font_14, COLOR_MUTED);
    make_label(summary, "负载", 355, 32, 55, &app_font_14, COLOR_MUTED);
    s_ui.pve_cpu_value = make_label(summary, "--", 12, 49, 92, &app_font_18, COLOR_TEXT);
    s_ui.pve_memory_value = make_label(summary, "--", 125, 49, 100, &app_font_18, COLOR_TEXT);
    s_ui.pve_storage_value = make_label(summary, "--", 238, 49, 108, &app_font_18, COLOR_TEXT);
    s_ui.pve_load_value = make_label(summary, "--", 355, 49, 90, &app_font_18, COLOR_TEXT);
    s_ui.pve_cpu_bar = make_split_bar(summary, 12, 78, 92, 7, 0, 100);
    s_ui.pve_memory_bar = make_split_bar(summary, 125, 78, 100, 7, 0, 100);
    s_ui.pve_storage_bar = make_split_bar(summary, 238, 78, 108, 7, 0, 100);

    lv_obj_t *list = make_surface(s_ui.pve_overview, 8, 104, 464, 146);
    s_ui.vm_title = make_label(list, "虚拟机 0/0 运行", 10, 6, 108, &app_font_14, COLOR_TEXT);
    s_ui.vm_header_ip = make_label(list, "IP", 120, 6, 108, &app_font_14, COLOR_MUTED);
    s_ui.vm_header_cpu = make_label(list, "CPU", 230, 6, 38, &app_font_14, COLOR_MUTED);
    s_ui.vm_header_memory = make_label(list, "内存", 270, 6, 72, &app_font_14, COLOR_MUTED);
    s_ui.vm_header_disk = make_label(list, "磁盘", 344, 6, 82, &app_font_14, COLOR_MUTED);
    make_button(list, "^", 436, 0, 28, 73, vm_scroll_event, (void *)(intptr_t)-1);
    make_button(list, "v", 436, 73, 28, 73, vm_scroll_event, (void *)(intptr_t)1);
    for (int i = 0; i < VM_VISIBLE; ++i) {
        const int y = 29 + i * 22;
        s_ui.vm_rows[i] = lv_btn_create(list);
        lv_obj_remove_style_all(s_ui.vm_rows[i]);
        lv_obj_add_style(s_ui.vm_rows[i], &s_muted_button_style, 0);
        lv_obj_set_pos(s_ui.vm_rows[i], 10, y);
        lv_obj_set_size(s_ui.vm_rows[i], 420, 20);
        lv_obj_add_event_cb(s_ui.vm_rows[i], vm_row_event, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        s_ui.vm_row_dots[i] = make_status_dot(s_ui.vm_rows[i], 5, 6, 8, COLOR_GRAY);
        s_ui.vm_row_labels[i] = make_label(s_ui.vm_rows[i], "", 17, 2, 92, &app_font_14, COLOR_TEXT);
        s_ui.vm_row_ips[i] = make_label(s_ui.vm_rows[i], "--", 110, 2, 108, &app_font_14, COLOR_MUTED);
        s_ui.vm_row_cpu[i] = make_label(s_ui.vm_rows[i], "--", 220, 2, 38, &app_font_14, COLOR_MUTED);
        s_ui.vm_row_memory[i] = make_label(s_ui.vm_rows[i], "--", 260, 2, 72, &app_font_14, COLOR_MUTED);
        s_ui.vm_row_disk[i] = make_label(s_ui.vm_rows[i], "--", 334, 2, 82, &app_font_14, COLOR_MUTED);
    }

    s_ui.vm_detail = make_surface(page, 8, 34, 464, 246);
    s_ui.vm_detail_title = make_label(s_ui.vm_detail, "VM --", 18, 16, 420,
                                      &app_font_18, COLOR_TEXT);
    s_ui.vm_detail_status_dot = make_status_dot(s_ui.vm_detail, 18, 55, 8, COLOR_GRAY);
    s_ui.vm_detail_status = make_label(s_ui.vm_detail, "--", 32, 50, 180,
                                       &app_font_14, COLOR_MUTED);
    s_ui.vm_detail_ip = make_label(s_ui.vm_detail, "IP  --", 18, 86, 205,
                                   &app_font_14, COLOR_TEXT);
    s_ui.vm_detail_cpu = make_label(s_ui.vm_detail, "CPU  --", 232, 86, 210,
                                    &app_font_14, COLOR_TEXT);
    s_ui.vm_detail_memory = make_label(s_ui.vm_detail, "内存  --", 18, 124, 205,
                                       &app_font_14, COLOR_TEXT);
    s_ui.vm_detail_disk = make_label(s_ui.vm_detail, "磁盘  --", 232, 124, 210,
                                     &app_font_14, COLOR_TEXT);
    s_ui.vm_detail_uptime = make_label(s_ui.vm_detail, "运行时间  --", 18, 164, 205,
                                       &app_font_14, COLOR_TEXT);
    s_ui.vm_detail_agent = make_label(s_ui.vm_detail, "Guest Agent  --", 232, 164, 210,
                                      &app_font_14, COLOR_MUTED);
    set_hidden(s_ui.vm_detail, true);
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
    set_hidden(s_ui.nas_overview, true);
    set_hidden(s_ui.nas_detail, false);
    update_nas_disks();
}

static void nas_pool_list_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) show_nas_disks();
}

static void nas_disk_list_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    lv_obj_t *target = lv_event_get_target(event);
    if (target == s_ui.nas_disk_previous || target == s_ui.nas_disk_next) return;
    show_nas_pools();
}

static void nas_disk_page_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    const int direction = (int)(intptr_t)lv_event_get_user_data(event);
    const int snapshot_disk_count = s_ui.snapshot.nas_disk_count > APP_MAX_NAS_DISKS ?
                                    APP_MAX_NAS_DISKS : (int)s_ui.snapshot.nas_disk_count;
    if (direction < 0 && s_ui.nas_disk_offset >= NAS_DISK_VISIBLE) {
        s_ui.nas_disk_offset -= NAS_DISK_VISIBLE;
    } else if (direction > 0 &&
               s_ui.nas_disk_offset + NAS_DISK_VISIBLE < snapshot_disk_count &&
               s_ui.nas_disk_offset <= APP_MAX_NAS_DISKS - NAS_DISK_VISIBLE) {
        s_ui.nas_disk_offset += NAS_DISK_VISIBLE;
    }
    update_nas_disks();
}

static void update_nas_disks(void)
{
    const app_snapshot_t *snapshot = &s_ui.snapshot;
    const bool nas_live = snapshot->nas_online && !snapshot->nas_stale;
    char value[96];
    if (!nas_live) {
        lv_label_set_text(s_ui.nas_disk_empty,
                          snapshot->nas_stale ? "NAS 离线 · 缓存" : "NAS 离线");
        set_hidden(s_ui.nas_disk_empty, false);
        set_hidden(s_ui.nas_disk_previous, true);
        set_hidden(s_ui.nas_disk_next, true);
        set_hidden(s_ui.nas_disk_page, true);
        s_ui.nas_disk_offset = 0;
        for (int i = 0; i < NAS_DISK_VISIBLE; ++i) set_hidden(s_ui.nas_disk_rows[i], true);
        return;
    }

    const int snapshot_disk_count = snapshot->nas_disk_count > APP_MAX_NAS_DISKS ?
                                    APP_MAX_NAS_DISKS : (int)snapshot->nas_disk_count;
    const int last_page_offset = snapshot_disk_count > NAS_DISK_VISIBLE ?
        ((snapshot_disk_count - 1) / NAS_DISK_VISIBLE) * NAS_DISK_VISIBLE : 0;
    if (s_ui.nas_disk_offset > last_page_offset) s_ui.nas_disk_offset = last_page_offset;
    const int remaining_disks = snapshot_disk_count - s_ui.nas_disk_offset;
    const int visible_disks = remaining_disks < NAS_DISK_VISIBLE ?
                              remaining_disks : NAS_DISK_VISIBLE;
    const int page_count = snapshot_disk_count > 0 ?
                           (snapshot_disk_count + NAS_DISK_VISIBLE - 1) / NAS_DISK_VISIBLE : 1;
    set_hidden(s_ui.nas_disk_empty, visible_disks > 0);
    if (visible_disks == 0) lv_label_set_text(s_ui.nas_disk_empty, "未获取到物理盘信息");
    lv_label_set_text_fmt(s_ui.nas_disk_page, "%d/%d",
                          s_ui.nas_disk_offset / NAS_DISK_VISIBLE + 1, page_count);
    set_hidden(s_ui.nas_disk_page, snapshot_disk_count == 0);
    set_hidden(s_ui.nas_disk_previous, s_ui.nas_disk_offset == 0);
    set_hidden(s_ui.nas_disk_next,
               s_ui.nas_disk_offset + NAS_DISK_VISIBLE >= snapshot_disk_count);
    for (int i = 0; i < NAS_DISK_VISIBLE; ++i) {
        const bool visible = i < visible_disks;
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
    lv_obj_set_size(s_ui.nas_overview, 480, 244);
    lv_obj_clear_flag(s_ui.nas_overview, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_ui.nas_overview, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ui.nas_overview, nas_pool_list_event, LV_EVENT_CLICKED, NULL);
    s_ui.nas_overview_message = make_label(s_ui.nas_overview, "NAS 未连接", 16, 92,
                                            448, &app_font_14, COLOR_MUTED);
    lv_obj_set_style_text_align(s_ui.nas_overview_message, LV_TEXT_ALIGN_CENTER, 0);
    for (int i = 0; i < NAS_VISIBLE; ++i) {
        s_ui.nas_rows[i] = lv_btn_create(s_ui.nas_overview);
        lv_obj_remove_style_all(s_ui.nas_rows[i]);
        lv_obj_add_style(s_ui.nas_rows[i], &s_muted_button_style, 0);
        lv_obj_set_pos(s_ui.nas_rows[i], 8, 4 + i * 59);
        lv_obj_set_size(s_ui.nas_rows[i], 464, 56);
        lv_obj_add_event_cb(s_ui.nas_rows[i], nas_pool_list_event, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);
        s_ui.nas_row_status_dots[i] = make_status_dot(s_ui.nas_rows[i], 9, 8, 8,
                                                       COLOR_RED);
        s_ui.nas_row_labels[i] = make_label(s_ui.nas_rows[i], "", 22, 3, 82,
                                             &app_font_14, COLOR_TEXT);
        s_ui.nas_row_descriptions[i] = make_label(s_ui.nas_rows[i], "", 106, 3, 78,
                                                   &app_font_14, COLOR_MUTED);
        s_ui.nas_row_values[i] = make_label(s_ui.nas_rows[i], "", 186, 3, 268,
                                             &app_font_14, COLOR_TEXT);
        lv_obj_set_style_text_align(s_ui.nas_row_values[i], LV_TEXT_ALIGN_RIGHT, 0);
        s_ui.nas_row_bars[i] = make_split_bar(s_ui.nas_rows[i], 9, 34, 446, 6, 0, 100);
        lv_obj_clear_flag(s_ui.nas_row_status_dots[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(s_ui.nas_row_bars[i], LV_OBJ_FLAG_CLICKABLE);
        set_hidden(s_ui.nas_rows[i], true);
    }

    s_ui.nas_detail = make_surface(page, 8, 4, 464, 236);
    lv_obj_add_flag(s_ui.nas_detail, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ui.nas_detail, nas_disk_list_event, LV_EVENT_CLICKED, NULL);
    make_label(s_ui.nas_detail, "NAS 物理盘（未按池映射）", 12, 10, 238,
               &app_font_14, COLOR_MUTED);
    s_ui.nas_disk_previous = make_button(s_ui.nas_detail, "^", 428, 0, 28, 98,
                                         nas_disk_page_event, (void *)(intptr_t)-1);
    s_ui.nas_disk_page = make_label(s_ui.nas_detail, "1/1", 428, 106, 28,
                                    &app_font_14, COLOR_MUTED);
    lv_obj_set_style_text_align(s_ui.nas_disk_page, LV_TEXT_ALIGN_CENTER, 0);
    s_ui.nas_disk_next = make_button(s_ui.nas_detail, "v", 428, 130, 28, 106,
                                     nas_disk_page_event, (void *)(intptr_t)1);
    set_hidden(s_ui.nas_disk_previous, true);
    set_hidden(s_ui.nas_disk_next, true);
    set_hidden(s_ui.nas_disk_page, true);
    make_label(s_ui.nas_detail, "硬盘", 22, 44, 80, &app_font_14, COLOR_MUTED);
    make_label(s_ui.nas_detail, "型号", 102, 44, 216, &app_font_14, COLOR_MUTED);
    make_label(s_ui.nas_detail, "温度", 318, 44, 86, &app_font_14, COLOR_MUTED);
    s_ui.nas_disk_empty = make_label(s_ui.nas_detail, "未获取到物理盘信息", 12, 112,
                                      440, &app_font_14, COLOR_MUTED);
    lv_obj_set_style_text_align(s_ui.nas_disk_empty, LV_TEXT_ALIGN_CENTER, 0);
    for (int i = 0; i < NAS_DISK_VISIBLE; ++i) {
        lv_obj_t *row = lv_obj_create(s_ui.nas_detail);
        s_ui.nas_disk_rows[i] = row;
        lv_obj_remove_style_all(row);
        lv_obj_set_pos(row, 8, 64 + i * 40);
        lv_obj_set_size(row, 412, 36);
        lv_obj_set_style_border_color(row, color(COLOR_LINE), 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, nas_disk_list_event, LV_EVENT_CLICKED, NULL);
        s_ui.nas_disk_dots[i] = make_status_dot(row, 3, 14, 7, COLOR_RED);
        s_ui.nas_disk_ids[i] = make_label(row, "--", 14, 9, 80, &app_font_14, COLOR_TEXT);
        s_ui.nas_disk_models[i] = make_label(row, "--", 94, 9, 216, &app_font_14, COLOR_TEXT);
        s_ui.nas_disk_temperatures[i] = make_label(row, "--", 310, 9, 86,
                                                    &app_font_14, COLOR_TEXT);
        lv_obj_clear_flag(s_ui.nas_disk_dots[i], LV_OBJ_FLAG_CLICKABLE);
        set_hidden(row, true);
    }
    set_hidden(s_ui.nas_detail, true);

    lv_obj_t *footer = make_surface(page, 8, 248, 464, NAS_METRICS_HEIGHT);
    s_ui.nas_cpu_value = make_label(footer, "CPU --", 8, 1, 160,
                                    &app_font_14, COLOR_TEXT);
    s_ui.nas_memory_value = make_label(footer, "内存 --", 168, 1, 170,
                                       &app_font_14, COLOR_TEXT);
    s_ui.nas_temperature_value = make_label(footer, "温度 --", 338, 1, 118,
                                            &app_font_14, COLOR_TEXT);
    s_ui.nas_network_prefix = make_label(footer, "网", 8, 19, 18,
                                         &app_font_14, COLOR_TEXT);
    s_ui.nas_upload_value = make_label(footer, "↑--", 26, 19, 66,
                                       &app_font_14, COLOR_TEXT);
    s_ui.nas_download_value = make_label(footer, "↓--", 94, 19, 66,
                                         &app_font_14, COLOR_TEXT);
    s_ui.nas_ip_value = make_label(footer, "IP --", 168, 19, 170,
                                   &app_font_14, COLOR_TEXT);
    s_ui.nas_uptime_value = make_label(footer, "运行 --", 338, 19, 118,
                                       &app_font_14, COLOR_TEXT);
    lv_obj_set_style_text_align(s_ui.nas_cpu_value, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_align(s_ui.nas_memory_value, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_align(s_ui.nas_temperature_value, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_align(s_ui.nas_network_prefix, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_align(s_ui.nas_upload_value, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_align(s_ui.nas_download_value, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_align(s_ui.nas_ip_value, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_align(s_ui.nas_uptime_value, LV_TEXT_ALIGN_LEFT, 0);
    show_nas_pools();
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
    make_label(monitor, "刷新", 10, 12, 42, &app_font_14, COLOR_MUTED);
    static const char *const labels[REFRESH_OPTION_COUNT] = {"5 秒", "10 秒", "30 秒", "60 秒"};
    static const uint8_t intervals[REFRESH_OPTION_COUNT] = {5, 10, 30, 60};
    for (size_t i = 0; i < REFRESH_OPTION_COUNT; ++i) {
        s_ui.refresh_buttons[i] = make_button(monitor, labels[i], 58 + (int)i * 72, 5, 66, 34,
                                              refresh_event, (void *)(uintptr_t)intervals[i]);
    }
    set_refresh_button_state();
    make_label(monitor, "监控配置", 10, 70, 82, &app_font_14, COLOR_MUTED);
    make_label(monitor, "PVE / 群晖只读访问", 96, 70, 196, &app_font_14, COLOR_TEXT);
    make_button(monitor, "PVE Token 设置", 314, 54, 136, 42,
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
    s_ui.nas_nav_button = make_button(bar, "NAS", 4, 3, 76, 26,
                                      page_event, (void *)(intptr_t)0);
    s_ui.time_label = make_label(bar, "--:--", 84, 7, 52, &app_font_14, COLOR_TEXT);
    lv_obj_set_style_text_align(s_ui.time_label, LV_TEXT_ALIGN_CENTER, 0);
    s_ui.ip_label = make_label(bar, "WiFi未连接", 140, 7, 166, &app_font_14, COLOR_MUTED);
    lv_obj_set_style_text_align(s_ui.ip_label, LV_TEXT_ALIGN_CENTER, 0);
    s_ui.pve_nav_button = make_button(bar, "PVE", 310, 3, 76, 26,
                                      page_event, (void *)(intptr_t)1);
    s_ui.settings_nav_button = make_button(bar, "设置", 390, 3, 86, 26,
                                           page_event, (void *)(intptr_t)2);
}

esp_err_t dashboard_ui_create(void)
{
    if (s_ui.root != NULL) return ESP_ERR_INVALID_STATE;
    uint8_t saved_theme = THEME_GRAPHITE;
    uint8_t saved_refresh = 5;
    if (device_settings_get_u8(DEVICE_KEY_THEME, &saved_theme) == ESP_OK && saved_theme < THEME_COUNT)
        s_theme_id = saved_theme;
    else s_theme_id = THEME_GRAPHITE;
    if (device_settings_get_u8(DEVICE_KEY_REFRESH, &saved_refresh) == ESP_OK &&
        (saved_refresh == 5 || saved_refresh == 10 ||
         saved_refresh == 30 || saved_refresh == 60))
        s_refresh_seconds = saved_refresh;
    else s_refresh_seconds = 5;
    memset(&s_ui, 0, sizeof(s_ui));
    s_ui.active_page = 1;
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
    set_hidden(s_ui.pages[0], true);
    set_hidden(s_ui.pages[2], true);
    set_nav_button_state();
    app_model_set_active_monitor(APP_MONITOR_PVE);
    app_model_set_refresh_seconds(s_refresh_seconds);
    s_ui.clock_timer = lv_timer_create(clock_timer_event, 1000, NULL);
    clock_timer_event(s_ui.clock_timer);
    ESP_LOGI("dashboard_ui", "Created NAS/PVE/Settings LVGL pages");
    return ESP_OK;
}

static void update_pve(void)
{
    const app_snapshot_t *snapshot = &s_ui.snapshot;
    char buffer[200], total[24], used[24];
    if (snapshot->pve_online) {
        snprintf(buffer, sizeof(buffer), "pve name:%s version:%s %s 在线",
                 snapshot->pve_name[0] ? snapshot->pve_name : "p330",
                 snapshot->pve_version[0] ? snapshot->pve_version : "--", snapshot->pve_host);
        lv_obj_set_style_text_color(s_ui.pve_identity, color(COLOR_GREEN), 0);
        lv_obj_set_style_bg_color(s_ui.pve_status_dot, color(COLOR_GREEN), 0);
    } else {
        snprintf(buffer, sizeof(buffer), "PVE %s  %s",
                 snapshot->pve_configured ? "离线" : "未配置",
                 snapshot->pve_last_error);
        lv_obj_set_style_text_color(s_ui.pve_identity, color(COLOR_MUTED), 0);
        lv_obj_set_style_bg_color(s_ui.pve_status_dot, color(COLOR_GRAY), 0);
    }
    lv_label_set_text(s_ui.pve_identity, buffer);
    format_percent(buffer, sizeof(buffer), snapshot->pve_cpu_percent);
    lv_label_set_text(s_ui.pve_cpu_value, buffer);
    format_bytes(used, sizeof(used), snapshot->pve_memory_used);
    format_bytes(total, sizeof(total), snapshot->pve_memory_total);
    lv_label_set_text_fmt(s_ui.pve_memory_value, "%s/%s", used, total);
    format_bytes(used, sizeof(used), snapshot->pve_storage_used);
    format_bytes(total, sizeof(total), snapshot->pve_storage_total);
    lv_label_set_text_fmt(s_ui.pve_storage_value, "%s/%s", used, total);
    snprintf(buffer, sizeof(buffer), "%.1f %.1f %.1f",
             snapshot->pve_load[0], snapshot->pve_load[1], snapshot->pve_load[2]);
    lv_label_set_text(s_ui.pve_load_value, buffer);
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
    lv_label_set_text_fmt(s_ui.vm_title, "虚拟机 %" PRIu32 "/%" PRIu32 " 运行",
                          snapshot->pve_running_count, snapshot->pve_guest_count);
    if (s_ui.selected_vm_index >= (int)snapshot->pve_guest_count) show_pve_overview();
    update_vm_navigation();
    const int max_offset = snapshot->pve_guest_count > VM_VISIBLE ?
                           (int)snapshot->pve_guest_count - VM_VISIBLE : 0;
    if (s_ui.vm_list_offset > max_offset) s_ui.vm_list_offset = max_offset;
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
        char cpu[16], capacity[52], disk_used[24], disk_total[24];
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
        if (guest->disk_total == 0) {
            lv_label_set_text(s_ui.vm_row_disk[i], "--");
        } else {
            format_bytes(disk_used, sizeof(disk_used), guest->disk_used);
            format_bytes(disk_total, sizeof(disk_total), guest->disk_total);
            snprintf(capacity, sizeof(capacity), "%s/%s", disk_used, disk_total);
            lv_label_set_text(s_ui.vm_row_disk[i], capacity);
        }
        lv_obj_remove_event_cb(s_ui.vm_rows[i], vm_row_event);
        lv_obj_add_event_cb(s_ui.vm_rows[i], vm_row_event, LV_EVENT_CLICKED, (void *)(intptr_t)index);
    }
    if (s_ui.selected_vm_index >= 0) update_vm_detail();
}

static void update_nas(void)
{
    const app_snapshot_t *snapshot = &s_ui.snapshot;
    char used[24], total[24], free_space[24], uptime[32];
    const bool nas_live = snapshot->nas_online && !snapshot->nas_stale;
    const bool has_pools = snapshot->nas_pool_count > 0;
    set_hidden(s_ui.nas_overview_message, has_pools);
    if (!has_pools) {
        if (!snapshot->nas_configured)
            lv_label_set_text_fmt(s_ui.nas_overview_message, "NAS 未配置  %s",
                                  snapshot->nas_last_error);
        else if (nas_live)
            lv_label_set_text(s_ui.nas_overview_message, "NAS 在线  未发现存储池");
        else if (snapshot->nas_stale)
            lv_label_set_text(s_ui.nas_overview_message, "NAS 离线 · 缓存");
        else
            lv_label_set_text_fmt(s_ui.nas_overview_message, "NAS 离线  %s",
                                  snapshot->nas_last_error);
    }
    for (int i = 0; i < NAS_VISIBLE; ++i) {
        const bool visible = i < (int)snapshot->nas_pool_count;
        set_hidden(s_ui.nas_rows[i], !visible);
        if (!visible) continue;
        const nas_pool_t *pool = &snapshot->nas_pools[i];
        const bool healthy = nas_live && pool->healthy;
        lv_label_set_text_fmt(s_ui.nas_row_labels[i], "存储池%d", i + 1);
        lv_obj_set_style_bg_color(s_ui.nas_row_status_dots[i],
                                  color(healthy ? COLOR_GREEN : COLOR_RED), 0);
        lv_label_set_text(s_ui.nas_row_descriptions[i], healthy ? "" :
                          (!nas_live ? (snapshot->nas_stale ? "离线 · 缓存" : "离线") :
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
        lv_label_set_text_fmt(s_ui.nas_cpu_value, "CPU %s", used);
    } else {
        lv_label_set_text(s_ui.nas_cpu_value, "CPU --");
    }
    if (nas_live && snapshot->nas_memory_valid) {
        format_percent(used, sizeof(used), snapshot->nas_memory_percent);
        lv_label_set_text_fmt(s_ui.nas_memory_value, "内存 %s", used);
    } else {
        lv_label_set_text(s_ui.nas_memory_value, "内存 --");
    }
    if (nas_live && snapshot->nas_temperature_valid) {
        lv_label_set_text_fmt(s_ui.nas_temperature_value, "温度 %d°C", snapshot->nas_temperature);
    } else {
        lv_label_set_text(s_ui.nas_temperature_value, "温度 --");
    }
    if (nas_live && snapshot->nas_network_rate_valid) {
        char upload[24], download[24];
        format_rate(upload, sizeof(upload), snapshot->nas_tx_bytes_per_second);
        format_rate(download, sizeof(download), snapshot->nas_rx_bytes_per_second);
        lv_label_set_text_fmt(s_ui.nas_upload_value, "↑%s", upload);
        lv_label_set_text_fmt(s_ui.nas_download_value, "↓%s", download);
    } else {
        lv_label_set_text(s_ui.nas_upload_value, "↑--");
        lv_label_set_text(s_ui.nas_download_value, "↓--");
    }
    lv_label_set_text_fmt(s_ui.nas_ip_value, "IP %s",
                          snapshot->nas_configured && snapshot->nas_host[0] ?
                          snapshot->nas_host : "--");
    if (nas_live && snapshot->nas_uptime_valid) {
        format_uptime(uptime, sizeof(uptime), snapshot->nas_uptime_seconds);
        lv_label_set_text_fmt(s_ui.nas_uptime_value, "运行 %s", uptime);
    } else {
        lv_label_set_text(s_ui.nas_uptime_value, "运行 --");
    }
    if (s_ui.nas_disks_visible) update_nas_disks();
}

void dashboard_ui_update(const app_snapshot_t *snapshot)
{
    if (snapshot == NULL || s_ui.root == NULL) return;
    s_ui.snapshot = *snapshot;
    s_ui.has_snapshot = true;
    lv_label_set_text(s_ui.ip_label,
                      snapshot->wifi_connected && snapshot->ip_address[0] != '\0' ?
                      snapshot->ip_address : "WiFi未连接，请到设置里面设置");
    update_pve();
    update_nas();
}
