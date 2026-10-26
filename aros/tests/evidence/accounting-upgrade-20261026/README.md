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

## Subsequent authority and mounted-path qualification

Eight-vCPU run `public-service-run.QSaUFCEW` used the same fresh-source service
and a rebuilt client. It additionally verifies changed-user privileged-ticket
reuse fails with EACCES, the reader's own session cannot upgrade accounting,
and a mounted current-format filesystem accepts repeated upgrades. The runner
requires the authority, mounted and readonly markers when
`AROS_TEST_REQUIRE_ACCOUNTING_UPGRADE=1`; all were present and the full run
passed. Root identity is restored before subsequent storage operations.

Logs, input hashes, test source delta and replay script are retained in
`authority-mounted/`. The completed disposable ISO was hash-recorded and
pruned (4.56 GiB). This closes the tested mounted and privilege-denial cases,
not legacy-format conversion, delegated permission success, hardware durability
or official-nightly compatibility. The private runtime boundary above remains.

## Delegated accounting upgrade

Eight-vCPU run `public-service-run.zhX6rDR9` passed with a rebuilt client and
the same recorded fresh-source service. The actual authenticated SET_FSACL
transport grants only the upstream `version` permission to local user 77.
The reader's fresh session is denied before grant, succeeds after grant,
and is denied again after revocation. Reusing the privileged ticket as the
reader remains denied throughout. Test cleanup restores root and revokes an
outstanding grant if an assertion fails. The runner requires the delegation
marker alongside the prior authority, mounted and readonly markers.

The full RAM-backed mirror/DOS suite also passed; the runner exited zero and
pruned the disposable ISO after recording its hash. Evidence is in
`delegation/`. This verifies delegated authorization on current-format
datasets, not conversion of legacy accounting metadata.

Full build logs and objects remain under
`/mnt/short-term/openzfs-bootenv-candidate-20261025/build-audit/source-engine.sa4fWOFY`.
