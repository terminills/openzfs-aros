# Guarded native-device label inspection

`zdb_label_io.h` separates POSIX file descriptors from native Exec handles.
The upstream label and L2ARC readers use the same read interface. Native
paths are parsed canonically, require configured security.library and native
root identity, and use the existing read-only device/partition implementations.
Authorization is rechecked for each native read, with explicit bounds and
overflow checks. Handles and security references close on failure/success.
No native write/flush operation is available. The ordinary file path retains
POSIX open/stat/pread behavior. The header is included in Automake sources as
well as compiler-discovered MetaMake dependencies.

This is not permission delegation, a coherent snapshot of a concurrently
written disk, external-writer exclusion, or protection against arbitrary code
in AROS's shared address space. Partition resolution retains the existing
catalog/open semantics, not atomic partition-table identity pinning.

## Evidence and failures

- `zdb-probe.zigF5oc8`: all three real debugger sources cross-compile.
- `mmake-debugger.f24nz6nk`: initial generated MetaMake link fails because
  the newly reachable generic invalidation path references `zfs_dev_flush`.
  Corrected by explicitly retaining native no-flush behavior and rejecting
  impossible POSIX-block invalidation on AROS, whose stat adapter accepts
  regular files only; no fake flush implementation was added.
- `mmake-debugger.lg9uITeY`: real generated MetaMake library/service/zdb build
  passes with the corrected reader. No engine copy is embedded in the launcher.
- `zdb-label-run.WHBjmB29`: root label inspection works, but a test that logs
  its parent in as UID 77 then starts a command still obtains labels. This
  does NOT verify the spawned debugger's identity and is retained as a failed
  authorization harness, not a passing denial case.
- `zdb-label-run.CHuyl2tm`: corrected helper authenticates and verifies UID
  77/GID 88 in the actual task calling OpenZFSRunDebugger. The real adapter
  returns `Permission denied`, exit 1. Root commands before and afterward
  return 0 with reports exactly matching host zdb. The helper restores its
  previous identity and closes its library references before exiting.

The latter guest has eight CPUs and one private qcow2 overlay backed by the
retained disposable 64-MiB file-pool member. The backing file is never opened
for guest writes. QEMU's final overlay/base comparison reports identical
contents, and all original input hashes remain unchanged. No networking,
host-filesystem share, DUT, or user's hosted session is involved.

The same run also passes shared-library lifetime tests, normal and damaged
file-label tests, and full healthy file-pool block/MOS checks (three datasets,
21 feature verdicts, 72 object rows). The host fixture/verifier suites pass
nine tests; those are parser/mutation checks, not substitutes for runtime proof.

## Additional isolated guests

`zdb-label-run.ltXVaTkp` creates a new 256-MiB MBR fixture with two 64-MiB
partitions in native positions 0 and 2, beginning at sectors 4097 and 139265.
Each contains a copy of the qualified member. Native
`/AROS/ata.device/0/partition/0` and `/partition/2` match the host's whole-member
label report exactly, demonstrating correct extent-relative label offsets at
both ends. The actual UID 77 caller is denied for `/partition/0`. The complete
partitioned disk is unchanged, not just the selected extent.

`zdb-label-run.pQpQziIV` omits the disposable Security configuration. Its
actual debugger caller checks `secIsConfigured()` is false; the existing
whole-device path returns EACCES/status 1. That overlay also remains unchanged.
This proves unconfigured-library denial, not a missing-library test.
Both guests also pass file-label/lifetime/damaged-label/full-pool checks.

`test-zdb-device-verifier.py` exercises whole-device, partition and
unconfigured-security transcripts plus missing identity/completion, duplicate
status, fault-marker and incorrect-denial rejection. It tests the verifier,
not native security. The unconfigured helper produced one unused-variable
warning; its unused login declaration is now guarded out (no runtime behavior
change), with a follow-up warning-clean cross-compile retained.

Still unqualified: GPT-specific debugger paths, missing security.library
denial, L2ARC fixtures, native whole-pool binding, and physical durability.
The parent/child identity behavior needs separate platform investigation;
do not infer child identity from successful parent login. Full-port and public
SDK/release qualification remain incomplete.
