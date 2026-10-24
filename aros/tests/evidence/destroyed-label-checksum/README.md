# Destroyed-member label checksum admission

The former shared engine `aQP3e9lF` accepted a member whose destroyed-pool
nvlist was intact but whose embedded checksum was damaged. In eight-CPU QEMU
run `public-service-run.KXV1pIp0`, authenticated Manager creation unexpectedly
succeeded and the new refusal test failed. This was a destructive-reuse
admission defect, not evidence of a normal pool-read checksum failure.

`pool-import.c` now validates each embedded label checksum before unpacking
its nvlist, for both native partitions and files. It uses the upstream label
checksum function, offset verifier and byte-order handling without requiring
an already-open spa. All four labels must still describe the same destroyed
pool and member. No force override or raw-media clearing was added.

Fresh source `source-engine.9o2hKeYl`, shared engine `shared-engine.f08bQXlx`:

- `public-service-run.lun7UZIU`: each of four file labels tested with cleared
  nvlist metadata, conflicting mirror-member metadata, and checksum-only
  damage. Both Manager API and forced CLI creation refuse all twelve cases.
  Both complete 64 MiB files remain unchanged after each pair of refusals;
  restoration permits successful recreation and exact DOS data readback.
- `public-service-run.A1PDQd0F`: native-partition destroyed reuse and damaged/
  conflicting-label refusals pass alongside the file cases. Active/exported
  members remain protected and the full service/storage suite passes.
- `public-service-run.nzpZ1cbj`: two-VM encrypted native-root recovery passes
  locked mount denial, correct-key loading and exact read-only data recovery.
  The entire native disk hash is unchanged. Both boots reach terminal PASS.

All runs use eight virtual CPUs and explicitly patched runtime inputs; hashes
and logs are retained here. Successful large ISO artifacts were pruned by the
normal evidence-preserving runner. Opposite-endian labels, physical torn writes,
controller read errors and concurrent external writers remain unqualified.

The earlier local archive `mmake-bundle.exm_ebrp.tar.xz` predates this fix.
Do not infer that it contains the checksum admission repair.
