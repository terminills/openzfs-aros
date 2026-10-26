# dRAID replacement after force-admission changes

The initial eight-CPU run `public-service-run.y39LczDw` completed the guest
storage suite, including spare activation, sequential rebuild and scrub, but
host read-only fsck failed before cold boot. The selected old FAT handler
(`a1ba32d6...`) predates the already-documented BPB allocation boundary fix.
The host found an out-of-range cluster and shortened OZReuseChanges/disk4.
No repair or integrity-check bypass was used. Failed media remains in the
external build-audit run; concise failure evidence is retained here.

The corrected run `public-service-run.yiC12MK8` selected the independently
hashed FAT handler `7e15a444...` documented in `../fat-bpb-bound`. It exited
zero. Manager typed spare replacement, sequential rebuild with nonzero
rebuilt bytes, zero-error completed scrub, separate-guest healthy/degraded
recovery, original-member-absent spare recovery, rebuilt redundancy and
beyond-parity refusal/restoration all passed. Host FAT checks, independent
zdb (No leaks) and unchanged full-member hashes passed. Successful disposable
images were hashed and pruned; records are under `manager/`.

This uses the current adapter service over retained upstream engine objects,
private CRT/pthread and a selected patched FAT handler. It does not establish
official-SDK or hardware compatibility. The real CLI forced-spare path is a
separate test, not implied by this Manager result.
