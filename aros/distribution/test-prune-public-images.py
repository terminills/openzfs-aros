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

    def test_external_root_rejects_same_filesystem(self):
        with patch("sys.argv", ["prune", "--run", self.run.name,
                                 "--artifact-root", str(self.run.parent)]), contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                prune.main()
        self.assertTrue((self.run / "test.iso").exists())

    def test_native_cold_requires_reader_and_unchanged_disk(self):
        (self.run / "Startup-Native-Cold").write_text("NATIVE-COLD\n")
        image = self.run / "native-cold.iso"
        image.write_bytes(b"cold image")
        self.invoke(True)
        self.assertTrue(image.exists())
        marker = "Public native cold reader PASS locked=1 mount-denied=1 key-loaded=1 exact-read=1 readonly-write-denied=1 exported=1\n"
        result = self.run / "native-cold-results.log"
        result.write_text("RESULT public service storage PASS\n" + marker)
        self.assertFalse(prune.eligible(self.run))
        hashes = self.run / "native-cold-unchanged.log"
        hashes.write_text("partition.raw: FAILED\n")
        self.assertFalse(prune.eligible(self.run))
        hashes.write_text("partition.raw: OK\n")
        self.assertTrue(prune.eligible(self.run))
        result.write_text("RESULT public service storage PASS\n")
        self.assertFalse(prune.eligible(self.run))
        result.write_text("RESULT public service storage PASS\n" + marker)
        self.invoke(True)
        self.assertFalse(image.exists())
        self.assertTrue(result.exists())

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

    def test_preferences_require_native_marker(self):
        prefs = self.run.with_name("preferences-run.test")
        self.run.rename(prefs)
        self.assertFalse(prune.eligible(prefs))
        (prefs / "serial-results.log").write_text("RESULT native IFF preferences PASS\n")
        self.assertTrue(prune.eligible(prefs))
        (prefs / "backing.raw").write_bytes(b"keep")
        self.assertFalse(prune.eligible(prefs))

    def test_failed_run_preserves(self):
        (self.run / "cold-results.log").write_text("RESULT public service storage FAIL\n")
        self.invoke(True)
        self.assertTrue((self.run / "test.iso").exists())

    def test_release_phase_requires_its_own_evidence(self):
        (self.run / "cold-release.txt").write_text("1\n")
        release = self.run / "release.iso"
        release.write_bytes(b"release image")
        self.invoke(True)
        self.assertTrue(release.exists())
        self.assertTrue((self.run / "test.iso").exists())
        (self.run / "release-results.log").write_text("RESULT public service storage PASS\n")
        (self.run / "release-zdb-check.log").write_text("No leaks\n")
        (self.run / "release-zdb-unchanged.log").write_text("disk0: FAILED\n")
        self.assertFalse(prune.eligible(self.run))
        (self.run / "release-zdb-unchanged.log").write_text("disk0: OK\n")
        self.assertTrue(prune.eligible(self.run))
        self.invoke(True)
        self.assertFalse(release.exists())
        self.assertTrue((self.run / "release-results.log").exists())

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
