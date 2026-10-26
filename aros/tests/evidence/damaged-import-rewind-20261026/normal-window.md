# Normal recovery window boundary

The initial normal-recovery success attempt `Z8jvHc1G` failed at the first
Manager `recovery=safe` request with ENXIO. This is retained as a failed
qualification attempt, not a successful normal recovery.

Inspection of the fixture's retained `labels.log` shows newest TXG 536 and
next retained TXG 520. Upstream `spa_load_best()` sets its ordinary recovery
retry-loop minimum to the latest TXG minus `TXG_DEFER_SIZE`, which is 2.
Extreme recovery uses `TXG_INITIAL` instead. This bounds whether another
retry begins, not the exact TXG selected by that retry: the first retry may
select a root below that boundary. The gap alone therefore does not prove
the entire cause of the original ENXIO. The observed refusal is qualified;
no production recovery limit was changed.

Run `m3f4gn7y` first verified Manager ENXIO, CLI failure and subsequent extreme
success. Its CLI check lacked an exact diagnostic. Stronger run `hv40YaRu`
captures actual zpool stderr and requires both `cannot import` and
`one or more devices is currently unavailable`. It passes Manager ENXIO,
CLI refusal, pool absence after both, followed by two explicit extreme
read-only recoveries with exact DOS payload and write refusal. Both original
backings and both overlays are unchanged after completion.

Use `AROS_TEST_IMPORT_REWIND_REFUSE_NORMAL=1` with this fixture. The separate
`AROS_TEST_IMPORT_REWIND_NORMAL=1` selects real non-extreme CLI/API recovery
and demands the corresponding `F` (not `FX`) result markers; it is for a
fixture with a recoverable recent root. It must not be used to claim
success for this original fixture. Subsequent positive qualification is
recorded in `recent-normal.md`. Both flags reject incompatible cold runs.

The final boundary run uses production shared engine `Rw1LBfue` and fresh
MetaMake tools `xSOZUKNQ`. The import operation's user-facing help now describes
transaction recovery separately from checkpoint rewind. This help-only
update is build-verified; earlier GUI screenshots are from `umJTAT98`.
