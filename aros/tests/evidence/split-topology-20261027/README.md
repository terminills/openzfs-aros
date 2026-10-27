# CLI mirror split with hole and indirect top-level slots

The previous CLI admission path refused every hole or indirect child, even
when upstream `zpool split` generated it from an admitted live source. The
replacement validates the selected GUIDs, paths, child order/count, placeholder
types and hole flags against that source. It reconstructs the config from
trusted source metadata, including indirect mapping/birth object IDs; it does
not forward caller-supplied indirect metadata. Real leaf paths still pass the
existing named-policy/live-vdev admission checks.

The cleaned native nvlist is repacked only if it fits the existing bounded
request buffer. Its actual packed size is passed to the ioctl. Root spare/cache
arrays, unknown leaf types, external placeholder paths, nonempty nested
children and invalid selections are refused. Volatile vdev statistics are
omitted from the reconstructed config, not persisted as metadata.

## Verification

- Host selector tests and ASan/UBSan pass. Cases include valid interior holes,
  wrong paths, GUIDs and hole flags, unsupported selected nodes, and an indirect
  request with a forged mapping object ID. The resulting config retains the
  trusted source object IDs instead of the forged value.
- Eight-CPU native old-engine control `VI2A3Xnr` creates mirror/log/mirror and
  fails actual `zpool split` with the old unsupported-pool diagnostic.
- New engine `7lsB2LKp`, built from all engine sources in
  `mmake-engine.DKeCORt5`, passes `frD12HRs`: two mirrors with an interior log,
  omitted from the destination as a hole. Actual CLI split and separate readonly
  imports of both pools retain the exact flushed payload; writes are refused.
- Initial indirect fixture `0a5REXPd` uses 64 MiB leaves and fails removal with
  out-of-space **before split**. The corrected disposable fixture uses four
  256 MiB leaves and a 4 GiB guest. `QAdC2p69` successfully removes the first
  top-level mirror, waits for removal, splits the remaining mirror and reads
  exact data from both separately imported pools, with readonly write refusal.
- Plain two-mirror regression `lCt1lpqF` passes the same CLI split/readback
  sequence without holes or indirect children.
- In all three passing topology runs, the attached native fixture disks and
  their overlays remain unchanged: these test pools reside in guest RAM files.
- General library/service/handler/Manager regression `x4xjdYG2` passes with the
  new production library and launcher. Native runner preflight also passes.

The test uses the real upstream CLI, native authenticated admission, upstream
split and native DOS handler. It does not synthesize the pool operation.
Recorded hashes bind the selected library, launcher and commands. Each run's
private source/header, logs and frozen runner are retained outside Git; generated
terminal ISO images may be pruned after recording hashes and checking holders.

## Replay and scope

Final candidate `k323h1Zv` (full source build `mmake-engine.RcutHFL4`)
also publishes the measured packed nvlist extent to the ioctl, rather than
the original request capacity. Fresh eight-CPU runs `GmzV6VSo` (hole),
`2DBSz0vI` (indirect) and `RPxmt4rp` (plain) all pass with this candidate.
General public regression `Yh2oaa1H` passes as well. Their logs, build log and
binary hashes are retained in `final/`. The host test runner was replayed both
normally and with sanitizers using an explicit external artifact directory and
explicit host-control build; both pass. These supersede the initial candidate
results above without changing their stated qualification scope.

The wrapper and input scripts record the private dependency/SDK/OS selections;
their local paths must be supplied explicitly on another host. Use
`AROS_TEST_SPLIT_TOPOLOGY=hole|indirect|plain` with the native-import runner,
`AROS_TEST_CLI_DIR` pointing at the qualified command suite, and
`AROS_TEST_MEMORY_MB=4096` for the indirect fixture. No physical disk is opened.

These are selected-runtime, in-guest separate-import tests. Guest-RAM backing
does not persist across VM shutdown. This adds neither cold persistence nor
power-loss qualification for these topologies, nor stock-SDK compatibility,
every possible split layout, or physical-device proof. Existing independently
cold-qualified native disk splitting is recorded in `native-manager-split-20261027`.
