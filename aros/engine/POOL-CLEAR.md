# Native pool/device clear

the development test archive, 2026-09-22. Native API operation 17 uses the upstream
`zfs_ioc_clear` no-rewind algorithm: missing-log/open-failure reset,
multihost suspended-pool activity check, vdev state exclusion, exact GUID
lookup, `vdev_clear`, and `zio_resume`. It does not merely edit UI counters.

Authenticated root is required. Target is an already admitted pool name;
argument is blank for the entire pool or an unsigned nonzero decimal vdev
GUID. Overflow, malformed GUIDs, topology data, options, force and dry-run
are rejected. Readonly imports are rejected before modifying pool state.
The existing admitted-file restrictions still apply when reopening devices.

The UI uses credential-capturing admission and the asynchronous worker. It
labels the GUID optional and warns that clearing recorded errors does not
establish data integrity. Query status and run integrity checks separately.

Recovery rewind is not implemented by this operation yet. Multihost conflict,
suspended-I/O resume, lost-log recovery, spare unspare, and physical hardware
recovery are not established by counter tests and remain explicit test gaps.
Those gaps do not redefine the full port's completion requirements.

## Evidence and failures

The native test harness alone seeds distinct in-memory error statistics on
two mirror members. It checks targeted clearing preserves the other member,
pool-wide clearing resets both, and non-root/readonly/invalid requests fail.
No disk data is corrupted for this fixture. A follow-up uses upstream
`vdev_degrade` on one private mirror leaf and checks clear reopens it healthy.

`admission-service-run.6Zs2uJJg`: upstream counter tests passed, but the first
API request failed validation because generic argument validation still made
the new optional GUID mandatory. Fixed the validator; no backend bypass.

`admission-service-run.6B6iHgzR`: writable API suite and readonly clear tests
passed, then the older pool-property test failed at its unset-comment check.
Commit 9d8b01a already made recognized absent pool properties return upstream
defaults. The old test expected ENOENT instead of `-`; it is now corrected.
Absent user properties retain their separate ENOENT checks.

Build and replay logs are retained under `build-aros/typed-import-service/`
with the `pool-clear-` prefix.

`admission-service-run.XnGqwJls` exited zero. The upstream degraded-member
recovery and selective/all counter checks passed; separate native clients
passed WRITE and fresh VERIFY boots, including root admission, malformed and
missing GUID refusal, readonly refusal, and non-root denial. The encompassing
admission suite passed cold persistence, permission checks, timestamp checks,
and safe shutdown, with original read-phase image hashes unchanged.
Full log: `build-aros/typed-import-service/pool-clear-run-3.log`.

The native Manager build passed (`pool-clear-manager-build.log`), with its
clear action routed through the same tested worker and optional-GUID API.
GUI replay `native-gui-run.86Lmre9t` now verifies the live native Manager:

- `03-clear-form.png`: pool-wide request with optional GUID blank, no dry-run.
- `04-confirm.png`: explicit pool.clear target and flags confirmation.
- `05-clear-result.png`: authenticated upstream clear returned errno=0.
- `06-health.png`: pool, mirror and both devices online with zero error counts.
- `09-export-result.png`: authenticated pool export returned errno=0.

The fixture then printed `GUI authenticated export absent pool PASS` after
the Manager exited. The saved clear response and activity were dumped to
`serial.log`. QEMU exited normally before offline inspection; zdb reported
no leaks and both image hashes stayed unchanged during inspection
(`offline-zdb.log`, `offline-before.sha256`). This is GUI execution evidence,
not only an editor preview or a build result. Hardware/recovery gaps above
remain unchanged.
