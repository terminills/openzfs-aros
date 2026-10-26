# Native accounting-upgrade transport

The ioctl transport previously rejected upstream ZFS_IOC_USERSPACE_UPGRADE
as unsupported. It now admits a dataset-name-only legacy command, rejecting
source/config/output payloads, history, unexpected scalars and auxiliary
strings. The existing authenticated descriptor and credential scopes remain;
upstream version-property permission, suspended and read-only pool checks
are not replaced or bypassed.

Fresh-source build `source-engine.sa4fWOFY` compiled all 248 upstream units,
then AROS adapters, service and Manager against the explicit private SDK and
pthread/dependency selection. No previous engine object was seeded into it.
The client was rebuilt after aligning the absent-dataset readonly expectation
with upstream: pool readonly checks precede dataset existence checks.

Fresh eight-vCPU QEMU run `public-service-run.AJQQc6Mt` passed:

- Current-format, unmounted filesystem: command succeeds twice.
- Unexpected cookie rejected with EINVAL.
- Missing dataset on writable pool rejected with ENOENT.
- Readonly pool rejects both existing and missing dataset commands with EROFS.
- Existing bootenv and authenticated mirror/DOS lifecycle checks also pass.

Runner exited 0. The disposable ISO was hash-recorded and pruned. Runtime was
the private alpha18c5 + recorded DOS/posixc pair, not the official nightly.
This does not prove legacy on-disk accounting conversion, mounted upgrade,
delegated authorization, or official-SDK runtime compatibility. Those remain
separate qualification work; current-format success is largely idempotence.

Full build logs and objects remain under
`/mnt/short-term/openzfs-bootenv-candidate-20261025/build-audit/source-engine.sa4fWOFY`.
