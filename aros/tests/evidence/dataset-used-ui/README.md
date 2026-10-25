# Dataset used-space reporting

`6846d2a` fills dataset-list `used` with upstream `dsl_get_used()` while holding
the dataset through `dmu_objset_hold()`. Referenced bytes remain a separate
detail field. This preserves ZFS accounting semantics, including descendants
and reservations; it is not a sum of visible file lengths.

The native Simple view shows Used and Available for filesystems and volumes.
It requires the service's explicit `used=` detail prefix before displaying the
used field, because older services left that field zero. An older reply must
not be presented as measured zero usage. Snapshot/unknown-kind formatting
continues to omit filesystem-capacity presentation.

Host formatter checks pass with AddressSanitizer and UndefinedBehaviorSanitizer,
including measured zero, older-service fallback, maximum values and short
output buffers. The production shared engine and native Manager both build.

Eight-CPU private QEMU `native-gui-run.yNcq4Bpa` displays seven live dataset
rows, including 288 KiB used for the mounted files dataset and 2.5 MiB for its
parent in this fixture. The screenshot and input hashes are retained in `gui/`.
Normal Manager close is followed by exact post-UI readback and final storage
PASS. Separate eight-CPU `public-service-run.ewELUCt4` passes the production
service and native cold read-only regression; evidence is in `production/`.

Artifacts remain under `/mnt/short-term/openzfs-qualification-20261024.aGFlVF7T`.
The engine is `shared-engine.t7YsOQpw` from `sibling-production.Lk2PCe50`;
Manager is `dataset-used.2M4GuMQG/OpenZFSManagerNative`. These are incremental
AROS builds with qualified runtime dependencies, not a fresh SDK or physical
device qualification. No DUT or user hosted session was used. The earlier
development archive does not contain this later UI/engine change.
