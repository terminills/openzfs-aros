# Current engine through the MetaMake make fragment

Source revision f54fd178341523a82db78e1ba37a945f998fd805 was clean for this build.
An empty external directory, mmake-engine.nPUULvEo, compiled all 360 units in
the actual `aros/engine/build-system.mk` inventory with eight workers. No old
engine objects were seeded. GNU make directly exercised the fragment; this
is not a new complete AROS MetaMake aggregate or SDK bootstrap.

The normal-engine provenance/symbol check passed. A second identical make
invocation ran no compile/link commands and retained engine SHA256
`b735f7c0bff61e40ddb4546601b9fea5f3f1d7be42756a3cf7b90501567baf02`.
The compressed full make log, incremental log, exact sources/hashes and build
recipe are retained. The environment file is an input reference: its final
line invokes the alternate source builder, and build.sh intentionally sources
all preceding lines only.

The exact resulting object was packaged by `aros/library/build.sh` into
shared-engine.SNTTTWQk. Library SHA256:
`caf11612e643c4d7d721d0c235006405b98e4e6d6cd35e1961c97c36540bc28a`.
Previously qualified nvpair, pthread, crypto, compiler/SDK and runtime
dependencies remain selected inputs, not rebuilt or newly qualified here.

Private eight-CPU QEMU public-service-run.1YhIZtI9 exited zero. It passed the
shared-library lifetime gates, file-backed CLI/Manager force-operation matrix,
DOS/relative path checks, resilver and exact read-only file round trip, with
the full public-service storage PASS marker. Successful media were hashed
and pruned. Serial logs and hashes are retained under runtime/.

Persistent run public-service-run.eEAuPfEq also exited zero with the same
shared library and CLI. Real CLI forced dRAID spare replacement completed a
sequential rebuild and scrub with zero reported errors. A separate cold boot
passed healthy/degraded recovery, original-member absence, rebuilt redundancy,
beyond-parity refusal and restoration, encrypted data and zvol/device reads.
Both host FAT checks passed, full member hashes remained unchanged during
cold verification, and independent zdb reported no leaks. Exact serial logs,
hashes, host checks and successful-media pruning records are in persistent/.
The handler/device and corrected FAT overlay were explicitly selected prior
qualified binaries; their input hashes are retained. No DUT or user-hosted
session was accessed. Physical durability and official-runtime qualification
remain separate open requirements.
