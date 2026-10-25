# Ordinary rename: malformed ancestry rejection

Source fix: `9d46fe1`; required native result gate: `7871cf5`.

The unmodified ordinary ancestry-lock helper failed the new host regression
by attempting to acquire an already-held lock. The fix checks its accumulated
lock chain before acquisition and returns EIO, leaving cleanup to the caller.
The host regression passes normally and with address/undefined-behavior
sanitizers, including reference/allocation/lock cleanup checks.

Eight-CPU isolated QEMU run `qemu-run.dYlBML3F` completed all five boots:
create, read-only mirror import, each surviving member, and writable reopen.
The create log contains `Native ordinary rename corrupt ancestry PASS
unchanged=1`: task-scoped SA injection supplies a two-node parent cycle;
the operation returns EIO without changing source/destination names or parent
counts. The following normal rename succeeds. No on-disk corruption is injected.

Independent host `zdb -bcc` checks of both original and writable-reopen pools
report no leaks; all four extracted member hashes remain unchanged. Logs and
input hashes are retained here. Full run artifacts remain on the external
volume under `/mnt/short-term/openzfs-qualification-20261024.aGFlVF7T`.
After successful completion and verifying no process had the ISOs open, the
three generated boot ISOs were removed; their hashes are retained. Disk
fixtures, logs and framebuffer captures remain available.

This is a narrow malformed-parent defense, not general corrupt-pool recovery
or physical-media qualification. The test builds reuse upstream object inputs
and recompile native adapters/ZPL; they are not clean-toolchain rebuilds.
