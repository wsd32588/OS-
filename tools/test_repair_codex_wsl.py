import os
from pathlib import Path
import tempfile
import unittest

from repair_codex_wsl import HELPERS, repair_directory


class RepairHelpersTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.executable = self.root / "codex"
        self.executable.write_text("test backend\n")
        self.executable.chmod(0o700)
        self.directory = self.root / "codex-arg0test"

    def test_missing_directory_and_repeat_repair(self):
        self.assertTrue(repair_directory(self.directory, self.executable).startswith("restored:"))
        for name in HELPERS:
            self.assertEqual((self.directory / name).resolve(), self.executable)
        self.assertEqual(repair_directory(self.directory, self.executable), "healthy")

    def test_check_does_not_create_directory(self):
        self.assertTrue(repair_directory(self.directory, self.executable, True).startswith("missing:"))
        self.assertFalse(self.directory.exists())

    def test_conflicting_file_is_preserved_without_partial_repair(self):
        self.directory.mkdir()
        conflict = self.directory / "apply_patch"
        conflict.write_text("existing file")
        with self.assertRaises(RuntimeError):
            repair_directory(self.directory, self.executable)
        self.assertEqual(conflict.read_text(), "existing file")
        self.assertFalse(os.path.lexists(self.directory / "codex-linux-sandbox"))

    def test_broken_link_is_not_overwritten(self):
        self.directory.mkdir()
        conflict = self.directory / "apply_patch"
        conflict.symlink_to(self.root / "missing")
        with self.assertRaises(RuntimeError):
            repair_directory(self.directory, self.executable)
        self.assertEqual(os.readlink(conflict), str(self.root / "missing"))

    def test_symlink_directory_is_rejected(self):
        actual = self.root / "actual"
        actual.mkdir()
        self.directory.symlink_to(actual)
        with self.assertRaises(RuntimeError):
            repair_directory(self.directory, self.executable)
        self.assertEqual(list(actual.iterdir()), [])


if __name__ == "__main__":
    unittest.main()
