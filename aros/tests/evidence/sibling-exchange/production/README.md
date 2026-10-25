# Production shared-engine integration

Source `c81be62`, private normal-service build `sibling-production.nrTm5ZCQ`,
shared library `shared-engine.rfZPXg5j`, QEMU `public-service-run.BKqlHdAu`.
All artifact directories are under
`/mnt/short-term/openzfs-qualification-20261024.aGFlVF7T`.

The normal engine was incrementally rebuilt from the retained upstream object
inventory in `source-engine.KNH9Dct2`; adapters, ZPL and selected upstream
dispatcher objects were recompiled. This is not a clean full-source build.
The shared-engine verifier accepted the normal dispatcher and rejected no
provenance checks: test modes and status tracing are disabled. The wrapper
does not embed a second engine. Build and link logs remain in the external
normal-service directory.

Eight-vCPU public-service acceptance exited zero with native partition discovery,
mounted DOS writes/reads, packaged CLI operations, authenticated pool lifecycle,
and read-only reimport. The ATA package is the full-source capacity plus flush
candidate `ata-capacity.6a3qpi9h`; the DOS-only companion is
`fat-replay.k72bijo1`. Other boot-module bytes and ordering were checked before
boot. Input hashes identify the exact older CLI/handler companion binaries.

This proves integration of the new ZPL implementation into the normal shared
service, not a public directory-exchange API. Direct exchange semantics and
concurrency are tested in the adjacent diagnostic evidence directories.
This run has `persistent=0`; it does not prove a production-service cold import.
The successful runner removed its disposable ISO, retaining logs, manifests
and backing images. No physical hardware or user hosted instance was used.
