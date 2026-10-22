# Port status

Experimental development snapshot; not production-qualified storage software.

## Current public-source qualification

The native MetaMake package now passes isolated two-CPU QEMU tests of the
shared engine, upstream `zfs`/`zpool` commands and standalone DOS handler.
This includes clone/rename, LZ4 configuration, rollback checked through mounted
data, full and incremental replication, and handler read-only enforcement.
The persistent run `public-service-run.qA8HX86o` additionally passed a fresh
guest boot, healthy and degraded mirror imports, exact file reads, unchanged
pool-member bytes, and independent host `zdb -bcc` with no leaks.
The same current native package also passed three-, four-, and five-member
RAIDZ1/2/3 suites, including fresh-boot reads with one/two/three members missing,
respectively. Host configuration inspection confirmed the actual parity.
The fresh native package additionally passes small dRAID1/2/3 configurations
with one distributed spare each, including cold degraded reads, excessive-loss
refusal and restored reads. Independent configuration checks confirm actual
dRAID parity/data/spare geometry. Subsequent public-package tests also qualify
typed distributed-spare activation and healing recovery for dRAID1/2/3.
Sequential reconstruction is exercised separately with the upstream
device_rebuild feature enabled: persisted rebuild completion, zero reported
errors, and the automatically started post-rebuild scrub are checked before
and after cold imports. The scrub must start no earlier than rebuild completion.
These are small file-backed layouts, not general hardware/topology qualification.

Mirror tests additionally cover offline/write/online resilver and replacement
with a new file-backed member. Cold imports read exact payload bytes with only
the repaired or replacement member available. See BUILDING.md for retained
run identifiers and the distinction between healing and sequential recovery.

Additional native tests qualify sparse DOS files beyond 4 GiB (including
replication and cold reads), excessive member-loss refusal followed by
restoration, and AES-256-GCM raw-key lifecycle with wrong-key denial.
Encrypted datasets survive healthy/degraded/restored cold imports. Full and
incremental raw encrypted streams transfer with unloaded keys; received data
remains inaccessible until key load, then matches the source through the DOS
handler. See BUILDING.md for individual runs and qualification limits.

These tests use newly created virtual storage and explicitly selected patched
runtime libraries. They supersede the initial startup-only qualification below,
but do not establish stock-nightly or physical-device safety. See BUILDING.md
for exact scope, inputs and evidence identifiers.

## Implemented integration

The source contains the upstream OpenZFS engine, a shared AROS library/service,
native DOS handler, management UI, upstream command-line tools, startup support,
and device adapters. The UI sends backend requests rather than simulating pool
operations. Security and MuFS integration are platform dependencies.

Historical development testing includes native file access, pool lifecycle,
file-backed multi-device pools, mirror checksum repair, unrecoverable checksum
errors, degraded RAIDZ2, and recovery/packaging checks. See the component notes
for the exact scope and restrictions of individual tests. These are prior test
results, not a fresh qualification of this public snapshot.

## Remaining boundaries

- A compatible patched AROS runtime is required; stock-nightly compatibility is
  not established for every component.
- Physical storage flush, disconnect, and abrupt power-loss behavior require
  hardware qualification. Virtual-disk tests do not establish these guarantees.
- Not every upstream feature or topology has end-to-end AROS validation.
- TRIM/discard is not implemented for AROS file or native-device backings;
  requests report unsupported rather than claiming successful discard.
- This does not provide an NFS/SMB client or server for browsing a remote ZFS
  server automatically.
- File-backed shared `mmap` is not provided by AROS; the port must use supported
  native I/O and explicit ownership rather than assume Linux memory semantics.
- Reproducible dependency bootstrap and wider independent testing remain work
  for contributors. No prebuilt binaries or test-disk images are included.
  Private source-build helpers now cover pthread and BoringSSL crypto as well
  as nvpair/XDR; matching AROS platform sources, SDK and overlays must still be
  selected explicitly. Crypto no longer requires Chromium's prebuilt archive.

Use `ENV:` for active settings and `ENVARC:` for persistent settings. Preserve
backups outside experimental pools, and never assume compatibility with the
older private AROS ZFS on-disk format.

## Public snapshot validation, 2026-10-21

The native Manager compiled and linked from initial public source `b3ecbadd`
using an explicitly configured x86-64 SDK and matching private pthread archive.
The resulting executable SHA-256 is
`0798042fe8f867d1721d613c7a61de3711089c909d1e26849395f1fbd47e0506`.

A separate isolated hosted test failed before the PC/SMP-linked Manager opened:
AROS reported a bus/address-error alert in `Exec_43_RemHead`, with a null-address
trap. The same binary also failed in a second historical hosted runtime.

Rebuilding the same public source with the hosted SDK, keeping the compiler,
pthread archive, and second runtime unchanged, produced a Manager that opened
and displayed the expected service-unavailable response. The older qualified
Manager also opened in that runtime. This isolates the observed failure to the
SDK/link configuration; it is not evidence of an application-source regression.
The precise low-level ABI mismatch has not been traced. The hosted executable
SHA-256 is `adb58f9246b22645e2197dfff0207c8b8ec56c41ab9383a2ac79745ed0ce67f7`.

The build helper now requires an explicit SDK. This hosted startup check does
not qualify the PC/SMP binary, service operations, or filesystem behavior.

The test used a private runtime copy, no storage service, and no physical disks.
Source syntax checks covered 99 shell scripts and 16 Python files; four metadata
unit tests passed. These checks are separate from native runtime acceptance.
