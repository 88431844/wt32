from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class NasDiskSortNativeTest(unittest.TestCase):
    def test_dynamic_disk_collection_sorting(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "nas_disk_sort_test"
            compile_result = subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "components/app_model/include"),
                    "-I",
                    str(ROOT / "components/dashboard_ui"),
                    str(ROOT / "tests/native/nas_disk_sort_test.c"),
                    str(ROOT / "components/dashboard_ui/nas_disk_sort.c"),
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
            self.assertIn("nas disk sort tests passed", run_result.stdout)


if __name__ == "__main__":
    unittest.main()
