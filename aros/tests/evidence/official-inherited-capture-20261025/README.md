# Official runtime inherited-environment subprocess capture

The published 20261023 nightly SDK and unmodified runtime pass this bounded
test of `oz_cli_capture_env_exact(..., NULL, ...)`. NULL requests ordinary
environment inheritance. This does **not** qualify explicit environments,
the private process-policy tags, shared engine ABI, storage, or hardware.

Nightly ISO SHA-256:
`38b5689f4d5c23b0765a3fbc266c67928d841d81c754675a107dd8bc0b50d6e3`.
SDK is the pristine `iso-Developer` extraction, not a custom runtime SDK.
Compiler, source hashes, commands, startup scripts, and serial evidence are
retained alongside this report. No production capture behavior was changed.

## Results

- One caller, 16 cycles, seven cases: **112 PASS**, all temporary files removed.
- Eight pthread workers, 16 cycles each, seven cases: **896 PASS**, all workers
  joined and all temporary files removed.
- Both final-source tests used isolated eight-vCPU, 2 GiB, diskless KVM QEMU
  guests with no network. The only image is a read-only boot CD; no DUT or
  user-hosted session was accessed. Guests were stopped after final results.

Cases cover empty output, blank lines and an unterminated final line, exact
131072-byte output plus a tail, embedded-NUL rejection, bounded-output overflow,
missing executable, and exact per-worker/per-cycle token delivery. Nonempty
children deliberately return status 23, which must be preserved. Error cases
must return no partial line array. Deleting the private temporary directory
after joining the workers verifies that the capture files were cleaned up.

`verify-inherited-capture.py` checks every unique case and its errno/status/line
count, final cleanup success, shell return status, and completion order. Its
host tests reject incomplete, duplicate, wrong-result and failed logs. Native
test code additionally checks the actual bytes, including unique tokens.

The pre-token sequential and concurrent exploratory runs also passed (96 and
768 cases), but the final-source token runs above are the qualification evidence.
The inspected buffered-seek path did not fail these cases; no speculative
reopen repair was applied. This finite run is not proof of general SMP race
freedom, atomic close-on-exec, or hostile-environment isolation.
