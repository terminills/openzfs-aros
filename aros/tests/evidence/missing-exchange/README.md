# Missing exchange destination

Commit af73306 checks for a missing exchange destination before ancestry locks
and the unsupported cross-parent directory exchange guard. A directory source
now returns ENOENT, like a regular file, rather than ENOTSUP for an absent
destination in another directory. Authorization still precedes this check.

Native diagnostic tests cover same-parent and cross-parent missing destinations,
checking the original source object identity and parent attribute, both parent
sizes and link counts, and continued absence of the target entry. The runner
requires the new success marker, preventing an older binary from passing.

Eight-vCPU QEMU run `qemu-run.sdWgr5cw` passed creation, cold mirror import,
both single-member survivors and writable reopen. The creation binary was
incrementally rebuilt with the fix; import/reopen binaries are the earlier
sibling-exchange build and are identified by the retained input hashes.
This is not an all-current-binary production qualification.

Independent host zdb checks report no leaks for the original and writable
reopened pools. Their partition image hashes remained unchanged across the
checks. The production ancestry helper's host fault/cleanup test also passed;
that host test does not exercise native scheduling or the new ENOENT path.

Artifacts remain under
`/mnt/short-term/openzfs-qualification-20261024.aGFlVF7T`, with build output in
`sibling-exchange.vSn1kgrI/qemu-pool-test.c.missing-exchange.log`.
Cross-parent directory exchange remains unsupported pending two-tree ancestry
locking. No physical-drive or power-loss claim follows from these tests.
