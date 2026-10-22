#!/usr/bin/env python3
"""Evidence binding tests; these do not replace native package acceptance."""
import importlib.util
import pathlib
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("stage", pathlib.Path(__file__).with_name("stage-mmake.py"))
stage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stage)


class EvidenceTests(unittest.TestCase):
    def test_selected_bytes_not_old_manifest_path(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            old, selected = root / "old", root / "selected"
            old.write_bytes(b"tested")
            selected.write_bytes(b"tested")
            (root / "inputs.sha256").write_text(f"{stage.digest(old)}  /old/location/engine\n")
            stage.require_match(selected, root, "engine")
            selected.write_bytes(b"different")
            with self.assertRaises(ValueError):
                stage.require_match(selected, root, "engine")

    def test_missing_and_ambiguous(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            (root / "inputs.sha256").write_text("a  /old/engine\n")
            with self.assertRaises(ValueError):
                stage.recorded_hash(root, "absent")
            (root / "gui-inputs.sha256").write_text("b  /new/engine\n")
            with self.assertRaises(ValueError):
                stage.recorded_hash(root, "engine")

    def test_singular_device_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            (root / "inputs.sha256").write_text("")
            (root / "device-input.sha256").write_text("abc  /run/OpenZFSDevice\n")
            self.assertEqual(stage.recorded_hash(root, "OpenZFSDevice"), "abc")

    def test_members_bind_names_and_hashes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            before, after = root / "before", root / "after"
            before.write_text("a  /before/disk0\nb  /before/disk1\n")
            after.write_text("a  /after/disk0\nb  /after/disk1\n")
            self.assertEqual(stage.member_hashes(before), stage.member_hashes(after))
            after.write_text("b  /after/disk0\na  /after/disk1\n")
            self.assertNotEqual(stage.member_hashes(before), stage.member_hashes(after))
            after.write_text("a  /after/disk0\nb  /other/disk0\n")
            with self.assertRaises(ValueError):
                stage.member_hashes(after)


if __name__ == "__main__":
    unittest.main()
