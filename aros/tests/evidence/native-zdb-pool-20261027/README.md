# Native block and MOS inspection of an isolated pool

The actual MetaMake-built `openzfs.library`/`zdb` from
`mmake-debugger.ZqUKKwpH` now passes native `-P -e -p ... -bcc topo_source`
and `-P -e -p ... -dddd topo_source`. This uses the same version-2 shared
library and small launcher as the previous label/lifetime qualification;
no new engine implementation or statically embedded diagnostic is substituted.

The fixture consists of `member0` and `member2` from the retained disposable
`native-import-run.NI5sF03m` split test: a two-member file pool with a special
allocation class. Both files are mapped into the guest's read-only boot ISO.
No writable guest disks, networking, host filesystem share, physical DUT or
user-hosted session is involved. Full source-member and other input hashes
remain unchanged after the guest exits.

## Evidence sequence

- `zdb-label-run.XRCRIqW0`: both native commands exit zero, but the initial
  line-oriented copier inserts log prefixes into metadata lines longer than
  its buffer. This is retained as an initial run, not exact-report proof.
- `zdb-label-run.lF4TbJYB`: bounded, offset-numbered hex chunks preserve every
  byte of both captured stdout report files, including long lines. Native and
  host use `-P` to report sizes as exact integers; the host uses UTC.
- `zdb-label-run.dteyA29o`: fresh eight-CPU repeat using the frozen runner,
  current comparison verifier and explicit missing-report failure marker.
  All comparisons and unchanged-input checks pass again.

Both final runs also pass the two exact label decodes and shared-library
lifetime tests: invalid arguments, one-shot cross-entry refusal, unload/reload
and caller exit. The launchers receive 64-KiB stacks; the library provides its
own 4-MiB debugger stack.

## What the comparison proves

Both native pool commands exit zero. Native `-bcc` reports no leaked blocks
and matches all seven host block accounting counters: 90 block pointers,
2,637,312 logical bytes, 66,560 physical bytes, 173,568 allocated bytes, and
zero ganged, deduped or cloned block counts/bytes in their respective fields.
The native metadata walk matches all three dataset/root-block descriptions,
21 feature-reference verdicts, and 72 object rows including exact integer
size columns and object types. Neither native report contains a contradictory
refcount/MOS-leak/assertion verdict. Host block/MOS commands also exit zero.

This is not byte-identical presentation of every report field. The derived
object `%full` column is excluded from comparison because host and AROS printf
round halves differently (28.12 versus 28.13). Progress output and NaN spelling
also differ. All captured native stdout bytes are preserved in decoded logs;
the verifier does not remove or replace them to manufacture a match.

Six host verifier tests cover exact accounting, missing/different inventory,
contradictory integrity markers, lossless long/binary records, malformed chunk
offsets/sizes, and conflicting exit markers. These test the evidence parser,
not the filesystem. Native QEMU results supply the runtime evidence.

Scope remains this healthy disposable file pool. Corruption cases, additional
topologies/options, native raw-device authorization, public SDK/runtime and
release-bundle qualification remain separate. This result does not prove
physical durability or completion of the full OpenZFS port.
