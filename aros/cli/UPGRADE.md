# Native pool upgrade

The transport admits real upstream `ZFS_IOC_POOL_UPGRADE` using only the
inline pool name and version cookie. Source/config/output/history buffers,
unrelated fields and client pointers are rejected. The upstream dispatcher
enforces config privilege and pool state; `spa_upgrade` performs the format
transition and refuses unsupported versions or downgrades. Feature enabling
uses the already-admitted upstream pool-property ioctl.

Manager operation 25 executes exact upstream `C:zpool upgrade` arguments
in its inherited-identity worker. Empty options request upstream's normal
upgrade and compatible feature enabling. `version=1..28` or `version=5000`
selects a supported format explicitly. Confirmation is required; extra
arguments, force, dry-run, unrelated options and invalid versions are refused.
The help warns that upgrades are irreversible and can remove older software's
access. This is pool format/feature upgrade, not filesystem ZPL version upgrade.

The regression creates a separate blank 128-MiB RAM-file pool at version 26,
writes a DOS file, verifies MuFS non-root upgrade denial leaves version 26,
then upgrades to 28 and feature flags. It checks lz4 feature enablement,
exports/reimports through the explicit backing path, and reads the original
file exactly before destroying that disposable pool and deleting its backing.
It also checks malformed/unconfirmed Manager requests. The main mirror's
feature state is not changed by this test.

Build: `source-engine.P2gN7Bsv/upgrade-service`, log
`engine-upgrade-build-1.log`, existing upstream CLI `cli-link.PTQ5X1YB`.
Test execution results are recorded below; a fixture or successful link
alone is not an upgrade pass. Private QEMU overlays only; no
stock-nightly, physical durability, cold-boot or visual GUI claim.

Initial run `OmBvmugC` passed the base guest regression but did not execute
the upgrade function because it was behind the optional pool-create switch.
The mandatory host marker gate rejected the run (exit 1); no upgrade pass
was claimed. The fixture now runs after the active ZVOL regression, whose
MuFS account setup it uses. Failed routing evidence is retained under the development test archive.

Run `COWjl8Kd` executed the permission refusal and 26-to-28-to-feature-flags
transitions successfully, then failed an overly strict fixture check:
`feature@lz4_compress` was `active`, not `enabled`. Both are enabled states;
active means in use. The corrected check accepts exactly either output,
not disabled/unavailable values. Export/reimport was not reached in this run.

Run `H37GSX3P` passed the format/feature transition and export, but reimport
returned "internal error: out of memory". The first import omitted the
explicit `cachefile=none` used by other isolated file-backed import tests;
the next run corrects that test-isolation setting. This is not yet an
established explanation for the ENOMEM message or a persistence pass.

Run `S9VvEvre` (`manager-upgrade-native-4.log`) passes the complete upgrade
fixture, full native suite and unchanged original backing hashes, host exit 0.
The isolated importer explicitly requires `cachefile=none`; with it supplied,
export/reimport and original DOS bytes pass. Source review found that the
earlier rejected import reached an unconditional libzfs output-nvlist unpack
despite no filled reply; that misleading diagnostic is separate follow-up
work. Evidence for this successful run is registered under the development test archive.
