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
        self.assertIn("#define NAS_METRICS_HEIGHT 42", UI)
        self.assertIn("NAS_METRIC_CELL_WIDTH 68", UI)
        self.assertIn("NAS_METRIC_DYNAMIC_COUNT 4", UI)
        self.assertIn("NAS_FOOTER_IP_WIDTH 112", UI)
        self.assertIn("NAS_FOOTER_UPTIME_WIDTH 48", UI)
        self.assertIn("NAS_FOOTER_STATIC_Y 13", UI)
        self.assertIn("&dashboard_icon_globe", UI)
        self.assertNotIn("&dashboard_icon_ip", UI)
        for unit in ('"B"', '"K"', '"M"', '"G"', '"T"'):
            self.assertIn(unit, UI)
        self.assertNotIn('"CPU %s"', UI)
        self.assertNotIn('"IP %s"', UI)

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

if __name__ == "__main__":
    unittest.main()
