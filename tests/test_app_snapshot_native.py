from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class AppSnapshotNativeTest(unittest.TestCase):
    def test_snapshot_lifecycle_and_dynamic_counts(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "app_snapshot_test"
            compile_result = subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "components/app_model/include"),
                    str(ROOT / "tests/native/app_snapshot_test.c"),
                    str(ROOT / "components/app_model/app_snapshot.c"),
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
            self.assertIn("app_snapshot tests passed", run_result.stdout)


if __name__ == "__main__":
    unittest.main()
