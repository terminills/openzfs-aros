# Native destroyed-member reuse

The CLI's upstream preflight permits destroyed pool members, but the service
previously scanned the entire extent and returned EEXIST on any nonzero byte.
The regression against `shared-engine.TEtOTOsm` failed as expected in
`public-service-run.SBoHxwdM`, displaying "pool already exists" on recreation.

The service create path now accepts a native extent with four decodable labels
that all identify DESTROYED state and the same nonzero pool/member GUIDs.
It performs this check using the already retained device reservation. Existing
authorization, partition bounds, DOS claim exclusion, overlapping reservations
and path policy remain in force. No client-supplied force flag bypasses these
checks. Other create candidates and add/attach/replace policies are unchanged.

Fresh source `source-engine.WqtNnG6P` and shared engine `shared-engine.97gavDHj`
were built independently. Eight-CPU QEMU run `public-service-run.tkfsx2L0`
passes the complete upstream storage suite, including recreation on the exact
destroyed extent without wiping or force, absence of the old root payload,
new exact data, and final destruction. Forced creates on the active and then
exported original member are refused; subsequent exact reads confirm retention.
The separate neighbor remains ONLINE with its property unchanged.

Run `public-service-run.zU8A7EgU` also passes the two-VM native encrypted-root
recovery regression with the same engine, including locked mount denial,
correct-key exact reads, read-only write refusal and unchanged disk hash.

This does not claim arbitrary-media force overwrite, nonblank file-vdev reuse,
corrupted/conflicting-label fault-injection coverage, or hardware durability.
The earlier local `mmake-bundle.ncbjd29s.tar.xz` predates this change. Both the
failing baseline and passing runtime evidence are retained; successful test
ISOs were pruned while logs and native fixture disks remain.
