# Native upstream channel programs

`ZFS_IOC_CHANNEL_PROGRAM` is admitted as a bounded packed upstream request.
Client pointers, config/history buffers and unrelated inline fields are refused.
Upstream owns schema validation, root-only config policy, pool state checks,
Lua execution, instruction/memory limits and returned result/error nvlists.
The real engine's Lua implementation is used, not a host interpreter or mock.
The existing libzfs_core special case does not retry channel programs on
ENOMEM: a script might already have committed changes.

Manager operation 75 invokes upstream `C:zfs program` asynchronously under the
caller's inherited identity. The target is a pool and argument is a Lua file.
Confirmation is mandatory; stdin (`-`) and unrelated flags are refused.
Options are semicolon-separated: `sync=on|off`, `instructions=N`, `memory=N`,
`json=on|off`, and repeated `arg=value` (up to 16). Numbers are positive decimal
with overflow checks; upstream enforces maximum limits. Duplicate singleton
options are refused. Arguments preserve spaces, but cannot contain a semicolon
in this UI encoding; the CLI retains its exact argument interface. No shell is
invoked. `sync=off` is upstream non-sync execution, not a rollback sandbox.
Scripts must check individual operation return codes, and errors do not promise
rollback of mutations already performed. Only trusted scripts should be run.

## Verification history

Build `source-engine.P2gN7Bsv/program-service`, log
`program-service-build-1.log`; CLI `cli-link.zhOWGskB`.
Run `native-import-run.kUkBLaPd` (`program-native-1.log`) passes real CLI/Manager
evaluation, spaced argv, JSON output, root-only refusal, non-sync mutation
refusal, snapshot creation with independent CLI enumeration, instruction-limit
termination (1100 instructions for a requested 1000, upstream hook granularity)
and subsequent valid execution. Full native suite and original backing hashes
pass, host exit 0. Evidence registered under the development test archive.

Extended run `native-import-run.vXpgmMto` (`program-native-2.log`) fails the
mounted snapshot rename assertion: Lua did not return expected EBUSY. The first
assertion did not print the numeric returned errno, so no exact return-value
claim is made. Memory/syntax extensions were not reached; host exit 1, no
full-suite or final backing-hash claim. Evidence is retained in the development test archive.

The native CLI's mount-table rename guard is bypassed by Lua synctasks. The
AROS common DSL snapshot-rename check now holds the old snapshot and rejects
long-held datasets before renaming, protecting native DOS published names
without accessing the service's mount table from a syncing worker. This also
requires other long-lived snapshot users to release their references first.
The upstream DSL object is explicitly recompiled for
`program-lifetime-service` (`program-service-build-2.log`), not merely relinked.
Run `native-import-run.JIRu1owj` (`program-native-3.log`) passes refusal while
mounted, preservation of the old name, success after unmount, instruction and
memory exhaustion, syntax errors and subsequent valid execution. The full
native regression and original backing-image hashes pass, host exit 0.
Registered under the development test archive. The two successful runs use different service
binaries: the later one includes the explicitly rebuilt common DSL guard.

These are isolated QEMU tests with private OS overlays, not stock-nightly or
hardware qualification. Interactive GUI clicks, full upstream channel-program
test-suite coverage, concurrent long-held send/receive interactions and native
mounted-operation combinations beyond the listed cases remain to be verified.
