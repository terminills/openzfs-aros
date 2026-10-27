# Manager clear-with-recovery workflow

Manager's existing `pool.clear` operation retains the native authenticated
counter-clear path for empty options. Explicit `recovery=safe|extreme` options
select the asynchronous upstream-command worker. `assessment=on` adds `-n`;
extreme mode adds `-X`; `-F` is always present on this route. The worker passes
exact argv with `--`, not shell text, and uses the existing native inherited
identity/CLI transport. Backend mounted-pool, readonly, identity/readmission
and authorization checks remain authoritative. No new recovery algorithm or
claim that assessment issues zero writes is introduced.

Validation requires confirmation, no optional device GUID, no extra flags,
no cursor/job state, no typed topology, and an unambiguous option set. Duplicate,
unknown, missing, or trailing options are rejected. The host policy test passes
four accepted and nine rejected cases while preserving outputs on rejection.
Operation-routing assertions were added to the native field-schema test;
that standalone test was not executed in this qualification.

Source-built CLI/Manager: `mmake-cli.JZ90tGIt`. Public client:
`dependency-client.vAeEzG72`. Engine/library remain the dependency-qualified
`shared-engine.3FShD5Gn`; this change is a client/Manager route over its already
qualified upstream CLEAR transport, not a rebuilt engine implementation.

Eight-CPU QEMU `public-service-run.P1wFAlv9` passes actual worker execution of
recent/extreme assessments and commits on a healthy unmounted pool, three
pre-command refusals (unconfirmed, force, malformed policy), and mounted-pool
assessment/commit refusals. The complete public lifecycle/handler write/read,
export and readonly-reimport regression passes. Set
`AROS_TEST_REQUIRE_MANAGER_CLEAR=1` to require the new worker markers.

Visible native Manager run `native-gui-run.zyqvRL6C` verifies extreme assessment
preview and confirmation, then submits it against the mounted test pool. The
UI displays upstream exit=1, DOS=0, capture errno=0 and 'pool is busy'; this is
a completed error response, not a transport retry or successful assessment.
Changing assessment to off displays the destructive transaction-loss and
inconsistent-data warnings. That destructive GUI request was canceled.
Normal Manager closure and the subsequent storage suite pass. Screenshots and
input actions are retained alongside serial and runner logs.

These are the selected alpha18c5/private DOS/POSIX runtime and eight virtual
CPUs, not stock nightly or hardware qualification. Healthy clear does not prove
damaged-pool selection of an older TXG. Damaged clear recovery remains open;
the separate damaged *import* recovery evidence must not be substituted for it.
Both successful generated test ISOs were pruned by the existing runner.
