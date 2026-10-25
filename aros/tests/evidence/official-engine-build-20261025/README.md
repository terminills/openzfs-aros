# Official SDK full-engine build: link blocked by pwrite

The fresh build used the pristine published 20261023 SDK, its pthread library,
and the source-built nvpair/XDR and BoringSSL dependencies qualified separately
in `official-dependencies-20261025`. No old engine objects were copied in.

**Result: failed service link; no runtime or pool qualification.** All 248
upstream engine compilation units in the generated inventory passed, followed
by compilation of the engine/platform/service adapter objects. The AROS linker
rejected `OpenZFSServiceNative` for unresolved `pwrite`. The unresolved caller
is `engine/lib_libzpool_kernel.c.o`, retained in the external build directory.
Neither a private import-vector stub nor a seek/write replacement was added.

The engine's `zfs_file_pwrite` and optional read-dump path use this operation;
the SDK gap cannot be dismissed simply because native device-backed I/O has
its own adapter. Proper positional-I/O semantics remain a prerequisite.
Current separately inspected upstream AROS source contains an FCB-locked
implementation; that does not establish the published nightly's header,
import-library or runtime availability. The shared-runtime owner is auditing
those boundaries independently.

## Build selection change

`build-source.sh` now supports explicit `AROS_PTHREAD_BACKEND=sdk`, selecting
only the SDK archive/header pair and recording their hashes. It rejects mixed
private archive/header inputs and unknown backend values before allocation.
The original private mode retains its header-snapshot comparison. The Manager
sub-build receives the same SDK mode without private-input overrides. This is
build selection, not a fallback implementation or a compatibility claim.

Full logs, source hashes, per-file compile logs and objects remain at:
`/mnt/short-term/openzfs-official-deps.fVjvrmGW/source-engine.ry8mBCM8`.
The exact launch script, source diff, source inventory, compile summary, failed
link log, and relevant input hashes are retained here. The SDK pthread pair
was checked unchanged after the failed build. No guest was launched from its
rejected executable. Existing qualified packages were not replaced.
