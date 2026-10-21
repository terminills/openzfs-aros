# Native hosted verification — 2026-09-18

Application/service source: `37f7c7c` (UI/API, not OpenZFS engine integration).

Built with the ADT x86_64 AROS cross compiler. Final `-Wall -Wextra` build log
`ui-build/build-final.log` is empty: no compiler warnings or errors.

Binary SHA-256 (matched against the deployed hosted files):

- OpenZFSManager: `b9b0acc3e47f14050cc71a32b2a2456031274221cbe6cf2785f8161f6e122ab5`
- OpenZFSService: `28b1736bc5cda6ddcd30c19e3c04f6197720e5995d0fdcb55f879fc2c633dedf`

Fresh hosted AROS boot, not Linux execution of the client:

- `hosted-ui/api-roundtrip.log`: 80 PASS, zero failures. Includes all 77
  operation IDs (two service queries plus 75 engine-dependent operations),
  version/opcode rejection and field/confirmation validation. Capability gating
  is also checked before any mutation-routing sweep.
- `hosted-ui/api-absent.log`: absent service PASS.
- `hosted-ui/api-timeout.log`: nonresponding test service PASS after timeout.
- Same suite passed in the separate `hosted-qa` runtime on X display :93.
- `hosted-qa/api-service.log`: interactive GUI pool-create request reached
  opcode 5 and returned status 2 (engine unavailable), not a simulated success.

Interactive QA on :93: preview submitted nothing; Cancel on the confirmation
dialog submitted nothing; Submit reached the real service; Save activity log
reported successful file write. Screenshots: `ui-build/qa-preview.png`,
`qa-confirm.png`, `qa-submit-result.png`. These show the no-engine response and
the exact disposable test operands. No disks or pools were opened.

Visual iterations fixed an over-wide layout and clipped response text on the
800x600 hosted desktop. The visible user instance is on :10.0, outer window
named **AROS**, with the manager and service left running. Do not automate
clicks in that instance while the user tests it. No DUT access occurred.

Screenshot correction: screen-region captures can show an overlapping browser
or editor. `capture-window.py` resolves the exact AROS title and captures that
window with xwd; it refuses ambiguous window matches or an unknown pixel format.
Use `python3 aros/ui/capture-window.py ui-build/current.png` on the user's
display, or pass `--display :93` for isolated QA. Old region captures are not
valid provenance for the AROS window; use the direct-window captures instead.

Limitations: no engine operations are implemented by the management adapter yet;
no real pool inventory, job progress, authorization or key handling is claimed.
The GUI has a full pinned operation catalogue and generic request/property
editors, not yet all dedicated guided workflows (topology tree, secure key
prompt, recovery wizard). See PROTOCOL.md for the remaining integration work.
