#!/usr/bin/env python3
"""Exercise the real runner's early probe checks without building or booting."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
RUNNER = ROOT / "aros/engine/test-qemu-public-service.sh"
INIT = b"Public upstream libzfs init PASS"
CPU = b"Public native CPU topology PASS"


class ClientPreflight(unittest.TestCase):
    def run_probe(self, payload, init="0", cpu="0"):
        with tempfile.TemporaryDirectory(prefix="oz-client-preflight-") as tmp:
            client = Path(tmp) / "client"
            client.write_bytes(payload)
            env = {k: v for k, v in os.environ.items()
                   if not k.startswith("AROS_")}
            env.update(AROS_TEST_SOURCE_ROOT=str(ROOT),
                       AROS_TEST_RUNNER_SNAPSHOT=str(RUNNER),
                       AROS_TEST_ISO="unused.iso", AROS_TEST_BASE_PLATFORM="1",
                       AROS_TEST_POSIX_LIBRARY="unused.library",
                       AROS_TEST_SERVICE_BINARY="unused.service",
                       AROS_TEST_CLIENT_BINARY=str(client),
                       AROS_TEST_REQUIRE_INIT=init, AROS_TEST_REQUIRE_CPU=cpu,
                       # Stop immediately after probe validation. No artifacts
                       # or QEMU may be created by this test.
                       AROS_TEST_COMPANION_TOOLS="2")
            return subprocess.run(["bash", str(RUNNER)], env=env,
                                  capture_output=True, text=True, timeout=10)

    def test_missing_init(self):
        result = self.run_probe(CPU, init="1")
        self.assertEqual(result.returncode, 2)
        self.assertIn("libzfs-init probe", result.stderr)

    def test_missing_cpu(self):
        result = self.run_probe(INIT, cpu="1")
        self.assertEqual(result.returncode, 2)
        self.assertIn("CPU-topology probe", result.stderr)

    def test_optional_probes_disabled(self):
        result = self.run_probe(b"no optional probes")
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stderr, "")

    def test_both_present_large_binary(self):
        # A full consumer must drain strings output under pipefail; grep -q
        # could otherwise cause an intermittent strings SIGPIPE rejection.
        result = self.run_probe(INIT + b"\0" + CPU + b"\0" +
                                b"padding-string\0" * 100000, init="1", cpu="1")
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stderr, "")


if __name__ == "__main__":
    unittest.main()
