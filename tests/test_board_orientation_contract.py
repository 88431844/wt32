from pathlib import Path
import unittest


BOARD_SOURCE = (
    Path(__file__).resolve().parents[1]
    / "components"
    / "board_wt32"
    / "board_wt32.c"
)
BOARD_HEADER = (
    Path(__file__).resolve().parents[1]
    / "components"
    / "board_wt32"
    / "include"
    / "board_wt32.h"
)
MAIN_SOURCE = (Path(__file__).resolve().parents[1] / "main" / "app_main.c")


class BoardOrientationContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = BOARD_SOURCE.read_text(encoding="utf-8")

    def test_lcd_is_rotated_180_degrees_in_landscape(self) -> None:
        self.assertIn("esp_lcd_panel_swap_xy(s_panel, true)", self.source)
        self.assertIn(
            "static bool s_rotation_180 = true;",
            self.source,
        )
        self.assertIn(
            "esp_lcd_panel_mirror(s_panel, s_rotation_180, s_rotation_180)",
            self.source,
        )

    def test_touch_transform_matches_rotated_display(self) -> None:
        self.assertIn(
            "x = (TOUCH_NATIVE_HEIGHT - 1) - raw_y;",
            self.source,
        )
        self.assertIn("y = raw_x;", self.source)

        def transform(raw_x: int, raw_y: int) -> tuple[int, int]:
            return 479 - raw_y, raw_x

        self.assertEqual(transform(0, 0), (479, 0))
        self.assertEqual(transform(319, 0), (479, 319))
        self.assertEqual(transform(0, 479), (0, 0))
        self.assertEqual(transform(319, 479), (0, 319))

    def test_runtime_rotation_api_and_both_touch_mappings(self) -> None:
        header = BOARD_HEADER.read_text(encoding="utf-8")
        self.assertIn(
            "esp_err_t wt32_board_set_rotation_180(bool enabled);",
            header,
        )
        self.assertIn("bool wt32_board_get_rotation_180(void);", header)
        self.assertIn("if (s_rotation_180)", self.source)
        self.assertIn(
            "esp_lcd_panel_mirror(s_panel, enabled, enabled)",
            self.source,
        )
        self.assertIn("x = (TOUCH_NATIVE_HEIGHT - 1) - raw_y;", self.source)
        self.assertIn("x = raw_y;", self.source)

    def test_startup_applies_persisted_rotation_and_brightness(self) -> None:
        main = MAIN_SOURCE.read_text(encoding="utf-8")
        self.assertIn("DEVICE_KEY_ROTATE_180", main)
        self.assertIn("DEVICE_KEY_BRIGHTNESS", main)
        self.assertIn("if (brightness < 10 || brightness > 100)", main)
        self.assertIn("wt32_board_set_rotation_180(rotate_180 != 0)", main)
        self.assertIn("wt32_board_set_brightness(brightness)", main)


if __name__ == "__main__":
    unittest.main()
