# Fresh handler actual Manager and desktop acceptance

`native-gui-run.qNAU3Rvr` passed with the fresh handler selected as in
`../fresh-handler-native/`, plus `AROS_PUBLIC_GUI=1`,
`AROS_TEST_GUI_HEALTH=1` and `AROS_TEST_GUI_DESKTOP=1`.

Actual GUI interaction selected Simple / Pool health, entered public_mirror,
queried results and saved the raw response through the ASL requester to
T:OpenZFS-Response.txt. Both mirror members displayed ONLINE with zero errors.
The drawer and Manager were closed normally. The harness verified the saved
raw topology/counters/scan/checkpoint response and exact post-UI DOS readback
while Wanderer remained available; terminal storage PASS and harness exit 0.
Screenshots, logs and input identities are retained here. Own VM stopped and
slot released; foreign VM untouched. No physical-device result is claimed.

This closes the fresh-handler GUI check alongside cold persistence and native
encrypted discovery. Updated package assembly still requires its own manifest
and matching-input validation; previous bundles remain unchanged.
