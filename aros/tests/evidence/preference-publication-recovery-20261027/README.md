# Preference publication recovery

Isolated QEMU run `preferences-run.UoK4w74L`, private test binary
`build-audit/preferences-recovery/OpenZFSPreferencesTest`, runner exit zero.
The entire native IFF preference suite passed.

The test-only `OZ_SETTINGS_FAULT_TEST` rename seam acknowledges publication
without moving `next`. Other renames still use DOS. Replacing an existing
valid record then returns ENOENT with published=0. The test verifies that
loading recovers the exact prior record, another save returns EBUSY, and
manual restoration of `previous` succeeds. The transaction and staged file
are removed only by explicit test recovery. No preference data is printed.

This tests OpenZFS recovery behavior under a simulated anomalous rename;
it does not fix or replace the independent muFS collision reproduction.
Production compilation has no fault seam enabled. The private test builder
alone defines the macro and supplies its implementation.

The runner now selects the matching build manifest for an overridden test
binary and explicitly passes its artifact root to completed-image pruning.
