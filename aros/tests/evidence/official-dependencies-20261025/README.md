# Source-built dependencies on the published AROS SDK/runtime

Both source-built dependency tests passed in an isolated eight-vCPU, 2 GiB
QEMU/KVM guest using the unmodified 20261023 nightly runtime. No writable
virtual disks, network, DUT or user-hosted instance were used. Test output
was written only to guest RAM. The guest was stopped after completion.

Base ISO SHA-256:
`38b5689f4d5c23b0765a3fbc266c67928d841d81c754675a107dd8bc0b50d6e3`.
The SDK is its pristine `iso-Developer` extraction. Both dependency builds
use that SDK's pthread headers/archive, not the private integration pthread.

## Built and executed

- Upstream nvpair and pinned libtirpc 1.3.7 memory-XDR sources: nested nvlists,
  integer/string/nvlist arrays, boolean values, 64-bit boundary values, exact
  round-trip payloads, and rejection of every truncated prefix of the packed
  fixture. The native executable and shell both returned success.
- The retained AROS-compatible BoringSSL source was rebuilt from source with
  eight build jobs and assembly disabled. No earlier crypto archive was reused.
  Native PBKDF2 tests passed three RFC 6070 known-answer vectors, the 32-byte
  OpenZFS passphrase derivation fixture at 350000 iterations, invalid iteration
  bounds, null arguments, and zeroed failure output. Shell status was zero.

This qualifies those selected dependency paths, not every crypto algorithm,
the complete engine, persistent encrypted pools or general SMP behavior.
Official SDK/environment and atomic pipe2 limitations remain open.

## Build blocker repaired

The initial crypto attempt lacked host Go on PATH and stopped before building.
After selecting the retained Go toolchain, the storage guard rejected the
external artifact root because the checkout itself was on that filesystem.
The guard now compares the destination against the **system filesystem**,
allowing an external checkout and artifacts on the same external disk. It
still rejects a missing/unmounted destination resolving onto the system disk,
failed/malformed filesystem identity, or less than 100 GiB free on the system,
default output, or destination filesystem. Unit checks exercise these cases
independently; the successful real crypto build exercises the external-checkout
case. No mounted filesystems or shared SDKs were modified.

## Provenance

Full external build evidence and source/hash manifests:
`/mnt/short-term/openzfs-official-deps.fVjvrmGW`.
Retained build scripts specify exact compiler, SDK, Go, XDR and crypto paths.
The builders verify consumed source/header hashes after compilation. Compact
input/output manifests, serial log, build logs, and storage test results are
included here; full source-manifest digests bind the larger external manifests.
Only the generated boot ISO is pruned after terminal-state and open-file checks;
its checksum is retained. Dependency archives and test executables remain
available externally for the next full-engine build.
