from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
UI = ROOT / "components/dashboard_ui/dashboard_ui.c"
FONT_14 = ROOT / "components/dashboard_ui/fonts/app_font_14.c"


class DashboardUiContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = UI.read_text(encoding="utf-8")

    def test_only_nas_pve_and_settings_pages_are_created(self) -> None:
        self.assertIn("#define PAGE_COUNT 3", self.source)
        self.assertIn("create_pve_page", self.source)
        self.assertIn("create_nas_page", self.source)
        self.assertIn("create_settings_page", self.source)
        self.assertNotIn("create_weather_page", self.source)
        self.assertNotIn("create_market_page", self.source)
        self.assertNotIn("lv_tileview_create", self.source)

    def test_top_bar_orders_nas_status_pve_settings_without_power(self) -> None:
        self.assertIn('make_button(bar, "NAS", 4, 3, 76, 26', self.source)
        self.assertIn('make_label(bar, "--:--", 84, 7, 52', self.source)
        self.assertIn('make_label(bar, "WiFi未连接", 140, 7, 166', self.source)
        self.assertIn('make_button(bar, "PVE", 310, 3, 76, 26', self.source)
        self.assertIn('make_button(bar, "设置", 390, 3, 86, 26', self.source)
        self.assertIn('"WiFi未连接，请到设置里面设置"', self.source)
        self.assertNotIn('"PWR"', self.source)

    def test_settings_page_has_wifi_rotation_brightness_and_token_controls(self) -> None:
        self.assertIn("lv_dropdown_create", self.source)
        self.assertIn("lv_keyboard_create", self.source)
        self.assertIn("lv_slider_create", self.source)
        self.assertIn('"PVE Token 设置"', self.source)
        self.assertIn('"0°"', self.source)
        self.assertIn('"180°"', self.source)

    def test_pve_layout_has_inline_vm_detail_and_vertical_guest_navigation(self) -> None:
        self.assertIn("pve_overview", self.source)
        self.assertIn("vm_detail", self.source)
        self.assertIn("selected_vm_index", self.source)
        self.assertIn("vm_nav_buttons", self.source)
        self.assertIn("show_vm_detail", self.source)
        self.assertIn("show_pve_overview", self.source)
        self.assertIn("vm_detail_previous", self.source)
        self.assertIn("vm_detail_next", self.source)
        self.assertIn("vm_scroll_event", self.source)
        self.assertIn("make_button(list, \"^\"", self.source)
        self.assertIn("make_button(list, \"v\"", self.source)
        self.assertIn("PVE_SUBNAV_LEFT", self.source)
        self.assertIn("PVE_SUBNAV_RIGHT", self.source)
        self.assertIn("PVE_VM_SCROLL_UP", self.source)
        self.assertIn("PVE_VM_SCROLL_DOWN", self.source)

    def test_live_fields_use_theme_split_used_and_remaining_bars(self) -> None:
        self.assertIn("palette()->free", self.source)
        self.assertIn("palette()->used", self.source)
        self.assertIn("pve_guest_count", self.source)
        self.assertIn("nas_pool_count", self.source)

    def test_lvgl_printf_does_not_receive_floating_point_arguments(self) -> None:
        self.assertNotIn('lv_label_set_text_fmt(s_ui.pve_load_value, "%.1f', self.source)
        self.assertNotIn('lv_label_set_text_fmt(s_ui.vm_row_metrics[i], "C %.0f', self.source)
        self.assertIn("format_percent(cpu, sizeof(cpu), guest->cpu_percent)", self.source)

    def test_five_persistent_themes_default_to_graphite(self) -> None:
        self.assertIn("#define THEME_COUNT 5", self.source)
        self.assertIn("THEME_DEEP_OCEAN", self.source)
        self.assertIn("THEME_HIGH_CONTRAST", self.source)
        self.assertIn("THEME_MIST_GRAY", self.source)
        self.assertIn("THEME_GRAPHITE", self.source)
        self.assertIn("THEME_CHARCOAL_CORAL", self.source)
        self.assertIn("static uint8_t s_theme_id = THEME_GRAPHITE", self.source)
        self.assertIn("DEVICE_KEY_THEME", self.source)

    def test_settings_offer_supported_refresh_intervals_and_default_to_five_seconds(self) -> None:
        self.assertIn("#define REFRESH_OPTION_COUNT 4", self.source)
        for label in ("5 秒", "10 秒", "30 秒", "60 秒"):
            self.assertIn(f'"{label}"', self.source)
        self.assertNotIn('"120 秒"', self.source)
        self.assertIn("static uint8_t s_refresh_seconds = 5", self.source)
        self.assertIn("uint8_t saved_refresh = 5", self.source)
        self.assertIn("else s_refresh_seconds = 5", self.source)
        self.assertIn("DEVICE_KEY_REFRESH", self.source)
        self.assertIn("app_model_set_refresh_seconds", self.source)
        self.assertIn("app_model_set_active_monitor", self.source)

    def test_nas_layout_has_pool_navigation_overview_and_detail(self) -> None:
        for field in (
            "nas_overview_button", "nas_pool_buttons", "nas_pool_button_dots",
            "nas_overview", "nas_detail", "selected_nas_pool_index",
            "show_nas_overview", "show_nas_detail", "nas_disk_rows",
        ):
            self.assertIn(field, self.source)
        self.assertIn('make_button(page, "NAS 总览"', self.source)
        self.assertIn('"存储池%d"', self.source)
        self.assertIn('"NAS 物理盘（未按池映射）"', self.source)

    def test_nas_footer_is_two_rows_and_includes_ip_uptime_and_network(self) -> None:
        self.assertIn("#define NAS_METRICS_HEIGHT 36", self.source)
        self.assertIn("nas_upload_value", self.source)
        self.assertIn("nas_download_value", self.source)
        self.assertIn("nas_ip_value", self.source)
        self.assertIn("nas_uptime_value", self.source)
        self.assertIn("format_rate", self.source)
        self.assertIn("format_uptime", self.source)

    def test_nas_footer_uses_fixed_left_aligned_cells(self) -> None:
        for field in ("nas_network_prefix", "nas_upload_value", "nas_download_value"):
            self.assertIn(field, self.source)
        self.assertIn('make_label(footer, "网", 8, 19, 18', self.source)
        self.assertIn('make_label(footer, "↑--", 26, 19, 66', self.source)
        self.assertIn('make_label(footer, "↓--", 94, 19, 66', self.source)
        self.assertIn('make_label(footer, "CPU --", 8, 1, 160', self.source)
        self.assertIn('make_label(footer, "内存 --", 168, 1, 170', self.source)
        self.assertIn('make_label(footer, "温度 --", 338, 1, 118', self.source)
        self.assertIn('make_label(footer, "IP --", 168, 19, 170', self.source)
        self.assertIn('make_label(footer, "运行 --", 338, 19, 118', self.source)
        for field in (
            "nas_cpu_value", "nas_memory_value", "nas_temperature_value",
            "nas_network_prefix", "nas_upload_value", "nas_download_value",
            "nas_ip_value", "nas_uptime_value",
        ):
            self.assertIn(
                f"lv_obj_set_style_text_align(s_ui.{field}, LV_TEXT_ALIGN_LEFT, 0)",
                self.source,
            )
        self.assertNotIn(
            'lv_label_set_text_fmt(s_ui.nas_network_value, "网 ↑%s ↓%s"',
            self.source,
        )

    def test_nas_capacity_layout_uses_compact_values_and_status_dots(self) -> None:
        self.assertIn("format_capacity", self.source)
        self.assertIn('"%s/%s 剩%s"', self.source)
        self.assertIn("nas_row_status_dots", self.source)
        self.assertIn("nas_live && pool->healthy", self.source)
        self.assertNotIn('"池 %d  %s"', self.source)

    def test_nas_stale_and_offline_snapshots_hide_cached_metrics(self) -> None:
        self.assertIn(
            "const bool nas_live = snapshot->nas_online && !snapshot->nas_stale;",
            self.source,
        )
        self.assertIn('snapshot->nas_stale ? "离线 · 缓存" : "离线"', self.source)
        self.assertIn('lv_label_set_text(s_ui.nas_row_values[i], "--/-- 剩--")', self.source)
        self.assertIn("lv_bar_set_value(s_ui.nas_row_bars[i], 0, LV_ANIM_OFF)", self.source)
        self.assertIn('lv_label_set_text(s_ui.nas_detail_capacity, "--")', self.source)
        self.assertIn('snapshot->nas_stale ? "NAS 离线 · 缓存" : "NAS 离线"', self.source)
        for placeholder in ('"CPU --"', '"内存 --"', '"温度 --"'):
            self.assertIn(placeholder, self.source)

    def test_nas_footer_requires_each_live_metric_to_be_valid(self) -> None:
        self.assertIn("nas_live && snapshot->nas_cpu_valid", self.source)
        self.assertIn("nas_live && snapshot->nas_memory_valid", self.source)
        self.assertIn("nas_live && snapshot->nas_temperature_valid", self.source)
        self.assertIn('lv_label_set_text(s_ui.nas_cpu_value, "CPU --")', self.source)
        self.assertIn('lv_label_set_text(s_ui.nas_memory_value, "内存 --")', self.source)
        self.assertIn('lv_label_set_text(s_ui.nas_temperature_value, "温度 --")', self.source)

    def test_nas_online_without_pools_is_not_reported_offline(self) -> None:
        self.assertIn('"NAS 在线  未发现存储池"', self.source)
        self.assertIn("if (!snapshot->nas_configured)", self.source)
        self.assertIn("else if (nas_live)", self.source)
        self.assertIn("else if (snapshot->nas_stale)", self.source)

    def test_nas_disk_table_pages_across_the_snapshot_capacity(self) -> None:
        self.assertIn("#define NAS_DISK_VISIBLE 4", self.source)
        self.assertIn("APP_MAX_NAS_DISKS", self.source)
        self.assertIn("nas_disk_offset", self.source)
        self.assertIn("nas_disk_previous", self.source)
        self.assertIn("nas_disk_next", self.source)
        self.assertIn("nas_disk_page", self.source)
        self.assertIn("nas_disk_page_event", self.source)
        self.assertIn("s_ui.nas_disk_offset + i", self.source)
        self.assertIn("APP_MAX_NAS_DISKS - NAS_DISK_VISIBLE", self.source)
        self.assertIn(
            "((snapshot_disk_count - 1) / NAS_DISK_VISIBLE) * NAS_DISK_VISIBLE",
            self.source,
        )
        self.assertNotIn("#define NAS_DISK_VISIBLE 8", self.source)

    def test_vm_rows_include_ip_and_used_total_capacity(self) -> None:
        self.assertIn("guest->ipv4_address", self.source)
        self.assertIn("guest->memory_total", self.source)
        self.assertIn("guest->disk_total", self.source)
        for field in ("vm_header_ip", "vm_header_cpu", "vm_header_memory", "vm_header_disk"):
            self.assertIn(field, self.source)
        for field in ("vm_row_cpu", "vm_row_memory", "vm_row_disk"):
            self.assertIn(field, self.source)
        self.assertNotIn("vm_row_metrics", self.source)
        self.assertNotIn("%-12s", self.source)

    def test_pve_summary_does_not_claim_temperature(self) -> None:
        self.assertNotIn("pve_cpu_temperature", self.source)
        self.assertNotIn('make_label(summary, "温度"', self.source)

    def test_status_dots_are_objects_and_dynamic_text_is_sanitized(self) -> None:
        self.assertIn("make_status_dot", self.source)
        self.assertIn("sanitize_dynamic_text", self.source)
        self.assertNotIn('"●"', self.source)

    def test_font_contains_nas_detail_glyphs(self) -> None:
        font_source = FONT_14.read_text(encoding="utf-8")
        for glyph in "物理按映射硬型号量计天用分异缓存在发现（）":
            self.assertIn(glyph, font_source)


if __name__ == "__main__":
    unittest.main()
