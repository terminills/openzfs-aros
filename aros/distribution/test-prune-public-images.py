#!/usr/bin/env python3
"""Retention policy tests; no real build artifacts touched."""
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import subprocess

spec = importlib.util.spec_from_file_location("prune", Path(__file__).with_name("prune-public-test-images.py"))
prune = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prune)


class Retention(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        self.root = patch.object(prune, "ROOT", root)
        self.root.start()
        self.addCleanup(self.root.stop)
        self.run = root / "public-service-run.test"
        self.run.mkdir()
        for name in ("serial-results.log", "cold-results.log"):
            (self.run / name).write_text("RESULT public service storage PASS\n")
        (self.run / "zdb-check.log").write_text("No leaks\n")
        (self.run / "zdb-members-unchanged.log").write_text("disk0: OK\n")
        (self.run / "test.iso").write_bytes(b"disposable")
        (self.run / "screenshot.png").write_bytes(b"keep")

    def invoke(self, apply=False, opened=False):
        args = ["prune"] + (["--apply"] if apply else [])
        result = subprocess.CompletedProcess([], 0 if opened else 1,
                                             "123\n" if opened else "", "")
        with patch("sys.argv", args), patch.object(prune.subprocess, "run", return_value=result), contextlib.redirect_stdout(io.StringIO()):
            prune.main()

    def test_dry_run_preserves(self):
        self.invoke()
        self.assertTrue((self.run / "test.iso").exists())

    def test_archive_requires_cold_restore_and_hash_evidence(self):
        archive = self.run / "startup-archive.raw"
        archive.write_bytes(b"persistent settings")
        self.assertNotIn(archive, prune.candidates(self.run))
        (self.run / "archive-unchanged.log").write_text(f"{archive}: OK\n")
        self.assertNotIn(archive, prune.candidates(self.run))
        for name, mode in (("serial-results.log", 1), ("cold-results.log", 0)):
            with (self.run / name).open("a") as log:
                log.write(f"Public persistent settings PASS write={mode}\n")
        self.assertIn(archive, prune.candidates(self.run))
        self.invoke(True)
        self.assertFalse(archive.exists())

    def test_gui_requires_pass_and_no_backing(self):
        gui = self.run.with_name("native-gui-run.test")
        self.run.rename(gui)
        self.assertTrue(prune.eligible(gui))
        (gui / "backing.raw").write_bytes(b"do not prune")
        self.assertFalse(prune.eligible(gui))
        (gui / "backing.raw").unlink()
        (gui / "serial-results.log").write_text("RESULT public service storage FAIL\n")
        self.assertFalse(prune.eligible(gui))

    def test_failed_run_preserves(self):
        (self.run / "cold-results.log").write_text("RESULT public service storage FAIL\n")
        self.invoke(True)
        self.assertTrue((self.run / "test.iso").exists())

    def test_ram_requires_explicit_mode_and_no_backing(self):
        (self.run / "cold-results.log").unlink()
        self.assertFalse(prune.eligible(self.run))
        (self.run / "persistent.txt").write_text("0\n")
        self.assertTrue(prune.eligible(self.run))
        (self.run / "backing.raw").write_bytes(b"preserve")
        self.assertFalse(prune.eligible(self.run))

    def test_ram_marker_cannot_override_failed_cold_run(self):
        (self.run / "persistent.txt").write_text("0\n")
        (self.run / "cold-results.log").write_text("RESULT public service storage FAIL\n")
        self.assertFalse(prune.eligible(self.run))

    def test_open_image_preserves(self):
        self.invoke(True, True)
        self.assertTrue((self.run / "test.iso").exists())

    def test_symlink_excluded(self):
        outside = Path(self.temp.name) / "outside.iso"
        outside.write_bytes(b"keep")
        (self.run / "cold.iso").symlink_to(outside)
        self.assertNotIn(self.run / "cold.iso", prune.candidates(self.run))
        self.invoke(True)
        self.assertEqual(outside.read_bytes(), b"keep")

    def test_apply_preserves_evidence_and_records_hash(self):
        self.invoke(True)
        self.assertFalse((self.run / "test.iso").exists())
        self.assertTrue((self.run / "cold-results.log").exists())
        self.assertTrue((self.run / "screenshot.png").exists())
        record = json.loads((self.run / "pruned-images.jsonl").read_text())
        self.assertEqual(record["sha256"], hashlib.sha256(b"disposable").hexdigest())


if __name__ == "__main__":
    unittest.main()
