# Retained POSIX ABI include qualification

The former snapshot `posix-source.krrt6d62` retained an absolute
`LC_LIBDEFS_FILE` pointing into the private MetaMake build. Its replay therefore
did not establish independence from that generated header. Historical runtime
results remain valid for that binary, but not as source-closure evidence.

Commit `1f344fb` rewrites the wrapper to include the retained ABI header by
name and rejects dependencies under the private build after compilation.
The distribution gate also rejects the old absolute-include snapshot.

Fresh external snapshot `posix-source.oxlli56c` built successfully. Replay
`fat-replay.xuc5c11y` compiled retained sources without cached objects and
produced the byte-identical library:

`8c9fffde57fdc57cb7525d07587b6ffbd5acf4810274828bd5cae3827a106525`

Eight-CPU isolated QEMU run `public-service-run.IqJwuyxv` passed service
lifecycles, mirror DOS round trip and native cold checks with that library.
The cold reader verified locked/mount-denied state, key loading, exact reads,
read-only write refusal and export. Hashes and result logs are retained here.
Full snapshot, replay and run logs remain under
`/mnt/short-term/openzfs-qualification-20261024.aGFlVF7T`.

The 2 POSIX snapshot, 6 generic snapshot and 11 staging host tests passed.
SDK/toolchain inputs remain external hash-bound prerequisites. This is not a
full SDK source rebuild, physical hardware qualification or full-port completion.
No DUT or user hosted session was used.

## Persistent and desktop qualification

`public-service-run.C69WOMvl` passed persistent healthy/degraded/restored cold
imports, trusted ENVARC startup restore, exact DOS reads, unchanged members,
and writable recursive snapshot release followed by independent zdb checks.
The staging companion, changed-pool and optional-release acceptance functions
all pass on this run. The initial `public-service-run.YVDxBTDJ` invocation was
rejected before boot for combining persistent and native-partition fixtures;
its log is retained rather than counted as a runtime test.

`native-gui-run.EzUsTnYs` used the same runtime and production engine with the
current Simple-view Manager. The screenshot shows `public_mirror ONLINE` and
files in Wanderer. Normal Manager close was followed by the harness's exact
post-UI data read and final storage PASS. UI action coordinates and result logs
are retained in `gui/`; this was a private QEMU, not the user's hosted session.

These tests qualify inputs for staging; they are not themselves a newly built
or archive-extracted distribution bundle.
