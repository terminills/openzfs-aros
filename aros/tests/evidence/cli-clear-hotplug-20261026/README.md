# Real CLI recovery after virtual USB loss

The current native fault runner now accepts explicit `AROS_CC`, `AROS_SDK`,
`AROS_PRIVATE_PTHREAD` and `AROS_PTHREAD_INCLUDE` inputs instead of hard-coded
compiler/pthread paths. `AROS_TEST_RUNTIME_PROFILE=dos-posix` selects only
the declared DOS and posixc overlays. `AROS_TEST_USB_CLI_CLEAR=1` uses the
actual `zpool` binary from `AROS_TEST_CLI_DIR`, without invoking the unrelated
whole-CLI suite. It requires writable total-member-loss mode. Exact selected
inputs are in `replay-inputs.sh`; private SDK/runtime prerequisites remain.

All runs used eight virtual CPUs, private xHCI USB storage and disposable
qcow2 overlays. The source mirror was copied from retained historical fixture
`qemu-run.2rXgZVOX`; source hashes were verified unchanged after successful runs.
No physical device, DUT, user hosted session or host storage pool was used.

Results:

- `slFXRjyu` stopped before hotplug at the old harness's startup transaction
  assertion. It constructed a legacy `.prefs.txn` name that current settings
  code refuses. The harness now calls `oz_settings_transaction_path`; production
  settings behavior was not changed. Failure evidence remains in `setup/`.
- `UcwMz7JV`, shared engine `cmPmmLLk`: sync, unplug both writable members,
  reach SUSPENDED, reinsert, reject an invalid clear GUID, then actual
  `zpool clear qemu_mirror` returns zero. ONLINE, export/readonly reimport
  and DOS payload readback pass. Completion and runner both passed.
- `CDmQoPIJ`, same engine, members swapped on reinsertion: CLI clear refuses,
  pool stays SUSPENDED, Manager independently reports ESTALE. Both overlays
  remain byte-identical to immediately before reinsertion; backing images
  also remain unchanged. Completion and runner both passed.
- `JAofqrft`, previous engine `SNTTTWQk`, identical CLI and fixture: reaches
  SUSPENDED and reinsertion, then fails at the CLI-clear success assertion
  (line 763). This distinguishes the missing native readmission hook from
  successful recovery through the new shared helper.

CLI input is the previously built `mmake-cli.f2fvfhdP`, unchanged across the
comparison. The repaired production engine was already committed as b2165ef.
This evidence covers same-path, idle writable mirror reconnection and
wrong-member refusal. It does not prove physical hotplug/power-loss durability,
in-flight user-write recovery, relocated members, or rewind recovery.

Generated ISOs were hashed and removed after each process had exited; logs,
source patches, overlay disks and fixture disks remain. The retained recipe
and inputs permit regeneration. See `pruned-isos.jsonl` for exact removed media.
