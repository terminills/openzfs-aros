# OpenZFS for AROS

An experimental port of the upstream OpenZFS engine to AROS, with a native DOS
filesystem handler, management application, and the upstream `zfs` and `zpool`
command-line tools. This repository is a community development source snapshot.

**Use disposable test storage. Do not entrust unique or production data to this
port.** Passing hosted and QEMU tests does not establish physical power-loss
safety or compatibility with every AROS build.

## Components

- `aros/library`: shared `openzfs.library` engine and service launcher.
- `aros/handler`: native DOS filesystem access for applications and Wanderer.
- `aros/ui`: native management UI, including a simplified view and pool wizard.
- `aros/cli`: upstream `zfs` and `zpool` integration.
- `aros/device`, `aros/zpl`, `aros/platform`: device and operating-system adapters.
- `aros/distribution`: staging and packaging checks.
- `aros/patches`: companion platform changes and integration notes.

Start with [build requirements](BUILDING.md), [status and limitations](PORT-STATUS.md),
and [contributing](CONTRIBUTING.md). Detailed implementation and test references
are under [aros](aros/README.md).

See [native AROS screenshots and test evidence](aros/tests/evidence/README.md)
for Manager operations and live pool-directory updates in Wanderer.

This is not the older AROS private ZFS filesystem format. Existing volumes using
that handler must not be imported or converted in place. Migration requires a
verified file copy to a separate pool.

## Upstream and licensing

Based on OpenZFS 2.4.4. See [source provenance](UPSTREAM.md) and the
[upstream overview](README.upstream.md). This is an independent AROS port, not an
official OpenZFS release. Upstream copyright, authorship, and license notices
are retained in LICENSE, COPYRIGHT, AUTHORS, NOTICE, and individual source files.
