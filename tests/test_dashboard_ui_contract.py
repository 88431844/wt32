from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
UI = (ROOT / "components/dashboard_ui/dashboard_ui.c").read_text(encoding="utf-8")
ICONS_HEADER = ROOT / "components/dashboard_ui/include/dashboard_icons.h"
ICONS_SOURCE = ROOT / "components/dashboard_ui/assets/dashboard_icons.c"

class DashboardUiContractTest(unittest.TestCase):
    def test_nas_pool_and_disk_lists_page_four_dynamic_items(self):
        self.assertIn("#define NAS_VISIBLE 4", UI)
        self.assertIn("#define NAS_DISK_VISIBLE 4", UI)
        for marker in ("nas_pool_offset", "nas_pool_previous", "nas_pool_next", "nas_pool_page",
                       "snapshot->nas_pool_count", "snapshot->nas_disk_count"):
            self.assertIn(marker, UI)
        self.assertNotIn("APP_MAX_NAS_", UI)

    def test_nas_list_body_toggles_and_pagers_do_not_toggle(self):
        for marker in ("nas_pool_list_event", "show_nas_disks", "nas_disk_list_event",
                       "show_nas_pools", "lv_event_stop_bubbling(event)"):
            self.assertIn(marker, UI)
        self.assertNotIn("capacity_bytes", UI)

    def test_loading_offline_and_refresh_failure_are_distinct(self):
        for marker in ('"数据加载中"', "APP_MODEL_EVENT_LOADING", "APP_MODEL_EVENT_OFFLINE",
                       "APP_MODEL_EVENT_REFRESH_FAILED", '"刷新失败，已显示上次数据"'):
            self.assertIn(marker, UI)
        self.assertNotIn("离线 · 缓存", UI)

    def test_refresh_failure_toast_is_centered_transient_and_noninteractive(self):
        for marker in ("show_refresh_toast", "lv_obj_center(s_ui.toast)",
                       "lv_obj_clear_flag(s_ui.toast, LV_OBJ_FLAG_CLICKABLE)",
                       "LV_OPA_TRANSP", "2500"):
            self.assertIn(marker, UI)

    def test_nas_footer_has_final_balanced_geometry(self):
        self.assertIn("#define NAS_METRICS_HEIGHT 52", UI)
        self.assertIn("NAS_METRIC_CELL_WIDTH 55", UI)
        self.assertIn("NAS_FOOTER_NETWORK_WIDTH 80", UI)
        self.assertIn("NAS_FOOTER_IP_WIDTH 130", UI)
        self.assertIn("NAS_FOOTER_UPTIME_WIDTH 58", UI)
        self.assertIn("NAS_FOOTER_STATIC_Y 18", UI)
        self.assertIn("NAS_FOOTER_Y 236", UI)
        self.assertIn("make_network_metric_row", UI)
        self.assertIn("LV_FLEX_ALIGN_CENTER", UI)
        self.assertIn("&dashboard_icon_globe", UI)
        self.assertNotIn("&dashboard_icon_ip", UI)
        for unit in ('"B"', '"K"', '"M"', '"G"', '"T"'):
            self.assertIn(unit, UI)
        self.assertNotIn('"CPU %s"', UI)
        nas_start = UI.index("static void update_nas(void)\n{")
        nas_end = UI.index("static void", nas_start + len("static void update_nas(void)\n{"))
        update_nas = UI[nas_start : nas_end]
        self.assertNotIn('"IP %s"', update_nas)

    def test_nas_disk_headers_sort_full_collection_and_keep_stable_pager(self):
        self.assertNotIn('"NAS 物理盘（未按池映射）"', UI)
        for marker in ("nas_disk_sort_event", "nas_disk_sort(",
                       "nas_disk_sort_key", "nas_disk_sort_descending",
                       "nas_disk_sort_buttons", "lv_event_stop_bubbling(event)",
                       "nas_disk_pager", "nas_disk_pager_event",
                       "LV_STATE_DISABLED", "LV_OPA_30",
                       "420, 0", "44, 228", "4, 0, 36, 82",
                       "34 + i * 46", "412, 46"):
            self.assertIn(marker, UI)
        sort_call = UI.index("nas_disk_sort(snapshot->nas_disks")
        page_slice = UI.index("snapshot->nas_disks[s_ui.nas_disk_offset + i]")
        self.assertLess(sort_call, page_slice)

    def test_opening_disk_list_restores_default_sort_and_first_page(self):
        callback = UI[UI.index("static void show_nas_disks") :]
        callback = callback[: callback.index("static void", 20)]
        for marker in ("s_ui.nas_disk_sort_key = NAS_DISK_SORT_ID",
                       "s_ui.nas_disk_sort_descending = false",
                       "s_ui.nas_disk_offset = 0"):
            self.assertIn(marker, callback)
            self.assertLess(callback.index(marker), callback.index("update_nas_disks()"))

    def test_top_brand_icon_and_label_are_both_vertically_centered(self):
        button = UI[UI.index("static lv_obj_t *make_button"):]
        button = button[:button.index("static void", 20)]
        self.assertIn("lv_obj_center(text_label)", button)
        icon_button = UI[UI.index("static lv_obj_t *make_icon_button"):]
        icon_button = icon_button[:icon_button.index("static void", 20)]
        self.assertIn("(height - 16) / 2", icon_button)

    def test_nas_pool_header_elements_share_one_baseline(self):
        for marker in ("NAS_POOL_ROW_HEIGHT 52", "NAS_POOL_ROW_STEP 55",
                       "NAS_POOL_ICON_Y 7", "NAS_POOL_STATUS_Y 11",
                       "NAS_POOL_TEXT_Y 7", "NAS_POOL_BAR_Y 38"):
            self.assertIn(marker, UI)

    def test_nas_pool_rows_fill_hidden_pager_column(self):
        for marker in ("NAS_POOL_ROW_WIDTH_PAGED 420", "NAS_POOL_ROW_WIDTH_FULL 464",
                       "NAS_POOL_BAR_WIDTH_PAGED 402", "NAS_POOL_BAR_WIDTH_FULL 446",
                       "set_nas_pool_pager_layout",
                       "snapshot->nas_pool_count > NAS_VISIBLE"):
            self.assertIn(marker, UI)

    def test_brand_icons_keep_official_colors(self):
        icons = ICONS_SOURCE.read_text(encoding="utf-8")
        self.assertIn("COLOR_ICON_DESCRIPTOR(dashboard_icon_dsm", icons)
        self.assertIn("COLOR_ICON_DESCRIPTOR(dashboard_icon_proxmox", icons)
        self.assertIn("LV_IMG_CF_TRUE_COLOR_ALPHA", icons)
        self.assertIn("make_brand_icon", UI)
        icon_button = UI[UI.index("static lv_obj_t *make_icon_button"):]
        icon_button = icon_button[:icon_button.index("static void", 20)]
        self.assertIn("make_brand_icon(button, icon", icon_button)
        self.assertNotIn("make_icon(button, icon", icon_button)

    def test_offline_icon_assets_cover_brand_hardware_and_status(self):
        self.assertTrue(ICONS_HEADER.exists())
        self.assertTrue(ICONS_SOURCE.exists())
        icons = ICONS_HEADER.read_text(encoding="utf-8")
        for name in ("dashboard_icon_dsm", "dashboard_icon_proxmox", "dashboard_icon_processor",
                     "dashboard_icon_memory", "dashboard_icon_temperature", "dashboard_icon_hdd",
                     "dashboard_icon_pool", "dashboard_icon_globe", "dashboard_icon_uptime",
                     "dashboard_icon_upload", "dashboard_icon_download"):
            self.assertIn(name, icons)

    def test_settings_homepage_selector_commits_before_navigation(self):
        self.assertIn("homepage_event", UI)
        self.assertIn("DEVICE_KEY_HOME_PAGE", UI)
        callback = UI[UI.index("static void homepage_event"):]
        callback = callback[:callback.index("static void", 20)]
        self.assertLess(callback.index("device_settings_set_u8"), callback.index("s_ui.active_page"))
        for marker in ("homepage_buttons", "saved_homepage", "saved_homepage > 1"):
            self.assertIn(marker, UI)

    def test_top_navigation_does_not_persist_homepage(self):
        callback = UI[UI.index("static void page_event"):]
        callback = callback[:callback.index("static void", 20)]
        self.assertNotIn("DEVICE_KEY_HOME_PAGE", callback)

    def test_pve_identity_row_uses_three_fields_and_two_theme_aware_separators(self):
        for marker in (
            "lv_obj_t *pve_name;",
            "lv_obj_t *pve_version;",
            "lv_obj_t *pve_host;",
            "lv_obj_t *pve_identity_dividers[2];",
        ):
            self.assertIn(marker, UI)
        self.assertNotIn("lv_obj_t *pve_identity;", UI)
        creation = UI[UI.index("lv_obj_t *summary = make_surface") : UI.index('make_label(summary, "CPU"')]
        compact_creation = " ".join(creation.split())
        for marker in (
            'make_label(summary, "name --", 24, 8, 132, &app_font_14, COLOR_TEXT)',
            "make_rule(summary, 160, 7, 1, 20)",
            'make_label(summary, "version --", 170, 8, 132, &app_font_14, COLOR_TEXT)',
            "make_rule(summary, 306, 7, 1, 20)",
            'make_label(summary, "IP --", 316, 8, 132, &app_font_14, COLOR_TEXT)',
        ):
            self.assertIn(marker, compact_creation)

        update_pve = UI[UI.index("static void update_pve(void)\n{") : UI.index("static void update_nas(void)\n{")]
        compact_update = " ".join(update_pve.split())
        for marker in (
            'lv_label_set_text_fmt(s_ui.pve_name, "name %s", snapshot->pve_name[0] ? snapshot->pve_name : "p330")',
            'lv_label_set_text_fmt(s_ui.pve_version, "version %s", snapshot->pve_version[0] ? snapshot->pve_version : "--")',
            'lv_label_set_text_fmt(s_ui.pve_host, "IP %s", snapshot->pve_host[0] ? snapshot->pve_host : "--")',
        ):
            self.assertIn(marker, compact_update)
        for marker in (
            "lv_obj_set_style_bg_color(s_ui.pve_status_dot, color(COLOR_GREEN), 0)",
            'lv_label_set_text(s_ui.pve_name, snapshot->pve_configured ? "PVE 离线" : "PVE 未配置")',
            'lv_label_set_text(s_ui.pve_version, "--")',
            'lv_label_set_text(s_ui.pve_host, "--")',
            "lv_obj_set_style_bg_color(s_ui.pve_status_dot, color(COLOR_GRAY), 0)",
        ):
            self.assertIn(marker, compact_update)
        self.assertNotIn('"pve name:%s version:%s %s 在线"', update_pve)
        self.assertNotIn("在线", update_pve)

        monitor_message = UI[UI.index("static void show_monitor_message") : UI.index("void dashboard_ui_update")]
        compact_message = " ".join(monitor_message.split())
        for marker in (
            "lv_label_set_text(s_ui.pve_name, message)",
            'lv_label_set_text(s_ui.pve_version, "--")',
            'lv_label_set_text(s_ui.pve_host, "--")',
            "lv_obj_set_style_bg_color(s_ui.pve_status_dot, color(COLOR_GRAY), 0)",
        ):
            self.assertIn(marker, compact_message)
        for field in ("pve_name", "pve_version", "pve_host"):
            self.assertNotIn(f"lv_obj_set_style_text_color(s_ui.{field}", update_pve)
            self.assertNotIn(f"lv_obj_set_style_text_color(s_ui.{field}", monitor_message)

        apply_theme = UI[UI.index("static void apply_theme(void)") : UI.index("static void page_event")]
        compact_theme = "".join(apply_theme.split())
        self.assertIn("for(size_ti=0;i<2;++i)", compact_theme)
        self.assertIn("if(s_ui.pve_identity_dividers[i]!=NULL)", compact_theme)
        self.assertIn(
            "lv_obj_set_style_bg_color(s_ui.pve_identity_dividers[i],color(COLOR_LINE),0)",
            compact_theme,
        )

if __name__ == "__main__":
    unittest.main()
