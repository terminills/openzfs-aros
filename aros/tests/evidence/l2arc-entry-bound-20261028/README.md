# Reject oversized L2ARC entry counts before decoding

Source inspection found that zdb passed untrusted `dh_log_entries` into
`dump_l2arc_log_entries`, which indexes the fixed 1022-element `lb_entries`
array. The reader checked block size but not this separate entry count.
The debugger now rejects larger counts after header byte swapping and magic
validation, before traversing logs, independently of verbosity. This is a
production debugger change, not a test-only substitution. The old binary was
not run against oversized counts; no old-version crash is claimed.

`build-host-zdb-overlay.sh` builds the current debugger translation units in
a private output directory using the existing host control libraries. It
does not overwrite the independent control debugger or its libraries.
The first attempt JXiS4hXI failed because its private source snapshot omitted
zdb_label_io.h; the corrected a4R8NlmV build succeeds. Known unrelated
missing-initializer warnings remain in that build log.

The real candidate passed 16 invocations across little/big endian, entry
counts 2/1022/1023/UINT64_MAX and -l/-lll. Valid counts traverse one log and
return zero; oversized counts return one with an explicit reason. The test
checks its input fixture hashes and deletes only the disposable mutated
copies after recording their hashes. No device or live pool was accessed.

All three AROS debugger units compile in zdb-probe.qqg8xsiM. Generated
MetaMake library/service/zdb targets link in mmake-debugger.5XPhlb4E with the
changed debugger source. Build inputs and output hashes are retained here.
The final source edit only reformats the guard comment after those builds.

Native execution of these changed binaries is still pending the coordinated
QEMU slot. Existing native qualification and release bundles continue to
refer to the prior library hashes; this commit does not relabel them as
qualification of the new binary. No DUT or user hosted session was touched.
