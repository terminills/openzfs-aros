# Authenticated persistent backing-file creation

Native operation 5 now creates upstream pools on preallocated FAT-backed files,
not only RAM files. Shared file identity locks protect blank/type/size/alias
preflight. Every parent is pinned before shared file locks are relinquished,
then write access is checked on every member before calling upstream spa_create.
This avoids native FAT's exclusive-writable-open conflict with shared locks.
As with writable import, the handoff is not atomic against external writers or
renames: files must be exclusively provisioned for this operation. Supplied
files are never created, grown or truncated by the adapter.

## Reproducible private QEMU evidence

`AROS_TEST_COLD_API_CREATE=1` selects native authenticated API creation in the
first boot of `test-qemu-file-backing.sh`. The second boot uses a new service
with no saved configuration and imports from labels through the same API.
Both boots mount through the DOS handler, read the exact payload and enumerate
the file; exported member bytes stay unchanged during the read-only boot.
Upstream host zdb checks checksums and leaks independently.

The native creation preflight test writes a nonzero byte one MiB into member 1,
requires EEXIST, verifies all 64 MiB of member 0 remain blank, and confirms the
sentinel survives before restoring it. After successful creation/export a
second create is refused, preserving existing labels. Status checks include
layout type, parity, member count and health, not merely a successful read.

| Layout | Fixed-runner evidence suffix |
| --- | --- |
| Stripe, 2 files | 1wZFd2M9 |
| Mirror, 2 files | xkJ95X1H |
| RAIDZ1, 3 files | mtRygN6y |
| RAIDZ2, 4 files | QOIROzJi |
| RAIDZ3, 5 files | zfXjixrH |

All five runs passed. The original five-layout RAM creation/import/refusal
regression also passed in admission-service-run.Atkrsos9.

Combined three-boot run AGKYpbVr also passed: API creation of RAIDZ3, writable
import with three of five members withheld, native 262144-byte file writes and
flush, then another boot for read-only exact readback. Withheld members remained
unchanged, surviving members changed after writes but not during final read-only
verification, and upstream zdb over surviving members passed independently.

The runner's API-creation manifest uses the fixed private nonce 51a20519 and
zero in the unused legacy object-ID field. No object ID or pool is fabricated:
all pool creation and DOS payload operations happen through the native API.
Blank backing files alone are provisioned offline, avoiding the separately
known native FAT file-extension leak.

## Preserved failures and diagnostic corrections

- THHYuCJY: pre-fix authenticated create returned EBUSY on native FAT. Shared
  pinning conflicted with exclusive writable opens.
- KqAANhCU: native RAIDZ2 creation passed, but a live shell-runner edit caused
  an invalid subsequent host rename attempt. This run is not cold evidence.
  The full matrix above was repeated with the runner loaded into `bash -c`
  once before starting the series; executables are also snapshotted per run.
- ZDOZyRIC: combined API-created degraded import was correctly refused with
  EACCES. API creation canonicalizes volume spelling, whereas the initial
  test listed absent paths with different capitalization. The fixture now
  obtains the canonical native parent name when it knows the pool was created
  through the API. Exact missing-label-path admission was not relaxed.

## Retention and remaining scope

The optional `AROS_TEST_ISO_TMPDIR=/tmp` places reproducible per-boot ISOs in a
unique tmpfs directory; their paths and hashes remain in durable evidence.
These scratch ISOs are ephemeral across host reboot. Serial logs, sources,
binary hashes, backing images, extracted members and zdb results stay under
build-aros. No prior evidence was deleted to accommodate storage pressure.

This is isolated file-vdev evidence, not physical partition formatting,
independent controller/device failure, crash durability, or full port completion.
