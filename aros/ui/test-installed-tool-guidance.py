#!/usr/bin/env python3
"""Keep user-facing service instructions aligned with the installed target."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


class Guidance(unittest.TestCase):
    def test_native_service_name(self):
        build = (ROOT / "aros/library/mmakefile.src").read_text()
        match = re.search(r"mmake=contrib-openzfs-service progname=(\w+)", build)
        self.assertIsNotNone(match)
        service = match.group(1)
        manager = (ROOT / "aros/ui/manager.c").read_text()
        self.assertIn(f"Start C:{service} and choose Connect.", manager)
        partition = (ROOT / "aros/ui/partition-view.h").read_text()
        self.assertIn(f"Check that {service} is running.", partition)


if __name__ == "__main__":
    unittest.main()
