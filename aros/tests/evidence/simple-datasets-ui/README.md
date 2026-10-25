# Simple dataset summaries: native GUI verification

Verified on 2026-10-25 with Manager changes `210856b` and `32fcb20`.
The isolated eight-CPU QEMU run used production shared engine
`shared-engine.4OvHDEDZ`; it did not access the DUT or user hosted session.

Evidence directory on the external test volume:
`/mnt/short-term/openzfs-qualification-20261024.aGFlVF7T/native-gui-run.TmLAHPNP`.

The screenshot records a real `List filesystems` response for `public_mirror`:
seven rows with dataset type, state and formatted available capacity. The mounted
`public_mirror/files` row reports `OZPUBLIC:`. Volumes and unmounted filesystems
remain distinguishable. No synthetic used-space value is displayed.

The Manager was closed normally through its close gadget. The runner then
completed its remaining service/export/reimport checks and exited zero, ending
with `PASS: public service lifecycles and mirror DOS round trip (persistent=0)`.
The GUI actions, input hashes, runner and guest result logs are retained here.
The host formatter test also passed with address/undefined-behavior sanitizers.

This verifies the Simple dataset presentation and this service regression run;
it does not qualify all Manager operations, physical hardware, power loss, or
the entire OpenZFS port. The production engine build used incremental upstream
objects. Full raw responses remain available through Save response and Advanced
mode; those paths were not interactively re-tested in this run.
