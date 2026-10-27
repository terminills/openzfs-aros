# Native shared-engine candidate

`openzfs.library` contains the real upstream engine, platform adapters,
authenticated management service and DOS endpoint implementation. The small
`OpenZFSServiceNative` launcher opens it and calls vector 5. `zfs`, `zpool`
and Manager continue using authenticated IPC; no engine is embedded in them
or in the launcher. This is an experimental packaging target, not yet the
default distribution bundle or a separately loadable DOS handler.

Version 2 adds vector 6, `LONG OpenZFSRunDebugger(LONG argc, APTR argv)`.
The small `C:zdb` launcher requires version 2 and calls the real upstream zdb
implementation inside this same library. It contains no private engine copy.
The service vector remains binary-compatible with version 1 clients.

The debugger is offline: it cannot acquire the active service owner's library.
Both entry points share the same one-shot claim. Invalid argc/argv values
return 20 without consuming it; a second service or debugger run returns 10.
Close/reload is required for a fresh initialization. The debugger entry uses
`NewStackSwap` with 4 MiB, since upstream label inspection alone puts four
256-KiB labels on its stack. The original runtime error/exit targets and stack
are restored before return. This ABI is not a memory-isolation boundary.

Current native qualification covers ISO-backed label decoding, exact host
comparison, repeated launches, invalid arguments, cross-entry refusal, reload
and caller exit. Full native block/MOS traversal and production raw-device
admission remain unqualified; do not infer them from label inspection.

## Host ABI and lifetime

The standalone `build.sh` requires explicit `AROS_CC`, `AROS_SDK`,
`AROS_GENMODULE`, `AROS_LINKED_OBJECT`, `AROS_SERVICE_BUILD_DIR`,
`AROS_NVPAIR_ARCHIVE`, `AROS_PRIVATE_PTHREAD`, `AROS_CRYPTO_ARCHIVE`, and
`AROS_ZLIB_IMPORT`, plus `AROS_PTHREAD_INCLUDE`, `AROS_CRYPTO_INCLUDE` and
`AROS_TIRPC_SOURCE` for the source-built debugger. Select an ABI-matched SDK and generator; no historical
workspace path is chosen automatically. The compiler selects `nm` and
`objcopy` (`AROS_NM` can override the former). Input hashes are recorded before
building and verified afterward; compiler identity and selected tool paths
are retained. Output stays in a private directory with the storage reserve
guard. This does not download or independently qualify dependency archives.

Explicit-input build `shared-engine.GvCwxDH6` passed input stability checks
and the selected hosted CLI replication/rollback, standalone handler and
persistent/cold/degraded mirror suite in `hosted-security-test.QR9mtLvQ`.
A missing-compiler invocation failed before allocation as intended. These
checks do not establish arbitrary SDK compatibility or physical durability.

Version 1, vector 5: `LONG OpenZFSRunService(ULONG mode)`.

- Mode 0 is readonly; mode 1 requests WRITABLE and retains the real service's
  configured-security/system-identity check. Other modes return 20.
- Only the task which initialized the library may open/run it. This preserves
  ownership of its C runtime and pthread state. Other tasks use IPC.
- A valid run claims the loaded module once; repeated runs return 10. A new
  run requires closing and reloading it, not reusing torn-down upstream state.
- The launcher holds its reference throughout service execution. The last
  close requests expunge so a new task receives fresh globals and runtime.
- Mounted/retired DOS endpoints still keep the service running. This ABI does
  not bypass the existing refusal to stop while those endpoints require it.
- These rules are lifecycle constraints, not memory isolation against arbitrary
  code in AROS's shared address space.

The entry point uses the SDK's `__posixc_nixmain` startup wrapper to activate
Unix-path semantics. Because a library has no executable CRT startup, it also
supplies a scoped `__stdc_set_errorptr` slot and restores the previous pointer
before returning. It also saves/restores the C-runtime exit jump target:
nixmain otherwise leaves a target into its expired stack frame. An outer guard
returns failure if startup exits before nixmain establishes its own guard.
This preserves failure status, including denied startup.

## Build and test

The AROS build-system targets `contrib-openzfs-library` and
`contrib-openzfs-service` now generate the native module/import ABI and launcher.
`contrib-openzfs-zdb` builds the debugger launcher and is included by the main
`contrib-openzfs` aggregate. Both standalone and MetaMake library builds compile
the real debugger sources and native client support into the module.
The library depends on the source-building `contrib-openzfs-engine` target;
an explicit full normal engine object/provenance override remains available.
See the root BUILDING.md for dependency inputs and remaining qualification work.
Both build paths share `verify-engine.sh` so diagnostic-object and stale-hash
rejection remain consistent.

On 2026-10-21 the metamake-produced library and launcher passed the isolated
hosted persistent mirror test in `hosted-security-test.a7H3v5Ar`: DOS writes,
export, cold exact-data reads, degraded reads with one member missing, and
unchanged backing hashes. Host `zdb` reported no leaks. This is not a QEMU or
physical-hardware result, nor proof of the entire upstream test suite.

Run `bash aros/library/build.sh` with `AROS_LINKED_OBJECT` naming the full,
normal-service engine object, `AROS_SERVICE_BUILD_DIR` naming the successful
normal build which recorded that object's hash, `AROS_NVPAIR_ARCHIVE` and
`AROS_PRIVATE_PTHREAD` naming its matching archives. SDK, compiler, generator,
crypto and zlib import paths have explicit overrides in the script. It creates
a fresh private `build-aros/shared-engine.*` directory, uses AROS genmodule,
and renames only the engine object's main entry in a copy. It rejects injected
linker wrappers and verifies the launcher has no engine entry symbols.
Existing installed systems and the qualified monolithic build are untouched.

The native regression accepts `AROS_TEST_ENGINE_LIBRARY` and optional
`AROS_TEST_ENGINE_LIBRARY_LIFETIME`; it freezes/hashes the library and test
executable and maps them only into its private ISO. The lifecycle test checks
invalid modes, nested opens, foreign-task rejection while the owner holds a
reference, and successful reload by a different task after final close.

## Evidence and failures

- Build 1: missing generated include subdirectories. Build 2: missing module
  end marker. Both failed before any boot and remain in their build logs.
- `native-import-run.mSQX4Uhl`: native and encrypted replication paths ran,
  but file-vdev pool creation failed with ENOENT because Unix-path startup
  had not run. This is not a passing regression.
- `native-import-run.ZTJ72z0q`: startup remained unauthorized, but nixmain
  returned zero without an exit-status slot. The negative startup assertion
  caught it before the normal service launched. No authorization was relaxed.
- `native-import-run.OGD8p04K`: corrected library plus launcher passes the
  complete selected native mirror/CLI/Manager regression, host exit zero,
  unchanged original backing hashes (`shared-engine-native-3.log`).
- `native-import-run.P2w5xpGd`: dedicated invalid-mode, nested-open,
  foreign-owner refusal and new-owner reload checks pass, followed by the full
  selected native regression. Host exit zero and unchanged backing hashes
  (`shared-engine-native-4.log`), using build `shared-engine.Pli8sMxp`.
- `native-import-run.ArwKewkr`: exit-target restoration candidate also passes
  denied-run status=20, same-instance retry=10, and explicit caller exit=7
  after closing the module, followed by the full selected regression.
- `native-bundle.bhgGRwWh`: guarded distribution of library, launcher,
  Manager, upstream commands and ZVOL device. Exact-package full regression
  passes in `native-import-run.ZgS4fE5N`, and two-boot protected settings/
  import/readback passes in `startup-cold.bSKBiMO1`. Both host runners exit
  zero with unchanged original backing hashes. Eleven negative staging cases
  pass in `stage-policy.6PYAQsdz`; monolithic staging remains functional.

- `native-import-run.ogNowF7N`: the packaged shared library and launcher pass
  native RAIDZ2 writable DOS cycles and export/reimport. Separate cold readers
  `Ls1kPm5X` (all members) and `lIBxv8YZ` (only member 0, two missing) pass
  persisted-byte and namespace verification with unchanged source and writer
  images. These are selected RAIDZ2 cases, not full CLI-suite reruns.
- `native-import-run.lMh7Hnvz`: actual Manager GUI connects to the packaged
  shared engine and inspects ata.device unit 0, displaying the GPT partition
  at sectors 4097..528384. `shared-devices-query.png` records the read-only
  query; `shared-manager-final.png` records the service response and activity.
  Closing Manager normally lets the native mirror read/write regression finish
  with exit zero and unchanged source hashes. This does not exercise all GUI
  actions. Earlier `8UtQZchv` was intentionally aborted after backend cycles:
  its headless QEMU lacked screendump; it is not a GUI pass.

- `native-import-run.iBPiEHgc`: expanded lifecycle probe repeats eight
  new-owner reload / denied start / explicit caller exit / original-owner
  reacquisition cycles against the exact packaged engine. All eight markers
  pass, followed by the complete selected packaged mirror/CLI/device suite;
  runner exits zero and original backing hashes match. Probe build/source are
  retained in `library-stress.7zqjEyBp`, with a clean compiler log. This bounded
  sequence is not a memory-leak measurement or exhaustive concurrency stress.

Still required: broader multi-layout acceptance of this packaging, lifecycle
stress, separately loadable handler and production runtime/startup integration.
Existing endpoint-retention and
physical durability/disconnect limitations remain. Generated AROS startup
warnings are retained in the build logs; builds are not warning-free.
