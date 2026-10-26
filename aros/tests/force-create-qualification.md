# Force-create qualification in progress

The private QEMU baseline in `build-audit/public-service-run.KL3OjoXH`
reached `Public force-create prerequisites PASS`, preserving the source
payload across active and no-force refusals. It then failed at
`exported-force-create`, as expected with the previous service and CLI.
This is failure reproduction, not a passing acceptance test.

The current implementation transports an AROS-only boolean create intent
from zpool to the service, strips it before upstream creation, and requires
captured privileged credentials. Exported members must have four coherent,
checksummed labels. The live SPA namespace is checked even for exported
labels, since a read-only import can retain those labels.

The service cross-build passed using retained upstream engine objects and
rebuilt adapters; see `build-audit/force-create/service-build.log`.
The updated runtime test additionally checks read-only imported source
refusal and preservation. The rebuilt CLI and passing QEMU replay are
recorded below. This does not qualify arbitrary nonzero-media overwrite, Manager
force semantics, or force add/attach/replace.

## Current CLI build

All common CLI sources and dependencies were freshly compiled in
`build-audit/cli-probe.miMoVFxl`. The final link in
`build-audit/cli-link.dr92BqmY` passed for zfs, zpool, startup commands,
runtime check, and host identity. This uses the private pthread/CRT inputs,
not the blocked official-SDK qualification lane.

An intermediate link used a stale pipe2 import slot (793). Inspection of
the retained CRT's generated dispatch table established slot 799; the
private import was corrected and all commands relinked before runtime.
Neither intermediate CLI build was executed. Logs are retained under
`build-audit/force-create/cli-link*.log`.

The exported-member denial regression now checks omission of force, while
active-member denial still supplies force. The separate acceptance test
requires exported recreation to succeed and checks readonly imported
source preservation. Runtime results are recorded in
`build-audit/force-create/replay-matched.log`.

## QEMU result

The eight-core private QEMU run `public-service-run.PNPRlR82` exited zero.
It proved active and readonly-imported source refusal with payload retained,
no-force exported refusal, successful force recreation of exported file
members, absence of the old payload, and exact new payload after readonly
reimport. The complete public-service storage suite also passed. Retained
serial results and input hashes are in `evidence/force-create-20261026`.
The completed test ISO was hashed and pruned by the runner.

This is file-backed runtime qualification with private CRT/pthread inputs;
it is not native-device force recreation, physical durability, or full-port
completion. Manager force and arbitrary nonzero-media force remain open.
