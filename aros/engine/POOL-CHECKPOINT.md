# Native checkpoint control and explicit feature enablement

Operation 22 accepts create or discard and calls upstream spa_checkpoint or
spa_checkpoint_discard through the authenticated worker. Root/config policy,
pool-name validation, writable/suspended checks, confirmation, and rejection
of force/dry-run/recursive/options apply. The legacy unauthenticated path is
refused. Upstream transactions, feature/health/conflict checks and background
discard remain authoritative. Discard acceptance is not completion.

pool.status now reports checkpoint=none/exists/discarding, space and timestamp
from upstream spa_checkpoint_get_stats. Failure to obtain those statistics
is reported as unknown with an error, never as absence.

pool.set accepts feature@<upstream-name>=enabled, using zfeature_lookup_name
and spa_prop_set; upstream enables dependencies. Disabling and arbitrary
values are refused. pool.get reads active/enabled/disabled from SPA state.
The native confirmation warns that feature enablement is irreversible and
may reduce compatibility. Enabling a feature flag is not proof that all
associated AROS facilities are integrated. No features are silently enabled
by checkpoint.create: its feature is named feature@zpool_checkpoint.

## Tests and failed hypotheses (build-aros)

- admission-service-run.8BGOQC47: initial test used an invalid missing-action
  legacy request when trying to check authorization. Corrected the fixture.
- admission-service-run.q1zXJ0bY and file-backing-run.C3Z8nf69: wrong assumed
  name feature@pool_checkpoint was rejected. Corrected tests/UI to upstream
  zpool_checkpoint, preserving the rejection rather than inventing an alias.
- admission-service-run.cQ6MCSvX: all five disposable RAM-file layouts pass:
  stripe, mirror, RAIDZ1/2/3. Covers unsupported-feature refusal, explicit
  enabling, invalid value/feature, no-checkpoint discard, create, duplicate
  create, reguid conflict, read-only import persistence/refusal, background
  discard completion, nonroot and legacy/flag/confirmation refusals, native
  DOS payload and the existing 40-mount endpoint lifetime regression.
- file-backing-run.lgWMfI3f: separate guest boots prove checkpoint persistence,
  active feature, conflict errors, native payload/directory access and byte-
  identical read-only members. Original independent verifier failed its
  histogram refcount check despite no block leaks. See CHECKPOINT-VERIFIER.md
  for the on-disk diagnosis, separate fix 25742c7 and same-byte re-verification.
- file-backing-run.Sb0S26eX: histogram-enabled RAIDZ1 two-boot run passes,
  including independent zdb, unchanged read-only members, fsync error paths
  and FAT dirty retry. Mirror covers old format; RAIDZ1 covers new format.
- native-gui-run.ZRd0Hs03: real native feature confirmation (02), successful
  feature set (03), checkpoint confirmation/create (04/05), status=exists
  (06), discard warning (07), accepted discard (08), status=none (09).
  Private QEMU guest stopped through QMP with launcher exit 0. No DUT or
  user-hosted session was accessed.

Checkpoint rewind import is now wired; see CHECKPOINT-REWIND.md for its
separate five-boot persistence and read-only evidence.

Not yet covered: asynchronous discard
interrupted by guest loss, suspended pools, device-removal/RAIDZ-expansion
conflicts, all individual upstream feature lifecycles and compatibility-file
policy. Full OpenZFS port completion is not claimed.
