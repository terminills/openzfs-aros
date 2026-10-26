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
