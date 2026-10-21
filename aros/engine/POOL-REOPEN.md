# Authenticated pool reopen

Operation 24 now routes through the native authenticated manager worker to the
upstream reopen sequence. Root/config privilege, valid pool name, confirmation,
and an imported nonsuspended pool are required. No device arguments, force,
recursion or dry-run are accepted. Empty options/default or scrub_restart=on
allow upstream to restart a scrub if needed; scrub_restart=off sets upstream's
spa_scrub_reopen only while an active scrub is detected. The state/config lock
and reset of that flag match ZFS_IOC_POOL_REOPEN. Read-only pools are permitted.

Reopen returning zero does NOT prove ONLINE health, data repair or resilver
completion. Both help text and response explicitly require querying status.
Existing exact path admission remains enforced by the upstream vdev open path.

## Native evidence

- admission-service-run.vl7LVEvh passes all five RAM layouts: normal and both
  scrub_restart choices, mounted/open-file reopen followed by exact readback,
  read-only reopen, invalid/absent pool refusal, non-root EPERM, legacy route
  rejection, confirmation/force/dry-run/recursive/argument/options refusal.
- file-backing-run.4Z2D6mq3 passes three boots: native API creation and reopen;
  import with one mirror member absent, reopen while status remains DEGRADED,
  mounted DOS writes; fresh read-only import/reopen and exact payload readback.
  Absent member bytes are unchanged, read-only recovery changes no member, and
  independent zdb verifies the surviving member with no leaks. Native fsync
  error and FAT retry tests also run in each boot.
- native-gui-run.FLQ3Ll1y uses the existing root-login launcher (export preset
  only to obtain credentials; no export was clicked). Actual Reopen with
  scrub_restart=off was confirmed/submitted, and 05-reopen-completed.png shows
  errno=0. 04-reopen-result.png deliberately retains the earlier pending state.

These fixtures do not yet prove resumption/restart of a long active scrub,
restoration of a replaced device, physical hotplug, or behavior when a pool
becomes suspended during the operation. Those remain distinct coverage gaps.
