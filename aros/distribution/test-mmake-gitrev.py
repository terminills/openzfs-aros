#!/usr/bin/env python3
"""Exercise real MetaMake fragment revision rules with isolated Git fixtures."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class RevisionTests(unittest.TestCase):
    def check_fragment(self, kind):
        with tempfile.TemporaryDirectory(prefix="openzfs-gitrev-") as temporary:
            root = Path(temporary)
            source = root / "src" / "port"
            source.mkdir(parents=True)
            for relative in (f"aros/{kind}/build-system.mk", "scripts/make_gitrev.sh", "META"):
                target = source / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(ROOT / relative, target)
            # The engine fragment reads these inventories at parse time.
            for library in ("libzpool", "libicp", "libavl", "libunicode", "libzstd"):
                target = source / "lib" / library / "Makefile.am"
                target.parent.mkdir(parents=True)
                target.write_text("")
            marker = source / "revision-fixture"
            marker.write_text("initial\n")
            env = dict(os.environ, GIT_CONFIG_NOSYSTEM="1",
                       GIT_AUTHOR_NAME="Fixture", GIT_AUTHOR_EMAIL="fixture@example.invalid",
                       GIT_COMMITTER_NAME="Fixture", GIT_COMMITTER_EMAIL="fixture@example.invalid")
            def git(*args):
                return subprocess.check_output(["git", *args], cwd=source, env=env, text=True).strip()
            git("init", "-q")
            git("add", ".")
            git("-c", "commit.gpgsign=false", "commit", "-qm", "initial")
            current = "port" if kind == "cli" else "port/aros/engine"
            output = root / "out"
            header = output / current / ("cli/zfs_gitrev.h" if kind == "cli" else "full/include/zfs_gitrev.h")
            consumer = root / "header-consumer"
            makefile = root / "Makefile"
            makefile.write_text(f"SRCDIR := {root / 'src'}\nCURDIR := {current}\n"
                                f"GENDIR := {output}\nMKDIR := mkdir -p\n"
                                f"include {source}/aros/{kind}/build-system.mk\n"
                                f"{consumer}: {header}\n\tcp $< $@\n")
            def build():
                subprocess.run(["make", "--no-print-directory", "-f", str(makefile), str(consumer)],
                               cwd=root, env=env, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                self.assertIn('"' + git("describe", "--always", "--long", "--dirty") + '"', header.read_text())
                self.assertEqual(header.read_bytes(), consumer.read_bytes())
            build()
            stamp = header.stat().st_mtime_ns
            consumer_stamp = consumer.stat().st_mtime_ns
            build()
            self.assertEqual(stamp, header.stat().st_mtime_ns, "unchanged revision must not rewrite the header")
            self.assertEqual(consumer_stamp, consumer.stat().st_mtime_ns, "unchanged header must not rebuild consumers")
            marker.write_text("changed\n")
            build()
            self.assertIn("-dirty", header.read_text())
            git("add", "revision-fixture")
            git("-c", "commit.gpgsign=false", "commit", "-qm", "changed")
            build()
            self.assertNotIn("-dirty", header.read_text())
            stamp = header.stat().st_mtime_ns
            consumer_stamp = consumer.stat().st_mtime_ns
            build()
            self.assertEqual(stamp, header.stat().st_mtime_ns)
            self.assertEqual(consumer_stamp, consumer.stat().st_mtime_ns)

    def test_cli_incremental_revision(self):
        self.check_fragment("cli")

    def test_engine_incremental_revision(self):
        self.check_fragment("engine")


if __name__ == "__main__":
    unittest.main()
