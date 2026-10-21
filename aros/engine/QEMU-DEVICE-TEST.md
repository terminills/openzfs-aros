# Native QEMU device lane

## Current verification and limits

Latest replay after the native buffered-UIO implementation:
`build-aros/qemu-run.TSAmyAKJ/` (summary `build-aros/uio-qemu-test.log`). The
create boot additionally tests scatter/gather, partial/repeated moves,
non-consuming copy, skip, offsets above 4 GiB, overflow and malformed spans.
All four boots and the no-write/hash/zdb gates pass. This does not enable
handler writes; it verifies native buffer primitives needed by the ZPL port.

After unifying the AROS engine/ZPL attribute ABI, a full native rebuild passes
all four boots again in `build-aros/qemu-run.Kkgo6M2F/`, with unchanged backing
hashes, zero cold/degraded writes and the independent zdb check. Summary:
`build-aros/attributes-qemu-test.log`. Actual handler access includes nested
directories, repeated file reads, locks, parent traversal and capacity queries.
Handler writes, full ACL/ZIL semantics and production packaging remain work
in progress. Sections below preserve prior evidence and its historical limits.

## Degraded mirror reads

`build-aros/qemu-run.dAx9cLFu/` passes four independent boots: create mirror,
healthy cold import, read using only member 0, and read using only member 1.
Both degraded imports explicitly report vdev health 6 (DEGRADED) and pass
the DOS file/directory/lock/Info/parent-path tests. Cold/degraded boots make
zero guest writes; original member hashes match; Linux zdb finds no leaks.
Summary: `build-aros/qemu/degraded-reproduce2.log`.

Important test distinction: member-0 survivor boot physically omits drive 1.
The member-1 survivor boot needs a fresh blank, NON-pool drive in IDE slot 0.
The earlier pure slave-only run `qemu-run.iWciTNYZ` failed because the nightly
ATA driver rejected both channel units during enumeration, then spa_import
returned ENODEV (19). We did not change ATA or call that an OpenZFS failure.
With an enumerable channel, the blank disk has no pool labels and the second
member alone supplies all data. No resilver/write/replacement claim is made.

Nested-directory and AROS parent-slash traversal now pass both boots in
`build-aros/qemu-run.BBnAkHsp/`; 81 blocks independently checked by Linux zdb.
The fixture contains hello.txt and subdir/nested.txt. Zero cold writes and
unchanged original hashes remain required. Summary: `qemu/nested-reproduce.log`.

Latest replay with DOS Info capacity/read-only checks:
`build-aros/qemu-run.9gu4cPMM/` (both boots PASS, zero cold writes and unchanged
backing hashes). Management IPC mount/unmount integration has additionally
been tested on isolated hosted AROS; see `MOUNT-API.md` for evidence/limits.

Reusable frontend extraction also passes both boots in
`build-aros/qemu-run.Vpm07yDc/` (summary `qemu/lifecycle-reproduce.log`).
`dos-handler.c` now supplies explicit mount/signals/pump/unmount; the separate
client verifies duplicate mount rejection and EBUSY on unmount with an open
file or live root lock. Successful teardown happens after all clients finish.
Arbitrary concurrent DeviceProc teardown still needs a retirement protocol;
the current unmount contract requires callers to quiesce new clients first.

`build-aros/qemu-run.ycBu4ZGy/` passes both fresh creation and separate cold
boot with DOS Lock, DupLock, SameLock, Examine, ExNext, ParentDir, CurrentDir
relative opens, and the previous repeated file reads. Both report
`DOS lock/enumeration/relative-open tests stage=6 result=0`. Cold import made
zero guest disk writes; both backing-file hashes are unchanged. Linux zdb
checksums the pool without leaks. Summary: `build-aros/qemu/directory-reproduce2.log`.

The preceding run `qemu-run.gPOeeYPU` failed stage 2: ACTION_SAME_LOCK must
return a DOS boolean, not public SameLock's LOCK_* value (LOCK_SAME is zero).
That packet/API mismatch was fixed and both boots rerun. Compiler warnings
also caught a flexible-array zap_attribute_t allocation error before testing;
enumeration now uses upstream zap_attribute_long_alloc/free.

At that earlier root-directory test, nested paths and reusable service mount
lifecycle were incomplete; subsequent runs above cover those additions.
Writes, full ACL/ZIL semantics and production packaging remain incomplete.

This lane uses ONLY two explicitly supplied disposable 256 MiB hard files as
`ata.device` units 0 and 1. Never attach host disks to this VM. The `/AROS/`
adapter is a bounded test interface, not production device discovery or a
partition-aware mount interface. It requires `OPENZFS_QEMU_DEVICE_IO=1`.

`device-test.c` passed write, CMD_UPDATE, read, byte comparison at 1 MiB and
at the last 4096 bytes on both disks. It also rejects out-of-bounds reads.
Evidence: `build-aros/qemu/device-test-serial2.log`. The adapter gives each
worker its own IORequest/reply port and never shares task signal ownership.

`qemu-pool-test.c` refuses disks with nonzero label regions. The initial version
only tested the mirror/DMU layer; it now also invokes the reusable DOS handler
test against the persisted upstream ZPL fixture.

## Preserved failures

* ISO boot-image replay failed on overlapping GPT entries; test ISOs use the
  existing BIOS GRUB image explicitly, not modified shared release artifacts.
* Device probe v1 faulted in PosixC stdio. Probe reporting now uses native DOS
  Write plus serial, which allowed the native device tests to run and pass.
* Pool boot 1 faulted before main in PosixC's StdCBase relative wrapper (NULL
  base at `__stdc_get_errorptr`). No pool operation was reached.
* Pool boot 2 substituted hosted C runtimes and failed earlier in IntelGMA's
  PosixC IsRootOfPath. Rejected: do not mix that runtime into this native ISO.
* Link attempt 3 used -B/-L, but map inspection showed GCC's sysroot path
  precedes -L and still selected the other SDK's libraries. Not boot-tested.
* Link attempt 4 explicitly sets --sysroot to the native nightly Developer
  tree; the link map confirms native pthread/posixc and startup. Runtime test
  pending at this note's creation; do not infer success from linking.

Logs and screenshots for failed boots remain under `build-aros/qemu/`.

## Native SDK and timer reply stall

Boot 4 (native SDK link) entered spa_create and wrote disk data but stalled.
Boot 5 with one virtual CPU also stalled. Boot 6 rebuilt all objects with the
native SDK headers (including its __AROSEXEC_SMP__ definition), but still
stalled. SDK consistency is required; it was not the cure for this stall.

QEMU register and memory captures identified Kernel_52_KrnSpinLock and its
caller Exec InternalPutMsg. The lock was in a stack MsgPort and held garbage
pointer values rather than initialized spinlock state. Inspection of the
nightly libpthread.a OpenTimerDevice confirms it does not zero that MsgPort.
The current ADT source already does: compiler/pthread/pthread.c, change first
present in a342421c (commit title "bump version"). A locally compiled copy of
that source is linked into test 7, ahead of the old archive, without changing
the shared SDK or source. Test 7 result remains pending at this entry.

**Boot 7 PASS:** upstream spa_create mirror=0; DMU 16384-byte write=0;
export=0; fresh engine read-only import=0; complete data comparison=0.
Both raw devices were accessed through native AROS Exec I/O. This establishes
pool/DMU access, NOT handler/ZPL access. Evidence:
`build-aros/qemu/pool-test-serial7.log`. pthread.c source SHA256:
`aa24f46b4bb4f42688b7f033e857b0d77b83b9c605d7e7d1ef155db0ee707728`.

## First actual DOS handler access

Boot 8 created a genuine ZPL v5 filesystem using upstream DMU/ZAP/SA APIs,
then re-imported and read its hello.txt. Linux zdb independently decodes the
filesystem directory and 60-byte ZFS plain file with system attributes.
It then caught an adapter cleanup bug: FTAG expands per function, so own and
disown tags differed across open/close helpers. Both now use the stable fs
instance as their ownership tag. Failed assertion and screenshot retained.

**Boot 9 PASS:** native two-device mirror create, export, fresh engine RO
import, ZPL file byte verification, then actual AROS DOS Open/Read/Seek/Close
through the registered OZTEST: handler, repeated three times. Missing file
returns ERROR_OBJECT_NOT_FOUND; write-open returns ERROR_DISK_WRITE_PROTECTED.
Cleanup and kernel_fini returned normally. Evidence:
`build-aros/qemu/pool-test-serial9.log`.

This is a real but intentionally limited read-only packet frontend, not a
complete production handler: only root-level regular files, no directory
locks/enumeration or writes yet. The disposable ZPL fixture's permissive ACL
is test-only. No claim of full ZPL/ACL/ZIL semantics or complete OpenZFS port.
Cold-machine-boot import remains the next verification step.

## Cold boot and reproducible acceptance PASS

Boot 10 exported a 1760-byte upstream XDR configuration through the captured
serial log. Boot 11 did not start: QEMU's IDE frontend rejects readonly hard
disks. Boot 12 instead used explicit COW overlays over the original two hard
files, while OpenZFS and the native device handles were opened read-only.
Fresh AROS boot: spa_import=0, ZPL open=0, repeated DOS file tests=0, PASS.
QEMU block statistics show **zero guest writes** to either hard disk; SHA256
of both original hard files remained unchanged. Linux zdb -bcc verified all
79 allocated blocks with no leaks. This proves actual handler read access to
persisted ZPL data after a machine reboot, not only an in-memory reopen.

The full independent script replay also passed using fresh hard files:
`build-aros/qemu-run.GiwT9bmN/`. It contains both serial logs, QEMU logs,
monitor I/O counters, screenshots (PPM), input hashes, source identity/diff,
export.nvlist, disk checksums, disk images and independent Linux zdb results.
Summary: `build-aros/qemu/reproduce-test.log`.

Reproduce from this repository:

```sh
bash aros/engine/build-qemu-test.sh
bash aros/engine/test-qemu-mirror.sh
```

The test creates two new 256 MiB hard files, a mirror with a ZPL filesystem,
and mounts the limited DOS frontend as OZTEST: for the bounded test. It runs
Open/Read/Seek/Close three times, verifies contents and EOF, verifies missing
file and read-only failures, then cleanly unregisters the handler. It repeats
those checks after a separate AROS boot using the same persisted pool. All
test QEMU instances exit afterward. No DUT, existing disk or shared OS build
is modified. These tests do not claim a permanently installed full handler,
write support, directory enumeration, crash recovery or complete UI coverage.
