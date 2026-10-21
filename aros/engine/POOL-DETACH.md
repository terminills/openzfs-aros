# Authenticated native device detach

Operation 12 now calls upstream spa_vdev_detach(spa, guid, 0, B_FALSE).
Target is the imported pool name; argument is the exact decimal leaf GUID
from a fresh pool.status query, not a path or a guessed member index.
Root/config privilege and explicit confirmation are required. Force, dry-run,
recursive and options are refused. Read-only and suspended pools are refused;
malformed, zero and overflowing GUIDs fail before mutation. Non-leaf devices
are refused. The legacy unauthenticated route cannot detach devices.

Upstream retains topology, DTL/only-copy, checkpoint and resilver safety checks.
Detach is not generic remove: mirror/replacing/spare parents are supported by
upstream, whereas a stripe or RAIDZ child cannot be detached this way. No
forced detach or automatic topology reinterpretation is provided. Native UI
uses the authenticated worker and warns of reduced redundancy before submit.
Capability 12 is advertised only by a write-capable initialized service.

Always query status after a successful detach. When a two-way top-level mirror
collapses, vdev_remove_parent transfers its GUID to the remaining leaf; the
survivor's old GUID must not be reused for subsequent commands.

## Native evidence and failed hypotheses

- build-aros/admission-service-run.HiRvjLxA: detach succeeded, then the test
  expected ENOTSUP using the survivor's stale GUID and got ENODEV. Upstream
  vdev_remove_parent confirmed GUID transfer. Test now reads fresh status.
- build-aros/admission-service-run.rzlNF1Hn: test-only placement error checked
  EROFS before the read-only import. Moved that assertion after import.
- build-aros/admission-service-run.syNP8AXU: five RAM-file layouts pass. Stripe
  and RAIDZ1/2/3 refuse detach. Mirror refuses removing the only available copy
  while its peer is offline; after online, detach succeeds, old GUID is absent,
  status has one remaining file, and detaching that last top-level leaf is
  refused. Native DOS reads of proof.txt still match exactly after detach.
  Nonroot, read-only, invalid/missing GUID, legacy route, force/dry-run/recursive
  and options refusal checks pass along with existing lifecycle regressions.
- build-aros/native-gui-run.DG3WvmgZ: live status (02), explicit leaf GUID and
  reduced-redundancy confirmation (03), completed authenticated detach errno=0
  (05), fresh status with one ONLINE disk and no mirror parent (06). The test
  uses only private copies of the baseline QEMU disks. No UI-only mock result.

Build logs: detach-engine-build.log, detach-client-v3-build.log and
detach-manager-build.log in build-aros. Failed runs and build logs retained.

- build-aros/file-backing-run.a6HAXsgj: two separate guest boots, file mirror
  on private FAT backing. Creation writes and flushes hello.txt, detaches a
  leaf and checks the collapsed ONLINE topology before export. Fresh service
  discovers from on-disk labels and imports read-only, confirms one ONLINE
  file/no mirror parent, reads the exact payload and enumerates it via DOS.
  Both member files are byte-identical through read-only boot. Independent
  zdb -bcc reports no leaks. Launcher exit 0; detach-cold-run.log and
  detach-cold-client-build.log retained. This tests orderly export/import,
  not sudden physical power loss. AROS_TEST_COLD_DETACH=1 selects this suite.

Remaining evidence gaps: mirror/replacing/spare
resilver interactions, concurrent topology changes, checkpoint conflict runtime
test, and physical device behavior. RAM-file success is not persistence proof.
No DUT or user-hosted session used. Full OpenZFS port remains incomplete.
