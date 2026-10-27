# Coordinated dependency source build

The explicit `contrib-openzfs-dependencies` target now builds complete selected
pthread sources, pinned patched BoringSSL, and nvpair/memory-XDR in one private
external directory. It publishes matching shell and MetaMake inputs. It does
not modify the SDK, bootstrap AROS, or replace missing runtime APIs.

`dependencies.YF6P1Tca` built all archives from sources. The full engine
`mmake-engine.8V5IujNh`, shared library `shared-engine.3FShD5Gn`, public client
`dependency-client.WhdRYDwD`, and nine generated MetaMake tools in
`mmake-cli.G78XLweV` used those inputs. Dependency hashes still match after the
consumer builds. `dependencies.3deRmtqs` separately verifies actual execution
through the generated MetaMake preparation target with the final builder.
This second set has build evidence only, not native runtime qualification.

The preflight suite passes one valid environment and five rejected inputs:
excessive parallelism, missing pthread source, system-filesystem artifacts,
make-special path characters, and an invalid XDR archive. Its original root
case was refused silently by the writability check; the filesystem identity
diagnostic now precedes that check. The separate storage preflight suite passes.

Eight-CPU QEMU `public-service-run.QOOhG9jh` passes public-service lifecycles,
authenticated pool operations, handler DOS writes/reads, export and read-only
reimport with the first fresh dependency set. This is the selected alpha18c5
runtime with documented private DOS/POSIX overlays, not pristine nightly proof.

Retained failures:

- Initial MetaMake dry run omitted architecture variables and failed loading
  the enclosing platform configuration. The corrected dry run and real target
  invocation succeed.
- Persistent attempt `peJXexmZ` stopped before boot because explicit FAT and
  host-zdb inputs were missing.
- `gPj87UkG` passed native nvpair, PBKDF2 and guest storage, but the host FAT
  check rejected an out-of-range cluster in `OZReuseChanges/disk4`. The selected
  FAT hash `a1ba32d6...` predates the already documented BPB-bound repair; see
  `../fat-bpb-bound`. No repair or cold boot was performed on this failed image.
  It is not a passing persistent qualification.

The complete raw logs and inputs remain under the external checkout's
`build-audit`. Selected build/runtime logs and hashes accompany this record.
No DUT or user hosted session was used. SDK/toolchain provenance, stock runtime
API compatibility, and the wider feature/hardware matrix remain open.
