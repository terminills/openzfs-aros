# Old-tree test media archival

The user requested cleanup of the old `openzfs-aros` tree. Two batches moved
1,722 historical untracked test disks from its `build-aros` directory to the
external filesystem UUID `18df445b-e065-44d2-b4ec-d98c69af396a`.
124,909,297,664 allocated bytes (116.33 GiB) were reclaimed on root.

Every completed move required age over 72 hours, a recorded test PASS in
the run's serial log, no open handles before and after copying, an unchanged
source identity, and equal source/destination SHA-256. Sparse copies were
fsynced and the manifest persisted before atomically replacing each original
with a symlink. All 1,722 final links, destinations and sizes were verified.
The manifests retain hashes and exact paths. Source, logs and screenshots
were not removed. Data remains recoverable on the mounted external drive.

The first batch was stopped during hashing of a 3 TiB sparse capacity
fixture after 382 completed moves. That original remains a regular file;
its unfinished archival copy is also retained. The bounded batch excludes
logical sizes over 8 GiB and allocated sizes under 1 MiB, completed 1,340
moves, and exited zero. The copied script is evidence, not a repository
cleanup command to run in place.

This is retention/space evidence, not new filesystem qualification.
