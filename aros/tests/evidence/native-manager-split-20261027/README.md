# Native Manager disk-leaf mirror split

The Manager's operation-14 topology builder rejected every selected leaf that
was not a file, although the admission layer and upstream split support native
disk leaves. It now accepts `disk` as well as `file`, preserves the selected
leaf's GUID/path, and still checks that path against the live source's retained
admission policy. No new disk discovery or blanket path admission was added.
The UI confirmation now says exact **members**, not exact files.

## Controls and failed runs

- The extended host selector test fails against the old source at the new
  disk-leaf assertion (exit 134) and passes against the changed source.
  Invalid non-leaf selection still fails. This is a host topology test only.
- Actual eight-CPU native comparison `D4D62O6Q`, using old production shared
  engine `VpiTT74c`, refuses Manager split with errno 86 (AROS ENOTSUP).
- New engine `EAysXbuw` in `ByTY3Mtg` passes mounted EBUSY, readonly EROFS,
  non-root EPERM, successful disk-leaf split and independent imports of both
  pools with exact 1 MiB payloads. The full suite subsequently fails its stale
  expectation that healthy `clear -F` must fail. The assertion now tests all
  four supported healthy recovery variants. This first run is not a full pass.
- Rerun `jNLKMK1e` accepts the split but logs ATA03 timeout after destination
  export and ends at the harness deadline. Retained-destination variant
  `UzWClRdk` also logs ATA03 timeout, during native-member resilvering, then
  fails guest completion. These secondary-channel failures remain unresolved;
  no full mixed-backing suite pass is claimed. Failed captures are retained.

## Two-native-member writer and separate cold reader

To qualify the operation independently of the secondary ATA channel, the
explicit `AROS_TEST_MANAGER_SPLIT=write` mode uses the existing disposable
primary-channel two-disk mirror. It writes and flushes a distinct marker,
selects member 1's actual health-reported GUID, calls the authenticated Manager
split API, exports the source, and imports each resulting pool separately.
Both pools expose exact original and newly written files through the DOS
handler, refuse writes under readonly import and export cleanly, twice each.

- Writer `PaZCv4vY` passes, modifies both private overlays, and leaves both
  original raw backings unchanged.
- Its overlays are flattened into new private raw fixture
  `qemu-run.manager-split-cold.rgSYwFMp`. Fresh eight-CPU guest `Wkkw6rlt`, with
  `AROS_TEST_MANAGER_SPLIT=cold`, repeats the separate imports and exact reads
  without performing a split. Both raw backings and reader overlays remain
  unchanged.
- Extracted members pass independent host `zdb -e -bcc` for both `qemu_mirror`
  and `qemu_split`, reporting no leaks and exiting zero.

The engine was rebuilt through the actual MetaMake fragment in
`mmake-engine.a2BMGw2G`, then shared library/launcher `shared-engine.EAysXbuw`.
The Manager, including its wording correction, also rebuilds successfully in
`mmake-cli.2DYgVEWO`. Native tests exercise the real authenticated Manager API,
not a new visible GUI submission. Existing CLI binaries are recorded by hash.

Fresh public-service regression `SyyuQOFa` passes general library/service
lifecycles, handler/mirror DOS roundtrip and Manager healthy CLEAR checks with
the new engine and freshly rebuilt command suite. Initial `WWIYztH6` failed
waiting for the service because the public runner was given ENGINE_LIBRARY
rather than its documented SHARED_BUILD input: the launcher was packaged
without its library. Selecting SHARED_BUILD packages and verifies all three
shared artifacts; that corrected run passes. The failed capture is retained.

## Replay and limitations

`mixed-run-recipe.sh` records the exact selected compiler/dependency/runtime
inputs. For the all-native writer, source its settings without executing the
last line, unset WHOLE_CLI_LINK, NATIVE_SPLIT, TYPED_NATIVE_SPLIT and
CLI_POOL_CREATE (all prefixed `AROS_TEST_`), set NATIVE_WRITABLE=0 and
MANAGER_SPLIT=write, then run `aros/engine/test-qemu-native-import.sh`.
For the cold reader, select new immutable raw copies of the writer's two
overlays as NATIVE_FIXTURE and set MANAGER_SPLIT=cold.

These are isolated QEMU/selected-runtime results, not stock-nightly, physical
storage, power-loss, every topology, or GUI interaction qualification. The
separate CLI restriction on hole/indirect split configurations remains open.
Full-port SDK/bootstrap/release and hardware boundaries remain in PORT-STATUS.

Setup errors were retained externally: an initial preflight invocation omitted
its required evidence-root argument, and the first all-native harness attempt
tried recording sources before allocating its run directory. Neither booted a
guest. Both were corrected; launcher preflight and fresh native runs pass.
