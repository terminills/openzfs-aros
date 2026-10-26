# Simple health rows: native GUI and detailed-save verification

Implementation 20d5ed1 simplifies topology rows in Simple / Pool health to
device name/type, exact reported state, and read/write/checksum counters.
It does not infer health from zero counters or erase errors on ONLINE devices.
Missing, duplicated, malformed or overflowing counters display unavailable.
Scan/checkpoint/removal diagnostics and rebuild/initialization information
remain present; readonly stored-only initialization does not become fake 0%.
Advanced rendering and Save response retain the original full response.

Host ASan/UBSan test passes exact zero/max counters, nonzero ONLINE errors,
DEGRADED/SUSPENDED/INACTIVE states, missing/duplicate/signed/overflowing fields
and short buffers. This is formatter coverage, not native filesystem proof.

Fresh native Manager build used the explicitly selected PC/SMP SDK and private
pthread pair. Manager SHA256:
`93f1477a9de7694a34be4c52ec1e632c2037bbe0e7320466366fa80959388ca9`.
Acceptance client rebuilt with the saved-response check in 09f3ef1:
`f862ed5f70a9a2830adbe6012392562b8701af6ae7cbf4e066108b5702ca313b`.
Exact build inputs and SDK identities are retained alongside the build logs.

Private native QEMU run `build-audit/native-gui-run.r7Tpu0Da`: eight CPUs,
no network, RAM-file mirror, PS/2 pointer, GUI health probe enabled. No DUT,
physical disk or user-hosted session. Actual GUI interaction queried
public_mirror health, captured the compact rows and used Save response.
After normal Manager close the guest opened T:OpenZFS-Response.txt and verified
raw pool topology, GUIDs, error counters, parent identity, scan/checkpoint text,
and absence of the simplified presentation string. The exact marker is:

    Public GUI health saved response PASS raw-topology=1 counters=1 scan=1 checkpoint=1 not-display-summary=1

The runner then passed export, readonly reimport and DOS access checks, reached
storage line=0/PASS, and exited 0. It hash-recorded and pruned the completed ISO.
First close click moved the title bar; the next click on the observed close
gadget closed normally. No timeout or force termination was used for this run.

An earlier launch was refused before image creation because root storage was
below the 100 GiB reserve. That failure log is retained. Old inactive disk
archives restored headroom; the reserve was not bypassed. See the separate
old-disk-archive-20261026 evidence.

Limits: private runtime/SDK pair, not pristine-official qualification. Live
visual case used healthy members; abnormal-state formatting is host-tested,
not newly injected in this GUI run. Full upstream feature and hardware gates
remain open. Source service was retained from the accounting-fix build; only
Manager and acceptance client were rebuilt for this change.
