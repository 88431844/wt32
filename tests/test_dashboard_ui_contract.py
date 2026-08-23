from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
UI = (ROOT / "components/dashboard_ui/dashboard_ui.c").read_text(encoding="utf-8")
ICONS_HEADER = ROOT / "components/dashboard_ui/include/dashboard_icons.h"
ICONS_SOURCE = ROOT / "components/dashboard_ui/assets/dashboard_icons.c"
FONT_14 = ROOT / "components/dashboard_ui/fonts/app_font_14.c"

class DashboardUiContractTest(unittest.TestCase):
    def test_14px_font_covers_all_static_chinese_ui_text(self):
        font_source = FONT_14.read_text(encoding="utf-8")
        symbols_match = re.search(r"--symbols (.*?) --no-compress", font_source, re.DOTALL)
        self.assertIsNotNone(symbols_match)
        font_symbols = set(symbols_match.group(1))
        ui_characters = set(re.findall(r"[\u4e00-\u9fff]", UI))
        self.assertEqual(set(), ui_characters - font_symbols)

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

    def test_top_wifi_status_updates_independently_of_monitor_snapshots(self):
        clock = UI[
            UI.index("static void clock_timer_event"):
            UI.index("static void create_settings_page")
        ]
        self.assertIn("network_manager_is_connected()", clock)
        self.assertIn("network_manager_get_ip", clock)
        self.assertIn("s_ui.ip_label", clock)

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

    def test_pve_node_view_has_approved_geometry_and_fields(self):
        for marker in (
            "#define VM_VISIBLE 4",
            "#define PVE_IDENTITY_HEIGHT 40",
            "#define PVE_METRIC_ROW_HEIGHT 64",
            "#define PVE_PROCESSOR_ROW_HEIGHT 42",
            "#define PVE_NARROW_WIDTH 168",
            "#define PVE_VM_BUTTON_Y 224",
            "#define PVE_VM_HIT_HEIGHT 56",
            "#define PVE_VM_VISUAL_HEIGHT 40",
            "lv_obj_t *pve_node_view;",
            "lv_obj_t *pve_uptime;",
            "lv_obj_t *pve_cpu_model;",
            "lv_obj_t *pve_vm_list_button;",
            "lv_obj_t *pve_vm_list_visual;",
            "lv_obj_t *pve_vm_list_label;",
            "lv_obj_t *pve_load_values[3];",
        ):
            self.assertIn(marker, UI)
        for label in ('"CPU"', '"系统负载"', '"内存"', '"存储"',
                      '"处理器"', '"虚拟机列表"', '"1分"', '"5分"', '"15分"'):
            self.assertIn(label, UI)
        creation = UI[
            UI.index("static void create_pve_page"):
            UI.index("static void show_nas_pools")
        ]
        self.assertIn('make_label(metrics, "存储", 12, PVE_METRIC_ROW_HEIGHT + 10', creation)
        self.assertIn('make_label(metrics, "内存", 180, PVE_METRIC_ROW_HEIGHT + 10', creation)
        self.assertIn("lv_obj_set_size(s_ui.pve_vm_list_button, 464, PVE_VM_HIT_HEIGHT)", creation)
        self.assertIn("(PVE_VM_HIT_HEIGHT - PVE_VM_VISUAL_HEIGHT) / 2", creation)
        self.assertNotIn("temperature", creation.lower())

    def test_pve_has_three_content_views_without_old_subnavigation(self):
        for marker in (
            "PVE_VIEW_NODE",
            "PVE_VIEW_VM_LIST",
            "PVE_VIEW_VM_DETAIL",
            "show_pve_node",
            "show_pve_vm_list",
            "show_vm_detail",
        ):
            self.assertIn(marker, UI)
        for removed in (
            "pve_overview_button",
            "vm_nav_buttons",
            "vm_nav_offset",
            "vm_detail_previous",
            "vm_detail_next",
            '"PVE 总览"',
            "PVE_SUBNAV_LEFT",
            "PVE_SUBNAV_RIGHT",
        ):
            self.assertNotIn(removed, UI)

    def test_pve_vm_list_has_four_aligned_rows_and_vertical_pager(self):
        for marker in (
            "#define VM_VISIBLE 4",
            "#define PVE_VM_LIST_RULE_COUNT 9",
            "vm_name_buttons",
            "vm_list_rules",
            "vm_list_previous",
            "vm_list_next",
            "vm_list_page",
            "vm_list_empty",
            "420, 0",
            "44, 276",
            "30 + i * 58",
            "166, 0, 1, 276",
            "280, 0, 1, 276",
            "328, 0, 1, 276",
            "420, 0, 1, 276",
            "0, 30, 420, 1",
            "lv_obj_set_size(s_ui.vm_name_buttons[i], 166, 58)",
        ):
            self.assertIn(marker, UI)
        for header in ('"虚拟机"', '"IP"', '"vcpu"', '"内存"'):
            self.assertIn(header, UI)
        pve_creation = UI[
            UI.index("static void create_pve_page"):
            UI.index("static void show_nas_pools")
        ]
        self.assertIn(
            's_ui.vm_header_cpu = make_label(s_ui.pve_vm_list, "vcpu", 288, 6, 40,',
            pve_creation,
        )
        self.assertNotIn(
            's_ui.vm_header_cpu = make_label(s_ui.pve_vm_list, "CPU"',
            pve_creation,
        )
        pve_update = UI[
            UI.index("static void update_pve(void)\n{"):
            UI.index("static void update_nas(void)\n{")
        ]
        self.assertIn("if (guest->cpu_cores > 0)", pve_update)
        self.assertIn(
            'lv_label_set_text_fmt(s_ui.vm_row_cpu[i], "%" PRIu32, guest->cpu_cores);',
            pve_update,
        )
        self.assertIn('lv_label_set_text(s_ui.vm_row_cpu[i], "--");', pve_update)
        self.assertNotIn("format_percent(cpu", pve_update)
        self.assertNotIn("vm_row_disk", pve_creation)

    def test_pve_vm_detail_uses_two_by_three_metric_grid(self):
        for marker in (
            "#define PVE_VM_DETAIL_RULE_COUNT 4",
            "vm_detail_rules",
            "0, 54, 464, 1",
            "231, 54, 1, 222",
            "0, 127, 464, 1",
            "0, 200, 464, 1",
        ):
            self.assertIn(marker, UI)
        for label in ('"IP"', '"CPU"', '"内存"', '"磁盘"', '"运行时间"',
                      '"Guest Agent"'):
            self.assertIn(label, UI)
        detail_update = UI[
            UI.index("static void update_vm_detail(void)"):
            UI.index("static void show_vm_detail")
        ]
        for combined_label in ('"IP  %s"', '"CPU  %s', '"内存  %s', '"磁盘  %s',
                               '"运行时间  %s"', '"Guest Agent  在线"'):
            self.assertNotIn(combined_label, detail_update)
        detail_creation = UI[
            UI.index('make_label(s_ui.vm_detail, "运行时间"'):
            UI.index('make_label(s_ui.vm_detail, "Guest Agent"')
        ]
        self.assertIn("s_ui.vm_detail_uptime", detail_creation)
        self.assertIn("&app_font_14, COLOR_TEXT", detail_creation)

    def test_pve_vm_events_route_name_detail_background_node_and_detail_list(self):
        for marker in (
            "pve_vm_list_button_event",
            "vm_name_event",
            "vm_list_return_event",
            "vm_detail_return_event",
            "vm_list_page_event",
            "lv_event_stop_bubbling(event)",
        ):
            self.assertIn(marker, UI)
        self.assertIn("show_vm_detail", UI)
        self.assertIn("show_pve_node", UI)
        self.assertIn("show_pve_vm_list", UI)

    def test_pve_snapshot_changes_clamp_list_and_detail_state(self):
        update = UI[
            UI.index("static void update_pve(void)\n{"):
            UI.index("static void update_nas(void)\n{")
        ]
        for marker in (
            "last_page_offset",
            "s_ui.vm_list_offset > last_page_offset",
            "s_ui.selected_vm_index >= (int)snapshot->pve_guest_count",
            "show_pve_vm_list()",
        ):
            self.assertIn(marker, update)

if __name__ == "__main__":
    unittest.main()
