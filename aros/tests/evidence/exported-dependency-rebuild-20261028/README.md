# Exported dependency rebuild and native replay

The exported pthread, Crypto and pinned libtirpc sources were rebuilt in the
independent checkout at `Amiwest/openzfs-source-replay-20261028/source`, based on
18e7afa. Outputs are in `build/exported-dependencies.jjAcgPAs`; the retained
output hashes, explicit toolchain environment and complete 7101-entry input
stability check are under `build/` here. The final input-stability check was
added to the script after that build and run separately against its inputs.

The shared engine in `shared-engine.bIyHgPvJ` was relinked against those new
archives using the previously qualified full-engine object. Its eight-CPU
QEMU replay `public-service-run.L6aa83F4` reached native nvpair and PBKDF2
known-answer PASS, plus `RESULT public service storage PASS`. The serial log
and actual ISO runtime hashes are retained under `native/`. This is not a
claim that every executable in the image was freshly rebuilt.

Persistent replay `public-service-run.IiTq2LX2` passed the writer and cold
read stages, but the writable-release QEMU invocation failed before guest
boot: its UNIX monitor socket path exceeded 107 bytes. That run remains
partial as originally invoked. The runner now uses shorter monitor
names and validates path byte lengths before ISO allocation. Boundary tests
cover 107/108-byte paths and UTF-8 byte counts; they do not replace a native
retest of the release phase.

The retained release ISO and private disks were subsequently booted using
`cold-release/resume-release.sh` and a shorter monitor path. This separate
replay exited zero: deferred parent and child snapshots were released and
remained absent after reimport, exact data and clean backing detach passed,
FAT checks passed, the startup archive stayed unchanged, and independent
host zdb reported no leaks and did not modify extracted members. Logs and a
terminal screenshot are retained in `cold-release/`. The original failed
QEMU invocation remains recorded, not rewritten as a successful full run.

A separate full source build, `source-engine.3f4plKUL`, compiled all 248 source
inventory entries and produced a fresh engine and management binaries. It was
then successfully linked into `shared-engine.ld5CZgji` against the newly rebuilt
dependencies. Input and output hashes and the compile summary are retained
under `build/fresh-engine-*`. This fresh-engine library is not yet runtime-
qualified as a replacement for the tested bundle.
The exported dependency script's six invalid-manifest rejection cases and
the two socket-path tests passed again after recovery. No DUT was used.
