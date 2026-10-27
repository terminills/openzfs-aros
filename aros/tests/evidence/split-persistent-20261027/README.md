# Persistent CLI split topology qualification

The earlier RAM-backed CLI split tests establish separate-import data access,
not cold persistence. The extended runner creates a private FAT file container,
uses the real upstream CLI and native DOS handler, exports both pools and
cleanly detaches FAT. Host verification includes FAT consistency, independent
`zdb -bcc` for both pools, and `zdb -dddd` metadata-object reachability checks.
A cold reader copies the writer container, performs no create/split, imports
both pools readonly and checks exact payload plus denied writes. Extracted
member hashes must match the writer; the original container must be unchanged.
Only disposable regular files on external storage are used.

## Failures retained

- Initial launch rejected the undeclared FAT overlay before boot. The runner
  now narrowly permits and records it for the explicit persistent split mode.
- Second launch stopped before boot because the new cold-source variable
  collided with an existing boolean. It now has a distinct name.
- Old engine `k323h1Zv`, hole writer `YDMysEsl`: guest split/readback passed,
  FAT was clean, but destination `zdb -bcc` returned 2: histogram feature
  reference count 8 versus 7. `zdb -dddd` identified leaked MOS objects 71
  (omitted log metaslab array), 100 (empty log space map), and 101 (flush-state
  array). The source pool still references these objects correctly. A block
  accounting `No leaks` line is not sufficient to qualify metadata reachability.
- First cleanup candidate `Ysvpkhlg`, hole writer `YSODmHOy`: a source ZAP read
  under the configuration writer lock triggered `dnode_hold_impl`'s assertion.
  This private VM was stopped through its own monitor after the assertion.
  The corrected candidate reads after the existing writer-lock release while
  retaining namespace serialization. Assertion matching now accepts the serial
  timestamp prefix rather than waiting for the harness deadline.

Old-engine indirect writer `hpnbL5DU` passes guest/FAT/block checks. Initial
cleanup candidate indirect writer `4bfUZykR` does too. Neither is a cold result,
and neither earlier frozen runner included the new full-object reachability
gate. Failed images and logs are preserved; no failed image is repaired in place.

## Repair

The split cleanup now records omitted log-vdev metadata identities and frees
their inherited space maps, metaslab arrays and flush-state arrays only in the
destination MOS. It does not invoke cleanup on live source vdevs. Space-map
freeing uses upstream feature-accounted operations. Existing DTL cleanup is
preserved.

## Corrected candidate results

Full engine `mmake-engine.Oa42nOuY` and shared engine `fttbNbqO` pass:

- Hole writer `onz1rv0G` and independent cold reader `LUP8slct`.
- Indirect writer `8mtAj3cm` and independent cold reader `4Z8ty8Fo`.

All four eight-CPU guests pass real CLI/handler exact payload and readonly
write-refusal checks. Each host gate passes FAT consistency, both pool block
checks, and full metadata-object reachability. Both cold readers have exactly
the writer's member hashes; original writer containers are unchanged. Their
logs show readonly imports only, with no pool create or split in the cold boot.
These are private selected-runtime FAT-file fixtures, not hardware durability,
power-cut recovery, stock-SDK compatibility or every possible split topology.
The persistent runner admission preflight passes without allocating VM media.

General library/service/handler/Manager regression `public-service-run.LnTR6xQe`
passes with the same corrected shared engine. Its wrapper log is retained as
`public-regression.log`. The final full engine build and selected binaries are
recorded in `engine-build.log` and `candidate.sha256`.

Generated ISOs from terminated runs may be removed after recording their hash
and checking for holders. The backing containers, extracted members, frozen
sources, complete logs and old failing images remain outside Git. Selected
logs and input hashes are included here for review.
