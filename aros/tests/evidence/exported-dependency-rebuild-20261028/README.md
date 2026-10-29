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
qualified as a complete replacement for the tested bundle.
The exported dependency script's six invalid-manifest rejection cases and
the two socket-path tests passed again after recovery. No DUT was used.

## Fresh engine native verification

The new full engine/shared library, freshly compiled acceptance client and
Manager passed `public-service-run.d0TprxPE` (RAM-backed full service suite)
and `public-service-run.Sf2PsAeA` (persistent writer, independent cold reader,
and writable cold-release boots). The latter runner exited zero with exact
DOS readback, degraded/beyond-parity handling, persisted release, clean FAT,
unchanged startup archive and independent zdb checks. Selected CLI, handler
and platform libraries remained the previously qualified dependencies.

Actual ISO runtime hashes and serial/host results are retained in
`fresh-native/`. This does not yet qualify encrypted native discovery or the
separate native debugger matrix by itself; those follow-up results are below.

## Fresh Manager and Wanderer

`native-gui-run.vYw278Jq` exited zero using the same fresh engine, client and
Manager. The mounted OZPUBLIC drawer visibly contained its files. Through the
actual Simple-mode UI, Pool health queried `public_mirror`, displayed both
members ONLINE and saved the raw response to `T:OpenZFS-Response.txt` using the
native requester. The acceptance client verified raw topology, counters, scan
and checkpoint content rather than accepting the display summary. After the
drawer and Manager closed, exact DOS readback passed with Wanderer still alive.
Screenshots, serial log and input/actual-ISO hashes are in `fresh-gui/`.

An earlier preparation attempt refused allocation because the short-term
checkout filesystem was below the 100-GiB reserve. The successful run used
the existing independent external checkout and external artifacts; the guard
was not lowered.

## Encrypted discovery, debugger and staged bundle

`public-service-run.E1h4chWv` passed encrypted native roots, wrong-key rejection
and recovery, unchanged conflicting-label refusal, and the full service suite.
`zdb-label-run.068Hkwv9` passed GPT native pool traversal, label damage, shared
library lifecycle, privileged/unprivileged device checks and L2ARC header/log,
endian, corruption and bounds checks. Both native device overlays compared
unchanged with their originals. Serial logs and identity/comparison evidence
are in `fresh-discovery/` and `fresh-debugger/`.

The staging validator matched all 20 selected artifacts to this candidate's
four native runs. The new local development bundle is
`Amiwest/openzfs-source-replay-20261028/build/mmake-bundle.1hwujvsn` (123 MiB).
All 5,012 copied manifest entries pass and no symlinks remain. The recipe,
qualification record, manifest and verification log are in `staged/`.
The old bundle remains available and unchanged. This new bundle contains the
fresh engine, Manager and debugger, but retains the previously qualified CLI,
handler and platform prerequisites; it is not a fully bootstrapped SDK release.
