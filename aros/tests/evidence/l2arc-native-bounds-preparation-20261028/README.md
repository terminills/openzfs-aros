# Native entry-bound regression preparation

`AROS_TEST_ZDB_L2ARC_BOUNDS=1` adds malformed little-endian count 1023 and
big-endian UINT64_MAX fixtures, followed by a healthy header read. Both
guarded host debugger and selected native engine must contain the entry-count
rejection diagnostic before the runner attempts these deliberately invalid
inputs. This prevents accidental testing against the old unguarded binary.

Native results must match the host output exactly, return 1 for both invalid
counts, return 0 on the subsequent healthy invocation, and emit a distinct
bounds/recovery completion marker. The verifier rejects extra or out-of-order
passes and unknown report indices as well as missing/mismatched results.

Full preparation run `zdb-label-run.fxlovUmZ` passed against:

- native debugger/library: `mmake-debugger.5XPhlb4E` (entry-bound fix);
- private host debugger: `host-zdb-overlay.a4R8NlmV`;
- populated logs and bounds modes enabled together, plus ordinary label and
  native-device test client compilation.

The runner's new BUILD_ONLY mode validates host fixtures and compiles the
actual clients, then stops before ISO creation or QEMU. This run used that
mode: no new ISO, VM or native result is claimed. All original input hashes
were checked unchanged. Preparation logs and exact inputs accompany this note.
The QEMU slot remains coordinated behind the Bluetooth/MAME handoff.
