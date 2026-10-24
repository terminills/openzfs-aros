# Native partition member changes

Four isolated eight-CPU QEMU runs qualify destroyed native partition admission
with production engine `shared-engine.rIPiaYWt` (source `KNH9Dct2`). All four
reach full storage PASS and terminal runner exit zero:

| Entry point | Native candidates | Run |
| --- | --- | --- |
| zpool CLI | attach and replace | `vBsRsVPz` |
| zpool CLI | add mirror | `6JR6gaEp` |
| Manager request API | attach and replace | `5bpVRnI7` |
| Manager request API | add mirror | `KslcJZ6w` |

Each VM receives a newly created private 256 MiB disk with two 64 MiB MBR
partitions. The discovery suite first validates the exact geometry and tests
encrypted pools, corrupt/conflicting labels, and safe destroyed-pool reuse.
The change suite uses those partitions via `/AROS/ata.device/0/partition/0`
and `/AROS/ata.device/0/partition/2`, with RAM files as the other members.

Attach/replace candidates must be refused while their source pool is active
and exported (also with CLI force). After explicit source destruction, attach
and replacement succeed, with resilver/replacement completion, online topology
counts and exact handler data reads checked. The add lane separately refuses
the native donor mirror while active and exported, reimports and destroys it,
then adds both native members and verifies four online leaves and exact data.
All target pools are destroyed at the end. Native labels are never wiped by
the harness. Replacement leaves a retired member's historical labels, so each
lane uses a fresh VM/disk rather than resetting labels for another attempt.

The first two runs (`ma2R1lTZ`, `wLBEvPtx`) failed in the test helper before
mutation: a full `/RAM/...` pathname did not match the engine's canonical DOS
file pathname. The helper now matches unique fixture file basenames while
retaining complete native partition identities. Those setup failures are
retained, not classified as engine failures.

Reproduce with the qualified public-service runner prerequisites and
`AROS_TEST_REQUIRE_PARTITIONS=1 AROS_TEST_NATIVE_DISCOVERY=1`, plus
`AROS_TEST_NATIVE_CHANGES=1` (CLI attach/replace), `2` (Manager attach/replace),
`3` (CLI add), or `4` (Manager add). Native-cold mode is mutually exclusive.
The runner requires the corresponding native-change result marker.

These are native AROS device-path operations on virtual disks in mixed
native/file pools, not all-native multi-controller qualification, GUI-click
proof, physical hardware durability, or changed native-pool cold recovery.
The separate `changed-pool-cold` evidence covers file-backed cold recovery.
Large artifacts remain on `/mnt/short-term/openzfs-qualification-20261024.aGFlVF7T`;
the runner prunes disposable images after success, retaining logs and hashes.
