# Native zdb damaged-label qualification

This extends the actual MetaMake shared-library build qualified in
`../native-zdb-pool-20261027`; no substitute engine or fake decoder is used.
Set `AROS_TEST_ZDB_BAD_LABELS=1` when running the retained full-pool recipe.

The runner creates exclusive new copies of the 64-MiB disposable `member0`
fixture. It zeros only the 112-KiB vdev configuration region at offset 16 KiB
within label 0 (`one-bad`) or all four labels (`all-bad`). Label locations
follow the upstream 256-KiB vdev_label_t layout. Uberblocks and pool data
remain unchanged. Fixture generation rejects preexisting destinations,
unaligned sizes and files outside 1..128 MiB. No original member is opened
for writing. Mutation offsets are retained in `mutation.json`.

`zdb-label-run.9MMYyQwP` stopped at host preflight: the initial test incorrectly
expected status 0 for one invalid label. Upstream dump_label sets its error
flag when configuration unpacking fails, even if other labels decode, so
the correct status is 1. The host failure is preserved under `preflight/`;
no QEMU guest started for that attempt.

`zdb-label-run.H3vTjNhB` passes on eight virtual CPUs, using read-only ISO
fixtures without guest disks, networking, shares, DUT or user hosted session:

1. Single-label damage: exact host diagnostic plus surviving labels 1,2,3;
   status 1.
2. Healthy invocation after the error: exact host four-label report; status 0.
3. Four-label damage: four exact unpack-failure messages; status 2.
4. Healthy invocation after that error: exact host report; status 0.

The same guest passes shared-library lifetime tests, normal repeated label
inspection and full healthy block/MOS traversal (three datasets, 21 feature
checks, 72 object rows). All source and damaged fixture hashes remain unchanged
after QEMU exits. No production code change was required for these cases.

Independent eight-CPU repeat `zdb-label-run.biFnjRXy` passes the same four
negative/recovery steps, block/MOS comparisons and unchanged-input checks.
Both runs use newly generated damage copies, frozen runner snapshots and
fresh guest boots. Their logs are retained under `first/` and `repeat/`.

Three host test methods separately check exact mutation boundaries and source
preservation, refusal of invalid fixture sizes/preexisting output, and rejection
of incomplete, duplicate, contradictory or faulting native transcripts. These
validate the harness, not the filesystem; QEMU logs are the runtime evidence.

This qualifies damaged label configurations, not arbitrary corrupt data,
rewind/repair safety, raw-device access or physical durability. Full port and
public SDK/release qualification remain incomplete.
