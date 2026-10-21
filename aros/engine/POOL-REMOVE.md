# Authenticated native device removal

Operation 10 accepts a pool name and exact unsigned decimal device GUID from
pool.status, or the literal argument `cancel` for active data evacuation.
It uses retained native credentials, root/config policy, writable
and nonsuspended pool checks and upstream spa_vdev_remove(..., B_FALSE).
No force, recursive, dry-run, options or automatic unspare bypass is provided.
Unknown GUIDs return ENODEV; upstream enforces removal constraints, checkpoint
conflicts, feature prerequisites, ashift/topology and available space.

Data evacuation is asynchronous. Acceptance is not completion. pool.status
now appends removal=none/running/finished/canceled/unknown, vdev ID and copied/
total bytes from upstream spa_removal_get_stats under the config read lock.
Log/cache/spare removal is separately checked by disappearance from topology.
The native UI advertises Device GUID, routes operation 10 to the authenticated
worker and warns about asynchronous evacuation in preview/confirmation.

Import admission recognizes upstream indirect vdevs with no external path and
no real children, including their generated empty children arrays. This does
not admit any additional backing-file paths: surviving file leaves must still
be explicitly listed and runtime vdev opens remain exact-path restricted.
Optional AROS_IMPORT_TRACE logs tree type/path presence and child counts;
the final passing run was rebuilt without that trace.

## Native QEMU evidence

All tests use new private RAM-file pools, never the DUT or shared hosted AROS.

- admission-service-run.axpYIFgT: cache/spare/log removal, invalid/overflow/
  absent GUID, read-only and nonroot refusals, and native DOS data read pass.
- Qq892Vju: retained failed data-removal attempt, ENOSPC after enabling the
  upstream device_removal feature. Two tiny equal mirrors leave inadequate
  space under upstream's reserved-space rules. No reserve was weakened.
- EiDNEjx6: after adding a third private data group, evacuation finished but
  reimport failed EINVAL. bqYnZJ0O confirms the native validator rejected the
  empty children array emitted for an indirect vdev. Both failures retained.
- bgCnXVBc: final focused ADD suite passes expansion, auxiliary lifecycle,
  removal of cache/spare/log, disabled-feature and ENOSPC refusals, added real
  capacity, populated data-mirror evacuation to completion, read-only reimport
  and exact native DOS payload read, writable reimport and destruction/cleanup.
  Force/dry-run/recursive/options and nonroot refusal checks pass. Baseline
  fixture hashes are unchanged. Eight private 64 MiB RAM files are provisioned.
- native-gui-run.Y1y5vZQJ: 02-remove.png and 03-remove-preview.png verify the
  native labels, operation pool.remove, entered pool/GUID and asynchronous
  warning. No service ran in this editor-only guest and nothing was submitted;
  this is preview proof, not a GUI-originated removal test.

Logs: build-aros/typed-import-service/remove-*.log. Private runners retain
their serial logs, inputs and source. The standalone ADD suite requires the
removal pass markers as well as the earlier add markers.

## Cold removal with original files unavailable

`AROS_TEST_COLD_REMOVE=1` with the cold API-create/expansion suite creates
five private files. After writing hello.txt and an 8 MiB patterned file, it adds
real evacuation capacity and removes the ORIGINAL populated mirror (disk0/1).
It requires upstream finished state and positive copied bytes, then checks one
indirect vdev, one mirror and three surviving leaves before clean export.
The host withholds both original paths. A separate guest imports using ONLY
disk2/disk3/disk4, verifies ONLINE topology, reads/enumerates hello.txt and
checks every byte of expanded.bin. All five files, including withheld ones,
must remain unchanged during the read-only boot. Host zdb receives only the
three survivors and checks all blocks/space accounting.

file-backing-run.33rALzzi passes both boots and independent zdb No leaks.
Logs: typed-import-service/remove-cold-*.log. This demonstrates persistent
indirect mapping, not merely a cached same-guest read. All files share one
private FAT image; no independent physical disk or power-loss claim is made.

## Cancellation

The cancel action calls upstream spa_vdev_remove_cancel under the same native
credential and writable/nonsuspended-pool checks. It does not remove or detach
the source. No active removal returns the engine's Solaris-compatible
ENOTACTIVE (4098), not false success. The native UI changes its warning for
cancel and still requires confirmation. Flags/options remain restricted.

The isolated `AROS_TEST_SUITE=remove-cancel` harness opts into upstream's
zfs_removal_suspend_progress test hook using the explicit WRITE-CANCEL harness
argument and required private fixture marker. The production service and API
provide no pause knob. This makes the cancellation timing deterministic;
it is not a claim of a performance or uncontrolled race test.

- admission-service-run.0A6gcvsM: observes removal=running, cancels through the
  native API, observes removal=canceled, verifies two original mirrors/no
  indirect node, checks duplicate cancel ENOTACTIVE, exports/reimports and
  reads the native payload. Read-only and nonroot cancellation are refused.
  Pool cleanup and baseline hashes pass.
- admission-service-run.t3kPQN4H: same rebuilt service without the test pause
  passes normal completed evacuation, indirect reimport and DOS payload again.
- native-gui-run.d5gmMB0g/02-cancel-preview.png: native GUID/cancel label,
  exact cancel request and source-retention warning. Editor-only unavailable
  service; nothing submitted. No GUI-originated cancellation claim.

Logs: typed-import-service/remove-cancel-*.log. The first client build failure
is retained: AROS POSIX errno lacks ENOTACTIVE, so the test now names the
engine's existing compatibility errno explicitly. No engine errno was changed.

## Interrupted removal resumption

`AROS_TEST_COLD_REMOVAL_RESUME=1` extends the cold removal suite to three boots.
The first marked private harness uses COLD-CANCEL to pause upstream removal
progress, starts removing the original populated mirror, successfully syncs,
and verifies removal=running. Only after that serial marker does the runner
SIGKILL its exact private QEMU PID (exit 137); there is no guest export or
shutdown. No original paths are withheld yet.

A fresh ordinary harness imports all five explicit files writable without the
test pause. The client requires persisted evacuation to finish with positive
copied bytes, checks topology and native DOS payload, then exports. The host
withholds disk0/disk1. A third guest imports only surviving disk2/3/disk4
read-only, verifies both payloads including every byte of the 8 MiB file, and
exports. All five member files remain byte-identical from the second export
through the final read-only boot. Host zdb receives only the survivors.

file-backing-run.OKBJhvvQ passes all three boots, recorded SIGKILL exit 137,
fresh writable resumption, final survivor-only reads and independent zdb No
leaks. Logs: typed-import-service/remove-resume-*.log. Source, paused startup,
per-phase disk images and serial/interruption evidence are retained. This is
guest-process interruption after explicit sync, not physical power-loss or
unsynced-write durability. The upstream test pause is harness-only.

Remaining: larger varied data
workloads and indirect-map verification, broader
topology/checkpoint refusal coverage, actual GUI mutation, raw device support
and eventual retired-path policy reclamation. The full OpenZFS port is not
complete. Removing a vdev from configuration does not sanitize its contents.
