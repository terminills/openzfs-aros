# Native upstream pool history

Operation 33 is an authenticated read through the native worker. It requires
upstream root/config privilege (`secpolicy_zfs`), a pool name and an optional
absolute logical byte cursor. The unauthenticated legacy route refuses it.
Force, recursive, argument and options fields are rejected. Raw, dry-run and
confirmation flags are accepted for this read-only operation.

The adapter reads upstream `spa_history_get` and decodes records with
`zpool_history_unpack`. It does not synthesize history from the UI log.
Summary pages contain at most 24 records. Their state contains the record
offset, used/available/total carry time/uid/txg, and the UI labels those fields
accordingly. Long summary text ends in `...`. Summary decoding limits one
record to 1 MiB; raw mode remains available for larger records.

Raw pages contain up to 24 rows of 96 original bytes, hex encoded. Name is
the absolute byte offset and total is the byte count. Raw mode preserves all
record fields and long text, not just the summary. Next-page requests use
the authenticated worker too. A zero next_cursor marks the observed EOF.
Each response is a live read, not an immutable history snapshot: upstream
may append or overwrite circular history between pages. A payload offset
jump/short read is ESTALE; invalid cursors and decoding failures return an
error with no partial rows. Suspended pools return EAGAIN.

The native boundary proof walks the complete raw byte stream to recover its
observed logical EOF, then checks both raw and decoded-summary operation 33
views. That exact byte offset is an exhausted empty page; EOF+1 and
UINT64_MAX are rejected with EINVAL and no partial rows. This validates the
byte-offset cursor independently of the lower-level ioctl wire test.

The pool-create regression also creates a private version-1 pool, confirms
its on-disk version through the packaged `zpool`, and submits authenticated
operation 33 requests in both summary and raw modes. Pools older than
`SPA_VERSION_ZPOOL_HISTORY` return ENOTSUP with no rows and no next cursor;
the error detail retains the exact errno. The fixture then destroys the pool
and deletes its 128 MiB RAM backing.

The isolated USB total-loss safety fixture exercises the suspended-pool branch
against a genuinely suspended writable mirror after both members are removed
and reinserted. Authenticated summary and raw operation 33 reads both return
EAGAIN with no rows and no next cursor; a following health query still reports
SUSPENDED, proving that the service remains responsive without claiming pool
recovery. The clean committed proof uses the fixture's default two guest CPUs
and `pci-ohci`. Two otherwise identical 16-CPU clean attempts timed out during
initial USB import before any removal marker or history request, so they do not
qualify (or refute) suspended history at that topology.

## Verification

All paths below are under build-aros; all guests were isolated with no NIC.

- admission-service-run.AbqiL994: initial native write and fresh-boot read
  passed, including two raw pages. A compiler warning in summary formatting
  was subsequently removed with explicit bounded copying and ellipsis.
- admission-service-run.PsJqy1kW: complete raw stream framing and record-count
  agreement with all summary pages passed on both boots.
- admission-service-run.DrWLM0wl: final regression additionally writes a
  400-byte property value, verifies its summary is visibly abbreviated and
  recovers all 400 bytes through the raw stream across page/row boundaries.
  Write: 224 records, 10 summary pages, result=0. Fresh boot: 358 records,
  15 pages, result=0. Root/nonroot policy, invalid cursor and flag rejection,
  existing DOS handler tests and independent compression/atime verification
  all pass. Logs: pool-history-long-run.log and the directory's write-serial.log
  and verify-serial.log. No private test guest remains running.
- native-gui-run.xYjnJ43g: actual native manager authenticated summary page 1
  and page 2, then raw pages 1 and 2. Screenshots 04-history-page1.png,
  06-history-page2.png, 08-history-raw.png and 10-history-raw-page2.png retain
  the visible responses. The final raw page starts at offset 2304. This used
  the root login preset; no export was clicked. Guest stopped through QMP.
- External archive run `native-import-run.WUElB0Al`, committed source
  `527c3394` and clean package `native-bundle.oTAr5WRm`: a real
  1,052,672-byte user command record was recovered exactly by libzfs. The
  authenticated summary reader returned EOVERFLOW (errno 84), zero rows and
  zero next cursor at the record boundary after two normal pages. Raw mode
  reconstructed the exact record across 462 pages. The independent ioctl-wire
  verifier captured 1,077,408 retained bytes and replayed them across 264
  short pages, then passed all six malformed requests, canaries, EOF and
  recovery checks. The full native regression completed `PASS line=0` and
  both original backing hashes remained unchanged.
- External archive run `native-import-run.WCogqLXM`, committed source
  `f09ac659` and patch-free clean package `native-bundle.i1hURMsL`: a real
  pthread writer completed 240 `zfs set` commands while an authenticated raw
  reader deliberately advanced in 1024-byte pages. The reader consumed
  966,824 bytes across 945 pages, observed 172 forward cursor jumps while the
  writer was active, discarded partial framing only at those explicit jumps,
  parsed 200 complete records and reached coherent EOF. A stable replay then
  retained 499,880 bytes across 123 pages with one logical BOF jump; all six
  malformed requests, canaries, EOF and recovery checks passed. The newest 20
  command markers remained and the oldest was absent. The full native mirror
  regression completed `PASS line=0`, and both original backing hashes were
  unchanged. The package archive SHA256 is
  `2cba18d4b7a0d61597aa6557ebfd0f85bcb6c6e588f0d358a27f624927210c2f`
  (13,181,300 bytes).
- Exact-package run `native-import-run.b0mEwuBG` replays that same package and
  helper with eight virtual CPUs. Its concurrent reader consumed 973,992 bytes
  across 952 pages, observed 172 overlap-time jumps, parsed 204 complete
  records and reached coherent EOF. Stable replay and the newest-marker suffix
  checks passed, as did the full native mirror regression and both original
  backing hashes.
- Clean committed AHCI run `native-import-run.89xxTVT9`, source `bc0e4d47`,
  helper `cli-link.WMI6GYez` and patch-free package
  `native-bundle.58W5Goph`: both mirror members used one QEMU `ich9-ahci`
  controller and `ahci.device` paths. The reader consumed 969,896 bytes across
  948 pages, observed 172 jumps while the writer remained active, discarded
  172 partials only at explicit jumps, parsed 201 complete records and reached
  coherent EOF. Stable replay retained 499,880 bytes across 123 pages with one
  logical BOF jump. Property catalogues, AHCI CLI discovery/import, service
  CLI, two final import cycles, completion line zero and both pristine backing
  hashes passed. The production client remains at its three-second timeout;
  only the debug-heavy AHCI verifier binaries use ten seconds. Retained runs
  `3UGuVg3O` (hard-coded ATA discovery path), `jbCwuHWX` (three-second verifier
  timeout), `f85GJgm1` (pre-boot DOS-overlay argument) and `c3icfYih`
  (600-second host deadline) preserve the distinct failure chain.
- Exact-package AHCI run `native-import-run.n1ru63XV` changes only the guest to
  eight virtual CPUs. It consumed 975,016 bytes across 953 pages, observed 172
  writer-overlap jumps, discarded 172 partials only at explicit jumps, parsed
  204 complete records and reached coherent EOF. Stable replay, the retained
  suffix, full native regressions, two final import cycles, completion line
  zero and both pristine backing hashes passed.
- Committed-source xHCI run `native-import-run.aD5cw6Hg`, verifier commit
  `54ad893e`, uses the same immutable package/helper with two removable
  `usb-storage` mirror members behind QEMU `qemu-xhci`; CLI path checks select
  `usbscsi.device`. Its concurrent reader consumed 971,944 bytes across 950
  pages, observed 172 jumps while the writer remained active, discarded 172
  partials only at explicit jumps, parsed 204 complete records and reached
  coherent EOF. Stable replay, the newest-marker suffix, USB CLI discovery,
  service CLI, two final import cycles, completion line zero and both pristine
  backing hashes passed. The writable USB gate refuses all hotplug/recovery
  combinations. A retained `pci-ohci` attempt deadlocked before its first
  writable import result through OHCI completion SoftInt -> `FreeVec` ->
  recursive `nommu_FreeMem` allocator locking; it is linked as the historical test archive the development test archive
  and is not counted as history evidence.
- Exact-source xHCI run `native-import-run.klSKOeOC` changes only the guest to
  eight virtual CPUs. It consumed 1,013,928 bytes across 991 pages, observed
  170 writer-overlap jumps, discarded 170 partials only at explicit jumps,
  parsed 237 complete records and reached coherent EOF. Stable replay, the
  newest-marker suffix, full native regressions, service CLI, two final import
  cycles, completion line zero and both pristine backing hashes passed.
- Exact-source xHCI SMP2 writer `native-import-run.7XP6K60F` at commit
  `b21d6055` has a zero-byte source patch. It consumed 972,968 bytes across 951
  pages during 240 concurrent writes, observed and discarded partial framing
  only at 172 explicit overlap-time jumps, parsed 202 complete records and
  reached coherent EOF. It retained `qemu_mirror/cli-import`; stable replay,
  service CLI, two final import cycles, completion line zero and both pristine
  backing hashes passed. Fresh-guest reader `native-import-run.VfARC5Cq` uses
  child qcow2 overlays over that completed writer at the same zero-patch
  commit. Its readonly import found the persisted `history-thread-beta` marker
  in `qemu_mirror`, excluded the cross-pool and forbidden markers, reread the
  retained dataset, passed native cold-write verification, completed two final
  import cycles and exited at completion line zero. The full backing chain and
  cold overlays passed their checks. This is independent virtual-boot
  persistence evidence, not physical-media durability evidence.

The opt-in large-history fixture now forces serial circular overwrite and
verifies the one logical BOF cursor jump while preserving the newest records
and dropping the oldest marker. The separate megarecord fixture now verifies
records larger than the summary bound through libzfs and authenticated raw
operation 33. The concurrent fixture now verifies circular overwrite while a
real command writer races a deliberately slow authenticated reader. That
qualification covers isolated 4 GiB IDE guests with two and eight virtual
CPUs plus isolated 4 GiB AHCI guests with two and eight virtual CPUs. USB
concurrent overwrite is additionally qualified with qemu-xhci at two virtual
CPUs and eight virtual CPUs, with an xHCI SMP2 cold writer/reader chain; USB
hotplug/removal, pci-ohci pending the development test archive and physical hardware remain outside
this evidence.
The UI saves the displayed response, not an automatically collected archive
of every page. This feature is not a claim of complete OpenZFS port coverage.
