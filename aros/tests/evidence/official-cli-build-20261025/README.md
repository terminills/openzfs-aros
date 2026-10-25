# Official SDK command-line build and synchronization adaptation

Baseline: pristine published 20261023 SDK/runtime, ISO SHA-256
`38b5689f4d5c23b0765a3fbc266c67928d841d81c754675a107dd8bc0b50d6e3`.
Dependencies are the source-built, official-SDK archives recorded in
`official-dependencies-20261025`. SDK pthread was selected explicitly; no
private pipe2 stub or modified runtime was supplied.

## Initial failure and focused port change

The initial common-source audit passed 24 of 25 units. Upstream
`cmd/zpool/zpool_vdev.c` failed because the published SDK has no `fdatasync`
declaration. A dependent link then reported the missing object. The probe now
stops after an unsuccessful common-source inventory rather than attempting
dependent links and obscuring the original failure.

The AROS SPL header maps `fdatasync` to native `fsync`: request the stronger
data-and-metadata synchronization and preserve its return value/error. This
does not implement a pretend flush, add a runtime vector, or alter non-AROS
builds. It does not repair underlying filesystem/device durability behavior.

An isolated eight-vCPU/2 GiB diskless QEMU run on the unmodified nightly tests
the actual compiled SPL call: invalid and closed descriptors return EBADF,
a RAM file's exact bytes survive the call, its current offset remains seven,
and cleanup succeeds. The test and shell report PASS. No disk/cache durability
is inferred from RAM-file evidence. The guest was stopped after completion.

## Final build results

All **25 common upstream CLI source units** compile. Full-command link results:

| Executable | Result |
| --- | --- |
| zfs | Fails: unresolved pipe2 and pwrite |
| zpool | Fails: unresolved pipe2 and pwrite |
| OpenZFSStartupMount | Fails: unresolved pipe2 and pwrite |
| OpenZFSStartup | Fails: unresolved pipe2 and pwrite |
| OpenZFSRuntimeCheck | Links; this audit did not run this copy |
| OpenZFSHostIdentity | Links; this audit did not run this copy |

These failures remain real platform prerequisites. A source implementation's
existence is not proof of a public header, import archive, registered runtime
vector, or correct semantics. No seek/write pwrite or pipe/fcntl pipe2 fallback
was added. No failed executable was run or deployed.

## Reproduction and provenance

The compile probe supports explicit external generated-header and XDR paths.
The linker supports `AROS_PTHREAD_BACKEND=sdk` and `-` as its third argument
to omit private pipe imports. Private build defaults remain unchanged.

External evidence:

- Initial compile: `/mnt/short-term/openzfs-official-deps.fVjvrmGW/cli-probe.Rk1r9FNv`.
- Final compile/dependencies: `/mnt/short-term/openzfs-official-deps.fVjvrmGW/cli-probe.0Q0O2lyH`.
- Full links: `/mnt/short-term/openzfs-official-deps.fVjvrmGW/cli-link.2TWxBYlW`.
- Native synchronization: `/mnt/short-term/openzfs-official-sync.U6UNoJ2L`.

Logs, compile flags, input hashes and launch recipe are retained here; full
objects/maps remain externally. The generated test ISO is hash-recorded and
pruned only after guest termination and an open-file check. No DUT or
user-hosted session was accessed.
