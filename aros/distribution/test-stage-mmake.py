#!/usr/bin/env python3
"""Evidence binding tests; these do not replace native package acceptance."""
import importlib.util
import pathlib
import json
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("stage", pathlib.Path(__file__).with_name("stage-mmake.py"))
stage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stage)


class EvidenceTests(unittest.TestCase):
    def test_fat_snapshot_binds_sources_license_and_binary(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            (root / 'Licenses').mkdir()
            files = {'fat.handler': 'binary', 'Licenses/LICENSE': 'license',
                     'build-options.txt': 'full_fat=1\n', 'rebuild-fat-snapshot.py': '# helper',
                     'source-provenance.json': '{}', 'fat.c': 'source'}
            for name, text in files.items():
                (root / name).write_text(text)
            recipe = {'format': 1, 'expected_binary': stage.digest(root / 'fat.handler')}
            (root / 'rebuild.json').write_text(json.dumps(recipe))
            names = [*files, 'rebuild.json']
            def manifest():
                (root / 'retained.sha256').write_text(''.join(
                    stage.digest(root / n) + '  ' + n + '\n' for n in names))
            manifest()
            stage.require_fat_snapshot(root, root / 'fat.handler')
            (root / 'fat.c').write_text('changed')
            with self.assertRaises(ValueError):
                stage.require_fat_snapshot(root, root / 'fat.handler')
            manifest()
            (root / 'other.handler').write_text('different')
            with self.assertRaises(ValueError):
                stage.require_fat_snapshot(root, root / 'other.handler')
            names.remove('Licenses/LICENSE')
            manifest()
            with self.assertRaises(ValueError):
                stage.require_fat_snapshot(root, root / 'fat.handler')

    def test_changed_pool_requires_each_phase_and_all_member_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            writer = (
                "Public destroyed changes CLI PASS attach=1 replace=1 add=1 active-force-refused=3 exported-force-refused=3 exact-read=5 cleanup=1\n"
                "Public destroyed changes Manager PASS attach=1 replace=1 add=1 active-refused=3 exported-refused=3 exact-read=5 retained=1\n")
            cold = "".join(f"Public changed pool cold PASS phase={i} exact-read=1 readonly-write-denied=1 exported=1\n" for i in range(3))
            hashes = "".join(f"hash{i}  /test/disk{i}\n" for i in range(5))
            files = {"serial-results.log": writer, "cold-results.log": cold,
                     "reuse-before.sha256": hashes, "reuse-after.sha256": hashes,
                     "reuse-zdb-check.log": "No leaks\n",
                     "reuse-zdb-unchanged.log": "disk0: OK\n"}
            for name, value in files.items():
                (root / name).write_text(value)
            stage.require_changed_pool(root)
            for name, bad in (
                ("serial-results.log", writer.replace("retained=1", "cleanup=1")),
                ("cold-results.log", cold.replace("phase=2", "phase=1")),
                ("reuse-after.sha256", hashes.replace("hash4", "changed")),
                ("reuse-before.sha256", hashes.replace("hash4  /test/disk4\n", "")),
                ("reuse-zdb-check.log", "incomplete\n"),
                ("reuse-zdb-unchanged.log", "disk0: OK\ndisk1: FAILED\n"),
            ):
                (root / name).write_text(bad)
                with self.assertRaises(ValueError):
                    stage.require_changed_pool(root)
                (root / name).write_text(files[name])

    def test_encrypted_root_requires_terminal_success_without_assertion(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            result = root / "serial-results.log"
            valid = (
                "RESULT public service storage PASS\n"
                "Public native encrypted root PASS properties=6 locked-import=2 mount-denied=2 key-reload=2 exact-data=1 cleanup=1\n"
                "Public native discovery data PASS handler-write=1 readonly-import-read=1 readonly-write-denied=1 writable-import-read=1 cleanup=1\n"
                "Public file label denial PASS corrupt=4 conflicting-member=4 checksum=4 manager-denied=12 forced-cli-denied=12 whole-members-unchanged=12 restored=12\n")
            result.write_text(valid)
            stage.require_encrypted_root(root)
            for invalid in (valid.replace("RESULT public service storage PASS\n", ""),
                            valid.replace("key-reload=2", "key-reload=1"),
                            valid.replace("checksum=4", "checksum=0"),
                            valid + "ASSERT at arc.c:1750:failure()\n",
                            valid + "RESULT public service storage FAIL\n"):
                result.write_text(invalid)
                with self.assertRaises(ValueError):
                    stage.require_encrypted_root(root)

    def test_optional_release_requires_completion_and_integrity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            stage.require_optional_release(root)
            (root / "cold-release.txt").write_text("1\n")
            with self.assertRaises(ValueError):
                stage.require_optional_release(root)
            (root / "release-results.log").write_text(
                "RESULT public service storage PASS\n"
                "Public cold writable release PASS pending-before=1 recursive-release=1 parent-absent=1 child-absent=1 reimport-absent=1 exact-data=1 exported=1\n"
                "Public backing clean detach PASS\n")
            (root / "release-zdb-check.log").write_text("No leaks\n")
            (root / "release-zdb-unchanged.log").write_text("disk0: OK\n")
            stage.require_optional_release(root)
            (root / "archive-before.sha256").write_text("hash archive\n")
            with self.assertRaises(ValueError):
                stage.require_optional_release(root)
            (root / "archive-after-release.log").write_text("archive: OK\n")
            stage.require_optional_release(root)
            (root / "release-zdb-unchanged.log").write_text("disk0: OK\ndisk1: FAILED\n")
            with self.assertRaises(ValueError):
                stage.require_optional_release(root)

    def test_companions_require_all_cold_cases(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            (root / "serial-results.log").write_text(
                "Public companion tools PASS runtime-prerequisites=1\n"
                "Public exec helper PASS real-zpool=1 captured-output=1 missing-executable=1\n"
                "Public companion configuration PASS hostid-save=1\n"
                "Public companion mount PASS guid-bound=1\n")
            marker = "Public persistent enabled restore PASS trusted-envarc=1 imported=1 mounted=1 readonly=1\n"
            (root / "cold-results.log").write_text(marker * 2)
            (root / "archive-unchanged.log").write_text("archive: OK\n")
            with self.assertRaises(ValueError):
                stage.require_companions(root)
            (root / "cold-results.log").write_text(marker * 3)
            stage.require_companions(root)
            (root / "archive-unchanged.log").write_text("archive: FAILED\n")
            with self.assertRaises(ValueError):
                stage.require_companions(root)

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
