# Mounted legacy accounting conversion: failing reproduction

Private two-vCPU diskless QEMU run `default-quota-run.seujZQnr` exits 1.
The fixture creates a pool at SPA_VERSION_USERSPACE-1 on exclusive guest RAM
mirror files, creates a filesystem and writes a payload, verifies accounting
is disabled and incomplete, upgrades the pool while keeping the filesystem
mounted, then invokes the upstream USERSPACE_UPGRADE dispatcher.

That ioctl returns EINVAL at test line 549. The accounting conversion and
subsequent data-preservation checks therefore have NOT passed. Earlier
current-format and delegation tests do not exercise this old object-set
expansion path. The exact source delta, inputs and serial output are retained.

The test was freshly linked against the source-built engine objects from
`build-audit/source-engine.sa4fWOFY` and rebuilt AROS adapters. It uses the
private alpha18c5 runtime with the recorded DOS/posixc pair. The BSP package
was extracted unmodified from that base ISO. It is not official-runtime or
physical-hardware qualification. No physical disks or user sessions are used.

Investigation must distinguish dispatcher rejection, suspend failure and
resume failure. In particular, upstream refreshes ownership between suspend
and resume; the AROS resume guard currently compares the dataset pointer
against its pre-refresh pointer. This is a hypothesis, not yet the proven
cause. Keep ownership validation intact while investigating.

The quota runner now validates storage before allocation and honors
AROS_TEST_ARTIFACT_ROOT, allowing these iterations to remain on the external
artifact drive. Each subsequent run builds a new ISO rather than reusing a
failed image. Terminal-run ISOs can be removed after recording their hashes;
the source, input hashes and logs remain.

## Confirmed cause and verified fix

Run `default-quota-run.EWUjQbOJ` adds diagnostic-only serial markers. Suspend
succeeds; resume receives a different dataset pointer and the old pointer
comparison returns EINVAL. See `trace/`. These diagnostics are removed from
the fixed source.

Resume now compares the saved pool and dataset object number, not the old
allocation address. It does not dereference the released dataset pointer.
Task ownership, suspended state, teardown writer lock, dataset ownership and
long-hold requirements remain. GUID is intentionally not the key because
receive may replace it. The end-filesystem path retains its existing check.

Runs `default-quota-run.LbsH8j7T` and `default-quota-run.NZbDa0VE` both pass
legacy conversion and the complete default-quota suite. The latter adds an
explicit wrong-dataset resume attempt using a held snapshot in the same pool:
it returns EINVAL and retains the suspended state and teardown lock, after
which normal rollback/resume succeeds. See `fixed/` and `guard/`.

This proves mounted conversion from a pre-accounting pool, populated user
accounting, preserved file contents after reopen, and regression coverage
for native rollback/ownership on the private runtime. It does not establish
official-runtime compatibility, physical durability, or every legacy format.

## Rebuilt service regression and cold mirror

The service and Manager were rebuilt at 5f96b20 using the retained fresh-source
upstream objects and recompiled adapters. `service/rebuilt.sha256` identifies
both binaries and the fixed mount source. This is an incremental relink, not
a second from-scratch upstream build. Manager compilation is not UI runtime
proof.

Eight-vCPU run `public-service-run.BJlvvR01` used the rebuilt service through
the authenticated public client and passed accounting authorization/delegation,
mounted repeat upgrades, readonly rejection, bootenv checks and mirror DOS
read/write/export/reimport. The runner exited zero and pruned its ISO.

File-backed run `public-service-run.mVqWBgPy` also exited zero. A separate
guest boot verified healthy, one-member-missing and restored readonly imports,
bootenv persistence, exact DOS data/metadata and beyond-parity refusal with
the service remaining responsive. Pool member hashes stayed unchanged across
the cold read-only phases; independent host zdb found no leaks and did not
change either member. The runner retained hashes and pruned seven disposable
images. Evidence is in `service-cold/`.

These are regression tests with the fixed service, not an additional claim
that the cold mirror itself was created with legacy accounting. The dedicated
legacy conversion is established by the quota fixture above. All runs remain
private-runtime QEMU qualification, without DUT or user-hosted session access.

## Unmounted conversion

Run `default-quota-run.Iy06otz1` repeats the populated pre-accounting-pool
fixture twice, once mounted and once closing the filesystem before upgrading
the pool. Both ioctl conversion paths pass, report nonzero user accounting,
and preserve the payload after reopen. The full quota suite also passes.
The runner requires both markers; see `unmounted/` for the source delta,
input hashes and serial results. The disposable ISO hash is retained before
pruning. This remains private-runtime, diskless QEMU evidence.

## Installed CLI filesystem upgrade

Eight-vCPU `public-service-run.z9lP4DWs` uses the fixed rebuilt service and
the retained package-stage.8NugSU7T zfs/zpool commands. Their hashes match
the previously recorded package inputs (zfs 6aa0a454..., zpool a31f44ac...).
The acceptance client was rebuilt from current source; the commands were
not rebuilt in this run.

The client invokes C:zfs to create an unmounted version-3 filesystem on the
modern test pool, verifies version 3, mounts and writes a payload, upgrades
to version 5 while mounted, verifies version 5, unmounts/remounts and reads
the exact payload. It then unmounts and destroys only its fixture dataset.
The command sequence and full package/service suite pass, runner exit zero.
Logs, package hashes and pruned-image hashes are in `cli-upgrade/`.

This tests the installed CLI ZPL-version upgrade and data preservation. It
does not replace the pre-accounting pool conversion fixture above, and does
not claim fresh Manager UI execution or official-runtime qualification.

## Manager asynchronous upgrade worker

Run `public-service-run.SGE8ZAkj` passes a second upgrade fixture using the
same `oz_project_worker_start/poll/free` path as Manager operation 47.
Unconfirmed version-5 and confirmed invalid version-6 requests return engine
errors before command execution. Version readback remains 3. The confirmed
version-5 request succeeds, readback becomes 5, and exact payload survives
unmount/remount. Cleanup unmounts and destroys the fixture dataset. The full
public package/service suite passes and the runner exits zero.

`manager-worker/` retains serial output, input/package hashes, source delta
and pruned ISO hashes. The client is freshly rebuilt; the CLI and fixed service
are the same recorded selections as the preceding run. This validates the
actual asynchronous Manager execution path, not visual widget interaction.
