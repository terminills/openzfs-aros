# AFS final root flush candidate

Baseline AROS source: `671efe3b321926332e3330a0e138ed4a3d4e820e`.
This is a private companion-platform overlay, not a shared AROS source edit.

The rename-fixed handler can publish OpenZFS startup preferences correctly but
then wait on AFS's "You MUST re-insert ejected volume" requester during
`Inhibit(OZARCH:, DOSTRUE)`. The traced control `public-service-run.eXPV3cG6`
shows cache flush and CMD_UPDATE return, but cache clearing does not return.
Its residual dirty/pinned block is 32768, the filesystem root. This run was
explicitly terminated through its private QMP socket after capturing that
requester and the trace; it was not classified as a machine deadlock.

`newMedium` in `volumes.c` deliberately sets BCF_USED on the root until
ejection. `flushCache` skips BCF_USED buffers, while `clearCache` displays a
requester for any residual BCF_WRITE buffer. These policies conflict when
the pinned root is dirty at final flush. No rename-existing rejection marker
appeared; the separately noticed rename-error pin leak is not this failure.

The candidate `afs-inhibit-root-flush.patch` flushes the cached dirty root
after the ordinary cache flush and before CMD_UPDATE and cache invalidation.
It uses the existing writeBlock path and does not unpin arbitrary buffers,
change intermediate flushCache semantics, or suppress the requester.
Enable it with `AROS_AFS_ROOT_FLUSH_FIX=1`; the qualification candidate also
uses `AROS_AFS_RENAME_FIX=1`. Build `afs-zvol.7Xv21fSp` has no inhibit tracing.

The full writer/cold/release test `public-service-run.hUrgoc6C` exited zero.
The writer passed both rename cases, all four fixed GUIDs, archive inhibit,
exact DOS data and clean backing detach. The cold reader passed all four
persistent GUIDs, trusted enabled restore/import/mount, wrong-GUID refusal,
healthy/degraded import, and unchanged pool member verification. Writable
cold release passed persisted deletion and an independent host zdb check.
Results and hashes are retained under `candidate/`.

This verifies the private AFS overlay with the current matched OpenZFS engine,
frontends and test client in QEMU. It does not verify an unpatched nightly,
an upstream-integrated AROS build, write-error handling or physical durability.
An independent repeat with a newly created pool/GUID is being run separately.
The optional trace patch records skipped dirty pins and up to 16 consecutive
packet-end root-pin observations per pinned interval. It is disabled by default.
