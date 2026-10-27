# DOS-only collision reproduction

Run `public-service-run.iMZw3QVA`, acceptance client
`dependency-client.1KM8C4gC`, performs only DOS operations for this probe:
create a fresh directory and staging subdirectory, write/Flush/Close 12 bytes,
rename `stage/next` into the parent and read the destination. No OpenZFS
pool, preference codec or transaction API is used by the probe.

Collision case uses `OpenZFS-58240c24e63dee45.txn` for the staging directory
and `OpenZFS-58240c24e63dee45.prefs` for the destination. Both hash to slot
35 using AFS's 72-slot hash. Rename returns true but Open fails DOS=205.
Control uses staging directory `stage` and the same destination: Rename
returns true and exact payload readback passes. The regression intentionally
returns failure against the affected filesystem. Failed structure retained.

Offline inspection of the disposable 512-byte-block archive found:

- Parent `RenameCollision`, block 32787, hash slot 35 points to 32793.
- Staging directory block 32793 has hash-chain link zero.
- Destination file block 32794 exists with parent 32787 but is unreachable.

Source-level hypothesis, reported as TRIG-1662: `renameObject` pins source
`lastlink` with BCF_USED. When `linkNewBlock` traverses a colliding destination
bucket through that same block, `getCacheBlock` refuses reuse of a pinned
non-root block and can obtain a duplicate from disk. Rename writes the new
link buffer followed by stale lastlink, erasing the insertion. This mechanism
is not yet proven by an instrumented or patched filesystem build. Do not
globally relax cache exclusion based on this hypothesis.
