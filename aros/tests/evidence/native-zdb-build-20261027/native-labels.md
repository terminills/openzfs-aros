# Native label-decoder continuation

The five initial missing symbols are now linked from the real upstream
libnvpair/libzfs_core and existing authenticated AROS client adapters.
`probe-zdb-support.sh` builds those sources and retains only their required
sections in a relocatable support object. Final AROS startup sections are
not garbage-collected. Device-path and streaming-dispatch implementations are
reused from the full engine rather than multiply defined or stubbed.

Intermediate retained failures:

- `zdb-link-probe.hZuiACzT`: duplicate device-path parser.
- `zdb-link-probe.YvsnKZoF`: streaming request implementation missing.
- `zdb-link-probe.DndQzotc`: duplicate streaming dispatch.

Final support `zdb-support.gBIS1njq` and executable audit
`zdb-link-probe.ysSf70E0` link successfully. This executable temporarily
contains the engine for dependency/runtime diagnosis, **not distribution**.
The production tool still needs its shared-library entry and lifetime rules;
this audit must not replace that design or be installed as the finished tool.

## Eight-CPU isolated native execution

`zdb-label-run.8FYZRHnS` boots the recorded alpha18c5/private DOS/POSIX runtime
with no guest hard drives, network or host filesystem share. One existing
disposable QEMU member from `native-import-run.NI5sF03m` is mapped into the
read-only ISO. Independent host zdb identifies `topo_source`, with all four
labels valid. Two native `zdb -l /SYS/zdb-member` invocations each exit zero.
Both complete 33-line reports match host zdb exactly. All recorded input
hashes, including the member, remain unchanged after QEMU exits normally.
`verify-zdb-labels.py` also rejects the retained failed run below.

Initial run `3jC9Hpb8` exhausts a 1-MiB stack in zdb: `dump_label` stores four
256-KiB label structures on the stack in addition to its other frames. The
owned guest was stopped after capture. The harness now requests 4 MiB and
explicitly detects this failure. This is a test stack adjustment, not yet a
default-stack fix for a production launcher.

Run `tF00H90g` then exits 1 because `/zdb-member` is not the correct native
volume-qualified path. Correcting the test to `/SYS/zdb-member` produces the
passing run. Neither failure was a passing decoder result. All serial logs
are retained, and only generated ISO scratch files are eligible for pruning.

Scope: native label decoding, repeat executable invocation and cleanup only.
No full native block/MOS walk, raw-device authorization, shared-library
diagnostic lifetime, physical storage or official-nightly compatibility claim.
