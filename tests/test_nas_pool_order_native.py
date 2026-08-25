from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class NasPoolOrderNativeTest(unittest.TestCase):
    def test_synology_volume_order_matches_dsm_pool_numbers(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "nas_pool_order_test"
            compile_result = subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "components/app_model/include"),
                    str(ROOT / "tests/native/nas_pool_order_test.c"),
                    str(ROOT / "components/app_model/nas_pool_order.c"),
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
            self.assertIn("NAS pool order tests passed", run_result.stdout)


if __name__ == "__main__":
    unittest.main()
