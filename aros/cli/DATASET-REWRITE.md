# Native dataset file rewrite

`zfs rewrite` targets native files and directories, not dataset names.  On
AROS, the upstream command keeps its ordinary traversal and range parsing.
Only `ZFS_IOC_REWRITE` is adapted: `aros/cli/rewrite.c` resolves the public
clib2 file descriptor to its DOS `FileHandle` and sends `OZ_ACTION_REWRITE`
to the owning handler.

The handler validates the exact request shape, captures the current packet
sender, rechecks `ACE_WRITE_DATA` and native MuFS write policy, and passes the
open file's stable ZPL object identity to `zfs_rewrite()`.  It does not accept
a caller-supplied object number or pathname.  Read-only mounts, directories,
special files, unknown flags, nonzero private arguments and stale handles are
refused.  The logical bytes, holes, size and file position are unchanged.

The Manager operation `dataset.rewrite` builds an exact argument vector for
installed `C:zfs`; it never evaluates options as shell text.  `offset` and
`length` are unsigned decimal bytes.  `physical=on` maps to `-P` and requires
the upstream `feature@physical_rewrite` pool feature.  `xdev=on` is meaningful
only with Recursive traversal and is otherwise rejected.

As upstream does, command completion means rewrite transactions were accepted;
it is not a scrub or data-health certification.  The operation can produce
substantial pool I/O and has no dry-run mode.

## Focused package qualification (2026-10-05)

`native-bundle.LjvljAOk`, staged from committed HEAD `2796e3b6`, passed the
focused isolated runner mode
`AROS_TEST_ZVOL_TRANSPORT=1 AROS_TEST_DATASET_ADMIN_ONLY=1` in
`native-import-run.j0O1lies`.  The mandatory marker reports CLI, Manager and
packet rewrites; three exact-data checks; physical-feature refusal followed by
physical rewrite after pool upgrade; non-root and read-only refusal; malformed
packet and Manager-option rejection; and cleanup.  The companion dataset and
pool upgrade markers, export/reimport recovery, completion-port PASS, and both
unchanged native fixture backing hashes also passed.  This is a disposable
file-backed QEMU qualification, not a physical-DUT or production-array result.
