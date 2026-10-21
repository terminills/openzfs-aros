# Guided pool editor verification — 2026-09-18

Scope: native Zune UI and Exec API, not an operational OpenZFS engine.
Changes are committed with this record; previous v1 evidence is retained in
VERIFICATION.md. The existing user-visible v1 instance on :10.0 was untouched.
No DUT, real disk, private-format Work volume or host pool was accessed.

## Build and execution

`bash aros/ui/build.sh`: warning-free ADT x86_64 AROS cross build.
`ui-build/build-guided.log` is empty. All three binaries compare equal to
the copies deployed in hosted-qa; that isolated runtime was freshly booted
on X display :93 after the final binary replacement.

SHA-256:

- Manager: d827b34967f534a0d474576cd3a976b0f4191d325d6b6b979d7a3d6f2a667f40
- Service: 5172280c27bb656c7fe273f5b589df9d02ad08f3ad4ec42ce91968bb0651659d
- APITest: d3111179f7902131add37b9c5435adb784a2726c45dfb0582afc8eb43296c07b

Native hosted results: `hosted-qa/api-roundtrip.log` has 91 PASS and zero
failures; absent and timeout logs each add one PASS (93 reported checks).
The added checks cover mirror/RAIDZ2 layouts, paths containing spaces,
field preservation across IPC, incomplete layouts, duplicate paths, invalid
group indices/counts, RAIDZ log rejection, invalid ashift, mixed raw/typed
parameters and topology on an unrelated operation. Mutation tests require
the service's explicit engine-unavailable capability before executing.

## Interactive evidence

All screenshots use capture-window.py on the exact AROS window, not desktop
rectangles. Files in ui-build:

- qa-guided-editor.png: native editor opens.
- qa-guided-validation.png: empty mirror rejected before submission.
- qa-guided-preview-fixed.png: two-path mirror preview, pool.create selected,
  paths with spaces intact, dry-run enabled, no submission.
- qa-guided-confirm.png: explicit confirmation; Cancel left the service log
  unchanged (only the manager's hello request).
- qa-guided-result.png: Submit generated request 2/opcode 5/status 2.
- qa-guided-multigroup.png: reopened retained mirror plus new log leaf group.
- qa-guided-second-result.png: a second request returned engine unavailable.

These responses are genuine API round trips, not proof of pool creation.
Full dRAID, additional guided workflows, real inventories/jobs, permissions,
secure keys and upstream engine integration remain incomplete.

## Failed hypothesis and correction

The first interactive preview stopped repainting. Serial explicitly reported
OpenZFSManager out of stack limits: task stack 40 KiB despite its __stack
declaration. Enlarging the ABI plus automatic preview/wrapping buffers caused
the overflow; it was not assumed to be a hosted display fault. Evidence is
`ui-build/qa-guided-preview-stack-failure.log` and qa-guided-preview.png.

Moved large manager buffers and APITest message storage to static storage
(one event-loop owner, no concurrent/reentrant submit handlers). Rebuilt,
replaced only the isolated QA binaries and restarted that runtime. Repeated
preview, cancel, submit, reopen, edit and second submission successfully.
User's live hosted instance and all shared SDK/OS sources were unchanged.

## Clear fields and retained add-device topology (2026-09-29)

The native Manager's Clear fields event cleared create/import topologies but
omitted the separate pool-add draft. `native-import-run.YaljMBg7` reproduces
this in the actual GUI: build a stripe with `/RAM/clear-test`, Use draft to
preview, click Clear fields, then reopen Pool builder. The name is empty but
the old group/path remain (`clear-retained-add-bug.png`). Nothing was submitted
to storage; the sample path is preview-only.

Clear fields now resets `pool_add_layout` along with create/import drafts.
The rebuilt native Manager (`manager-clear-add-build-1.log`) passes the same
clickthrough in a fresh QEMU boot, `native-import-run.leYPpFuQ`:
`fixed-before-clear.png` shows the prepared draft; `fixed-cleared-add.png`
shows empty name, groups and devices after clearing and reopening. Both runs
close normally, exit zero, and preserve original backing hashes. Evidence and
UI action logs are registered in the historical test archive. This is native GUI reset verification,
not a GUI-originated pool mutation or stock-nightly compatibility test.
