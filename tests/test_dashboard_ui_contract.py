from pathlib import Path
import re
import unittest


UI_SOURCE = (
    Path(__file__).resolve().parents[1]
    / "components"
    / "dashboard_ui"
    / "dashboard_ui.c"
)


class DashboardUiContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = UI_SOURCE.read_text(encoding="utf-8")

    def test_time_page_is_removed_but_context_clocks_remain(self) -> None:
        self.assertNotIn("create_time_page", self.source)
        self.assertNotIn("draw_digital_clock", self.source)
        self.assertNotIn("time_canvas", self.source)
        self.assertIn(
            's_ui.status_time = make_label(s_ui.status_bar, "22:18"',
            self.source,
        )
        self.assertIn(
            's_ui.weather_time = make_label(page, "22:18"',
            self.source,
        )
        self.assertIn(
            'make_label(page, "22:18", 15, 33, 160',
            self.source,
        )
        self.assertIn(
            'lv_label_set_text_fmt(s_ui.status_time, "%02u:%02u"',
            self.source,
        )
        self.assertIn(
            'lv_label_set_text_fmt(s_ui.weather_time, "%02u:%02u"',
            self.source,
        )

    def test_carousel_starts_with_information_page(self) -> None:
        self.assertIn("#define PAGE_COUNT 10", self.source)
        titles_match = re.search(
            r"static const char \*page_titles\[PAGE_COUNT\] = \{(.*?)\};",
            self.source,
            re.DOTALL,
        )
        self.assertIsNotNone(titles_match)
        titles = re.findall(r'"([^"]+)"', titles_match.group(1))
        self.assertEqual(
            titles,
            [
                "资讯",
                "日历",
                "天气",
                "PVE",
                "群晖 NAS",
                "Antigravity",
                "智能家居",
                "相册",
                "告警",
                "设置",
            ],
        )
        create_function = re.search(
            r"esp_err_t dashboard_ui_create\(.*?\n\}(?=\n\nvoid dashboard_ui_update)",
            self.source,
            re.DOTALL,
        )
        self.assertIsNotNone(create_function)
        page_builders = re.findall(
            r"create_(\w+)_page\(s_ui\.pages\[(\d+)\]\);",
            create_function.group(0),
        )
        self.assertEqual(
            page_builders,
            [
                ("market", "0"),
                ("calendar", "1"),
                ("weather", "2"),
                ("pve", "3"),
                ("nas", "4"),
                ("quota", "5"),
                ("home", "6"),
                ("gallery", "7"),
                ("alert", "8"),
                ("settings", "9"),
            ],
        )
        self.assertIn(
            "lv_obj_set_tile_id(s_ui.tileview, 0, 0, LV_ANIM_OFF);",
            create_function.group(0),
        )

    def test_rotation_setting_is_persistent_and_reverts_on_failure(self) -> None:
        self.assertIn('load_setting("rotate180", 1)', self.source)
        self.assertIn('save_setting("rotate180"', self.source)
        self.assertIn("画面 180°", self.source)
        self.assertIn("lv_obj_invalidate(s_ui.root)", self.source)
        self.assertIn("wt32_board_set_rotation_180", self.source)


if __name__ == "__main__":
    unittest.main()
