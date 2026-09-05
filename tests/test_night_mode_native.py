from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class NightModeNativeTest(unittest.TestCase):
    def test_schedule_and_brightness_limits(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "night_mode_test"
            compile_result = subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "components/dashboard_ui/include"),
                    str(ROOT / "tests/native/night_mode_test.c"),
                    str(ROOT / "components/dashboard_ui/night_mode.c"),
                    "-o",
                    str(executable),
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(compile_result.returncode, 0, compile_result.stderr)
            run_result = subprocess.run(
                [str(executable)], capture_output=True, text=True
            )
            self.assertEqual(run_result.returncode, 0, run_result.stderr)
            self.assertIn("night mode tests passed", run_result.stdout)


if __name__ == "__main__":
    unittest.main()
