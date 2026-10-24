# Reuse-capable native package and Manager qualification

Local archive: `/mnt/short-term/openzfs-qualification-20261024.aGFlVF7T/mmake-bundle.exm_ebrp.tar.xz`

SHA256: `d08cea719373fb93ebeedcb3917038bcec698ca1c5247f99cb54bb03687637a8`

This archive selects `shared-engine.aQP3e9lF`, including native/file destroyed
member reuse and the ARC authentication repair, plus the rebuilt native Manager.
The Manager's create help and wizard now distinguish blank/destroyed members
from exported pools. The package gate checked exact production binary hashes
against eight-CPU GUI, cold-start and native encrypted-root runs.

- `native-gui-run.vGnoN9wd`: mounted OZPUBLIC drawer visible in Wanderer,
  live ONLINE mirror query, readable revised wizard guidance. A draft named
  preview was cancelled without submitting any storage mutation. Manager closed
  with Wanderer left running; post-UI exact file read and full storage PASS.
- `public-service-run.0vDGRGlA`: three persistent startup restores, healthy,
  degraded and restored mirror imports, unchanged member hashes, zdb no leaks.
- `public-service-run.8gndo0qu`: native encrypted-root, wrong-key, destroyed
  reuse and damaged/conflicting label regressions, full storage PASS.
- `public-service-run.nV3OO7vZ`: freshly extracted production binaries pass
  the full upstream storage suite; acceptance client is supplied separately.

All extracted SHA256SUMS entries passed. Seven package-gate host tests passed.
Runtime overlays remain separate explicit inputs. This is not stock-nightly
or hardware durability qualification, and the archive has not been uploaded
as a release. Earlier local archives remain historical, not this revision.
