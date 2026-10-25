# Upstream TRIM integration and remaining platform work

Operation 21 calls upstream `spa_vdev_trim()` through the authenticated worker.
Root credentials, confirmation, a writable imported pool and explicit leaf
GUIDs are required. The request is not a shell command.

- target: pool name
- argument: `start` (also resumes), `suspend`, or `cancel`
- options: `guids=123,456;rate=1048576;partial=on;secure=off`

GUIDs are mandatory; other fields default to rate 0 (unlimited), partial off
and secure off. Rate is an unsigned decimal byte-per-second count, with
overflow rejection. Duplicate/unknown fields, empty tokens and malformed
booleans are rejected. Nonzero rate, partial on and secure on apply only to
start. Force, dry-run and topology input are rejected.

Upstream per-device errors remain in the response. A request can partially
succeed on mixed-capability devices; no automatic retry is performed. The
4096-byte detail holds all errors for the bounded 32-device input, while the
structured view contains at most 24 rows. Acceptance is not completion.

## AROS discard is not implemented yet

`lib/libzpool/kernel.c:zfs_file_deallocate()` currently returns EOPNOTSUPP
on AROS. Neither AROS file hole punching nor native device discard is wired.
The inspected AROS `rom/devs/ata/ata.c:cmd_TRIM()` has its actual invocation
inside `#if (0)`; `ata_trim.c:ata_TRIMCmd()` has no active implementation.
The command number alone therefore cannot prove support or successful discard.

The AROS branch of `vdev_file_open()` now advertises `vdev_has_trim = false`,
instead of letting upstream start a TRIM worker against an unsupported
deallocator. Non-AROS behavior is unchanged. Native requests still traverse
upstream validation and return device-specific EOPNOTSUPP; they are not
reported as successful trimming. UI help states this current limitation.

Full support still requires a real backing-file deallocator and/or an audited
device discard contract, per-device capability detection, safe range mapping,
actual discard verification on disposable storage, asynchronous progress and
cold lifecycle tests. Secure TRIM must not be advertised without support.
No physical device was used and no ATA driver code was changed here.

## Native upstream CLI transport

`7836561` admits `ZFS_IOC_POOL_TRIM` through the native bounded ioctl transport.
The upstream handler retains schema validation, config privilege, read-only
pool checks and per-vdev errors. Extra inline flags, history data and client
pointer-like fields are rejected. No backing capability is enabled by this
change: `zpool trim`, `zpool trim -s` and `zpool trim -c` must report unsupported
devices on current AROS backings, rather than fail before upstream dispatch.

The native acceptance client uses the direct exact-path process launcher,
not shell status assumptions, and requires both mirror members' specific
unsupported messages. `AROS_TEST_REQUIRE_TRIM=1` makes the public-service QEMU
runner require that marker. Positive media-discard qualification is still open.

## Verification

The native fixture checks malformed options/duplicates/overflow, nonroot and
read-only refusals, both real mirror members reporting no TRIM capability,
upstream per-device unsupported errors for start/suspend/cancel, and no TRIM
threads or state transitions after refusal. The independent service client
checks the authenticated API path.

`build-aros/admission-service-run.puEUARyR` passed WRITE and fresh read-only
VERIFY: TRIM validation/unsupported capability results were zero in both,
native service/client checks and the DOS regression passed, read-only members
issued zero writes, baseline/readonly hashes were unchanged, host `zdb -bcc`
reported no leaks, and independent compression/atime checks passed. Runner
exit status was 0. Log: `build-aros/typed-import-service/pool-trim-run-1.log`.
Service, independent client and Manager builds passed. The TRIM-specific GUI
interaction and positive discard remain unverified; this is not a claim of
working media discard.
