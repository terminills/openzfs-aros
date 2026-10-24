# Recovery of an attached, replaced and expanded pool

Eight-CPU QEMU run `public-service-run.yp3paeiP` retains the actual Manager-API
reuse target on its private FAT backing disk. The writer attaches a destroyed
member, replaces it with another destroyed member, and adds a destroyed mirror.
It waits for resilver and replacement retirement, verifies four online leaves
and exact DOS data, and exports instead of destroying the final pool.

A separate VM then imports `public_reuse_target` read-only in three phases:

0. Healthy four-member/two-mirror configuration.
1. One member missing from each mirror. The original target member `disk2` and
   added member `disk4` are hidden; retired replacement member `disk0` is hidden
   as well. Remaining `disk1` is the replacement and `disk3` belongs to the newly
   added mirror. Imports use explicit member paths, not directory discovery.
2. Restored names and healthy import again.

Each phase reads exact DOS payload bytes, denies a write and exports. The host
compares all five complete files before and after the reader VM, including the
retired file, and independently runs `zdb -bcc public_reuse_target` with no leaks.
Host zdb also leaves the files unchanged. The pre-existing persistent mirror
suite passes separately in the same boots; it is no longer the only cold test.

The fixture requires the private BPB-bound FAT rebuild described in
`../fat-bpb-bound`: the previous FAT overlay produced an out-of-range allocation
and was rejected by host fsck before cold boot. Both filesystem checks pass with
the new overlay. No repairs or integrity-check bypasses were used.

Production engine is `shared-engine.rIPiaYWt` from source `KNH9Dct2`; only the
acceptance client, runner and FAT prerequisite changed for this qualification.
Terminal writer, reader and runner PASS plus hashes are retained here.
This is clean-export recovery on virtual file-backed mirrors, not abrupt power
loss, every topology, or native-device add/attach/replace qualification.
