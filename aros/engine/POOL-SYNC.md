# Native pool sync and backing-file durability

Operation 26 is authenticated and routed from the native manager. It follows
upstream ZFS_IOC_POOL_SYNC: no root-only policy, a valid imported pool name,
read-only EROFS and already-suspended EAGAIN checks, optional configuration
dirtying under SCL_CONFIG for Force, then txg_wait_synced. Mounted filesystems
and open files are allowed. The UI requires confirmation; arbitrary arguments,
options, recursive, raw and dry-run requests remain refused. This does not
implement sync-all-pools or interruption of a wait that later suspends.

## Evidence

- admission-service-run.icxRF97K: five RAM layouts passed normal/forced sync,
  mounted/open-file sync, read-only rejection, invalid/absent pool names,
  envelope/flag refusals and non-root sync. Legacy unauthenticated routing is
  unsupported. Also runs the existing create/import/destroy regression.
  VA3HIJS2 repeats this suite successfully with the corrected private POSIX
  library, including RAM backing and the existing raw-device fixture.
- native-gui-run.7UslWqws: real service and manager as MuFS UID77/GID88.
  05-sync-result.png and 06-force-sync-result.png show errno=0 after actual
  confirmation/submission, not an editor-only preview. Guest stopped afterward.
- file-backing-run.BvzdUIVm: preserved failure. Native DOS Write and pool sync
  returned success; SIGKILL terminated the private guest with the file still
  open, without Flush/Close/unmount/export. Fresh read failed at payload length.
  Independent diagnostic-zdb-payload.log found the file persisted at zero bytes.
- With private flush-overlay.0ccycq0_, mirror QSSxnvxg and RAIDZ2 Lum2ed89 pass
  the identical abrupt-guest-loss test, exact cold DOS read/enumeration,
  unchanged member bytes during read-only recovery and independent zdb -bcc
  with no leaks. The loop waits forever after the sync marker so normal cleanup
  cannot accidentally make the test pass.
- file-backing-run.BTTQksEb repeats mirror interruption/recovery using rebuilt
  flush-overlay.g8cx3siy and a native FAT cache fault test in BOTH boots.
  The test links the actual cache implementation, injects AccessDisk failure
  on the first and second dirty writes, verifies retained dirty-list length,
  retries, and checks both persisted byte patterns. It passes. The same test
  linked with the original release cache.o fails its dirty-list assertion in
  kNMdIVOt (pool-sync-cache-retry-before.log); this expected failure is retained.
- file-backing-run.kurg8vVi adds a native test of the production fsync body
  (renamed only for injection), actual DOS Flush/DoPkt, a controlled descriptor
  lookup and a reply-handler thread. Invalid/read-only descriptors, missing
  port, unsupported ACTION_FLUSH (ENOSYS), failed reply with no IoErr (EIO),
  translated native errors and success all pass, with exact packet counts.
  It also repeats the cache test and abrupt-stop/cold recovery. Initial runs
  KOuCxClZ and WOjM4FTx preserve an incorrect test expectation: existing AROS
  ioerr2errno maps disk-write-protected to MAX_ERRNO+214 (1214), not EROFS.
  The test was corrected against the actual translator; fsync was unchanged.

## Dependency fix

AROS POSIX fsync previously called DOS Flush only: that drains caller buffering
but does not ask the backing filesystem to commit its cache. FAT also lacked
ACTION_FLUSH. The private fix sends ACTION_FLUSH after Flush; unsupported
handlers fail ENOSYS rather than claim persistence. FAT flushes dirty cache
blocks then requests CMD_UPDATE and reports errors. Cache_Flush must check its
error before RemHead: otherwise the failed block requeued for retry is removed
on the next condition evaluation and silently lost from the dirty list.

The source changes are in AROS compiler/crt/posixc/fsync.c and
rom/filesys/fat/{packet,cache}.c. build-flush-overlay.py compiles those exact
sources against the release's matching headers/object order into a private
fat.handler and complete posixc.library. No installed SDK/library or shared
hosted runtime is overwritten. A direct standalone fsync object cannot link
against private __getfdesc; that failed build is retained in
pool-sync-build-flush.log. The correct private library rebuild retains all
other module objects. Initial missing trackdisk/GNU-header compile errors are
also retained in pool-sync-flush-overlay*.log.

## Limits

This proves abrupt guest-process loss over file-backed QEMU media, not host
power failure or device-cache persistence. The examined baseline ATA
cmd_Update is a no-op: physical cache guarantees need a real driver flush path
and separate fault tests. FAT cache write-failure/retry coverage is native, but
device CMD_UPDATE failure injection is still required. The native fsync test
uses controlled descriptor lookup, not a complete third-party handler mount;
real POSIX-library/FAT integration is covered by the separate cold test.
Directory fsync remains
the existing no-op and is not covered by the regular-file fix. Nothing in these
tests establishes atomic backing ownership against external writers.
