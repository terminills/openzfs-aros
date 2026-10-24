# Retained FAT source and development package replay

Local archive: `mmake-bundle.o6mq86dd.tar.xz`, 12,439,224 bytes.
SHA256: `88fecd9c216b49373b3e41b8a31db68fdfcf63825e42643406c21d20bd12bf8b`.
It resides under `/mnt/short-term/openzfs-qualification-20261024.aGFlVF7T/`.
This remains experimental, local development packaging, not a public release.

The full FAT builder now compiles against private copies of its local and
generated headers and retains source provenance, upstream license texts,
applied patches, compilation commands and used external dependency hashes.
The corrected full-rebuild manifest records the headers actually selected
from AROS_SOURCE, rather than AROS_BASELINE_SOURCE when those differ.
Large output can be placed on the designated external filesystem without
lowering the 100 GiB artifact-filesystem reserve.

Snapshot `flush-overlay.tufmbxjd` contains 22 build commands and hashes for
259 external dependency files. No shared source-checkout file remains among
those external inputs; the matching SDK headers, linked archives and compiler
are still required. Direct replay `fat-replay.2c_bshqb` and archive-extracted
replay `fat-replay.3037x5cp` each produce the exact FAT hash
`7e15a444306fe0a1b901fcd66c7a403cca17e925385475bdaedec3760881cad3`.
Replay copies only retained C/header inputs into a new directory and compiles
there, not using cached FAT object files or the original source checkout.
The SDK/toolchain itself is not reconstructed or supplied by this snapshot.

Fresh eight-CPU QEMU run `public-service-run.vvdA9DOJ` uses the replayed FAT
binary and passes writer plus cold healthy/degraded/restored recovery,
including the changed-member pool. Both FAT checks are clean; original and
changed pool-member hashes remain unchanged during read-only recovery;
independent host zdb reports No leaks for both pools. This remains controlled
file-backed virtual-storage evidence, not hardware durability qualification.
10.51 GiB of successful disposable images were pruned with hash manifests.
Logs, binaries, source snapshots and other retained artifacts remain external.

Package staging requires `--fat-source-snapshot` and checks its manifest,
license presence, full rebuild mode, and exact selected/tested binary hash.
The archived `Sources/FAT` snapshot verifies after extraction and its replay
also produces the exact binary. Nine host packaging-gate tests and five
snapshot path/drift tests pass; they do not replace the native QEMU evidence.
The initial staging invocation used a nonexistent local GUI evidence path;
the corrected invocation uses the existing external GdR5Oa3h run.

DOS and posixc source/license packaging, complete SDK/toolchain provenance,
and the broader feature/hardware matrix remain open. Checksums detect drift,
not malicious provenance; review downloaded build recipes before execution.
