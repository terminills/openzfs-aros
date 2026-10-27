# Deterministic muFS publication failure

Isolated QEMU, stock alpha18c5 filesystem package, private DOS/POSIX overlays;
no DUT, shared platform source, or user-hosted session modified.

Fixed disabled startup GUID `58240c24e63dee45` reproduces the formerly
intermittent startup ENOENT before any reboot. `initial/` is run DINQe6o5.
Split-word hexadecimal formatting matches the expected literal name.

`traced/` (cOVm1hdM) shows the destination absent immediately after DOS Rename
reports success, before transaction cleanup: Lock returns DOS error 205 for
`ENVARC:OpenZFS-58240c24e63dee45.prefs`. The same record in RAM-backed ENV is
reachable. Staging serialization, Flush, Close and readback passed first.
This identifies a filesystem publication failure, not a ZFS pool read error;
the exact muFS implementation defect has not yet been established.
Reported to Claude as TRIG-1661.

## Guard

Preference storage now reads back and verifies the destination and its
trusted metadata before reporting publication or deleting recovery state.
An unverified commit retains the transaction and any saved previous version.
`guarded/` (JjlpGLIu) demonstrates errno=2, published=0, and the retained
transaction directory. This is failure detection, not a repair of muFS.
The traced source is retained for reproduction; trace instrumentation is
not enabled in production. The fixed-GUID regression intentionally fails
against this affected filesystem instead of silently selecting lucky names.

## Normal-path regression

`preferences/` (preferences-run.yRY7oeho) passed the complete native IFF
preference suite, including storage lifecycle, trusted metadata and
foreign-user denial. Runner exit zero. This does not prove physical-media
durability or resolve the muFS rename defect.
