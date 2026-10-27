# AFS pinned-link rename candidate, 2026-10-27

This is a private platform-overlay experiment, not a complete cold-restore
qualification or an upstream AROS fix claim. Shared AROS source and objects
were not modified.

Baseline AROS source: `671efe3b321926332e3330a0e138ed4a3d4e820e`.
Both overlays include the existing single ACTION_DIE reply patch. Only the
candidate includes `aros/platform/afs-rename-pinned-link.patch`.

## Matched runtime observations

- Control: `public-service-run.I1ytLjDM`, handler SHA-256
  `389244ffc3d1c355acaca44bddbb9cb3e9c38d823eccf215395c394826835e72`.
  The DOS-only collision rename returned success but destination lookup failed
  with DOS 205. The non-collision case passed. The suite failed as expected.
- Candidate: `public-service-run.4lIYFI1u`, handler SHA-256
  `7f07b725576d4ed7553ff799e59c6fab334737c3c1797ce5373d6993ad6543c7`.
  Both DOS rename cases passed, as did all four fixed-GUID writer checks.
  The actual pool startup record was published and listed in the archive.
  The writer subsequently timed out after the startup-record marker. There is
  no cold-reader or release pass for this run.
- First attempts `4qCS05eD` and `UM6gxb8W` did not stage either explicit
  handler because the runner edit initially targeted the wrong conditional.
  They are not A/B evidence. The staged runs above retain explicit handler
  hashes. The invalid candidate log is retained separately.

## Mechanism and scope

`renameObject` pins the old chain predecessor. If destination hash-chain
traversal encounters that same block, `getBlock` can obtain a second cache
buffer because the original non-root buffer is marked BCF_USED. Updating the
second copy and later writing the first loses the new link. The candidate
passes the known pinned predecessor to `linkNewBlock` and reuses it on an
exact block-number match. Global cache behavior is unchanged.

Build with `AROS_AFS_RENAME_FIX=1` using
`aros/platform/build-afs-zvol-overlay.sh`, with explicit AROS source, release
build, compiler and external artifact root. Omit that flag for the control.
Use `AROS_TEST_AFS_HANDLER` with the startup-archive public service test to
stage the resulting handler. Build logs and input/output hashes are retained.

The subsequent diagnostic client adds bounded cleanup-phase markers to the
test, and the writer timeout now invokes the existing opt-in failure capture.
That follow-up must establish what happens after publication before this
overlay can be called qualified for the complete startup path.

## Cleanup localization

`public-service-run.3WjW1wTf`, using diagnostic client `dependency-client.9ojIj5R1`
and the same candidate handler, timed out at `phase=archive-inhibit`.
ENV record deletion and ENVARC reassignment returned. The writer did not emit
the cleanup-end marker. The timeout capture retained QEMU block information;
host debugger attachment failed with a ptrace error, so no host stack was
obtained. Raw results are under `cleanup-timeout/`.

`AROS_AFS_INHIBIT_TRACE=1` is an optional private-overlay diagnostic, disabled
by default. It emits boundaries for cache flush, device CMD_UPDATE, cache
clear and volume removal. It changes no operation ordering or error policy.
It is not intended for performance measurements or release packaging.
