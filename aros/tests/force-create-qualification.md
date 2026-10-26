# Force-create qualification in progress

The private QEMU baseline in `build-audit/public-service-run.KL3OjoXH`
reached `Public force-create prerequisites PASS`, preserving the source
payload across active and no-force refusals. It then failed at
`exported-force-create`, as expected with the previous service and CLI.
This is failure reproduction, not a passing acceptance test.

The current implementation transports an AROS-only boolean create intent
from zpool to the service, strips it before upstream creation, and requires
captured privileged credentials. Exported members must have four coherent,
checksummed labels. The live SPA namespace is checked even for exported
labels, since a read-only import can retain those labels.

The service cross-build passed using retained upstream engine objects and
rebuilt adapters; see `build-audit/force-create/service-build.log`.
The updated runtime test additionally checks read-only imported source
refusal and preservation. A rebuilt CLI and a passing QEMU replay remain
required. This does not qualify arbitrary nonzero-media overwrite, Manager
force semantics, or force add/attach/replace.
