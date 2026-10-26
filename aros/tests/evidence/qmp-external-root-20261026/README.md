# External-checkout QMP admission and bounded visual replay

The QMP helper incorrectly compared the artifact filesystem with its source
checkout. Moving both onto external storage therefore rejected a valid setup.
It now compares against `/`, matching the launcher storage policy. Existing
direct-child and private-run-name restrictions remain in place.

`python3 aros/ui/test-qmp-ui-root.py /mnt/short-term /home/terminills/Documents/GitHub`
passes using a fake QMP server: same-external-filesystem checkout and artifacts
accepted; unrelated, nested, and system-filesystem destinations rejected. Only
capabilities and status requests reach that endpoint. This is tooling coverage,
not guest or filesystem qualification. Initial test attempts exposed Unix socket
path length and `/tmp` being a separate filesystem; the final test explicitly
requires a short external root and a caller-selected system-filesystem directory.

Visual runtime evidence remains **incomplete**, not passing:

- `build-audit/native-gui-run.aKyWdL8v` reached the mounted RAM-pool GUI and
  connected to the engine, but timed out with Manager open.
- A fresh private 8-CPU, no-network QEMU reused that image read-only in
  `build-audit/native-gui-run.retry20261026`. It reached the same GUI state.
  Keyboard Down selected List pools and Tab visibly focused Connect. Pool-query
  results and normal close were not observed. Pointer targeting was inconsistent;
  neither an application deadlock nor a successful query is established.
- The retry process exited 124 at its explicit 900-second timeout; QEMU recorded
  SIGTERM from timeout. No reboot or intervention in a foreign session occurred.
- Screenshots, QMP action history, serial output, and binary input hashes are
  retained in these external run directories. The copied evidence here preserves
  the final observed UI state and terminal log. The input image is unchanged.

These runs use the private runtime pair and the rebuilt native Manager/service
from the earlier accounting fix. They do not qualify the stock SDK, all UI
operations, or the complete port. No DUT or user-hosted session was accessed.
