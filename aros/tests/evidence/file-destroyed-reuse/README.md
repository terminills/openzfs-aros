# CLI and Manager API file-backed destroyed-member reuse

Eight-CPU isolated QEMU run `public-service-run.gjJbbGg6` passes with fresh
source `source-engine.lA9L4gkS` and shared engine `shared-engine.aQP3e9lF`.
The regression first creates a two-file mirror through the Manager's actual
authenticated request API, writes data through the DOS handler, and destroys
the pool through upstream zpool. Without truncating or clearing the files, it
recreates through zpool, verifies the old payload is absent, writes and reads
new data, exports, and requires both Manager API and forced CLI create to fail.
Reimport recovers the same data. A second destruction allows Manager API create
on the same files; old-data absence and new exact reads are verified again.

The former engine `97gavDHj` fails the same regression at CLI recreation in
`public-service-run.kYD3T4kw`; that failure is retained. A candidate attempt
stopped at the host 100 GiB reserve before boot; inactive evidence was moved
intact to the separate artifact disk before retrying.

The common validator requires all four decodable labels to agree on destroyed
state and pool/member GUIDs. Manager and CLI keep their existing file identity,
caller read/write access, size and type checks. No arbitrary-data/force bypass
was introduced. External writers/renames must still be excluded by the operator;
the existing file-lock handoff is not an atomic transaction against them.

The same engine passes two-VM encrypted native-partition recovery in
`public-service-run.IY73Nzji`, including locked mount denial, exact reads and
unchanged whole-disk hash. Both runs reach full upstream storage PASS.
This is service/API evidence, not a new visual Manager interaction test.
The local `ncbjd29s` archive predates this change; `reuse-package` records its
tested replacement, `mmake-bundle.exm_ebrp.tar.xz`.

## File-label refusal coverage

Eight-CPU QEMU run `public-service-run.xkaRTuJd` tests each of the four labels
in turn, first with its nvlist region cleared and then with the other destroyed
mirror member's metadata substituted (different member GUID). For every case,
both the authenticated Manager create API and forced upstream CLI create must
refuse. Complete 64 MiB snapshots of both members are compared after refusal;
no byte may change beyond the deliberate test substitution. Each original
label is restored before the next case. Successful recreation and exact DOS
readback then pass, followed by the full service/storage suite and terminal
runner PASS. Logs and binary hashes are retained in `label-negatives/`.

This tests malformed/conflicting metadata admission on private RAM files.
It does not test physical torn writes, device read errors, label checksum
validation, external concurrent writers, or general force-create support.
