# Native force-add visual qualification

Private 8-vCPU QEMU run `native-gui-run.ior2hVdk` completed with exit 0.
It used the fresh-source Manager from source-engine.phZ4CAdP, shared engine
Nmm3fNwi, CLI 29cnkfv7, and the current acceptance client. Exact binary hashes
and the QMP action sequence are retained alongside the serial result log.

The actual native Manager connected, switched to Advanced/Pools/Add, and
opened the typed **Add devices - new groups only** builder. A stripe group
with `/RAM/preview-only` was drafted for the nonexistent `preview_pool`.
The unused argument field was initially populated, then cleared before the
typed builder was used. No review/submit action was performed.

The final preview visibly shows Force=yes, Dry-run=no, the exact draft path,
and these warnings:

- Adds new groups rather than replicas; later removal may be unsupported.
- Force permits differing sector-alignment shifts (ashift).
- Forced exported-member reuse can destroy data; verify paths and backups.

The status is **Preview only - nothing submitted**. The Manager was closed
through its window close gadget, after which the runner verified export,
read-only reimport, exact file access, and the full storage PASS marker.
This closes the previously missing add-specific visual-warning check; it
does not claim a new GUI-submitted destructive operation or hardware test.
Successful generated media were hashed and pruned. No DUT/user-hosted access.
