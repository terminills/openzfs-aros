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
The local `ncbjd29s` archive predates this change and needs refreshed packaging.
