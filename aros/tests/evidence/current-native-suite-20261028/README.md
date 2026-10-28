# Current native combined suite: failure localization

`public-service-run.TWxWdCyM` failed before boot. The new actual-ISO runtime
check found a retained source GPT/APM disk layout whose endpoint exceeded
the smaller rebuilt image. xorriso extracted POSIX but exited 32 with SORRY.
The public-service test generator now clears the inherited system area and
creates its explicit GRUB El Torito entry. This private QEMU CD is not a USB
or UEFI installer qualification.

`public-service-run.z7H7Yy2O` subsequently extracted all five selected runtime
libraries without the disk-layout error and booted the guest successfully.
It failed the persistent configuration test's DOS-only rename collision probe:
Rename returned success but the destination could not be opened (DOS 205).
The noncolliding control succeeded. The recipe had omitted the known fixed
AFS handler, so the stock ISO handler was used for OZARCH. No
`afs-handler.sha256` was present in this run. This matches the existing
AFS pinned-link issue; the test was not weakened.

The selected follow-up handler is `afs-source.wg6ei6nj/afs-handler`.
The source-snapshot verifier accepts its source/patch/build/binary bindings.
The private run recipe now explicitly selects it, in addition to the current
shared engine, client, CLI/Manager and four CRT dependencies. A passing
follow-up native run is still required. These failures do not qualify a bundle.

Both failed images are eligible for verified zstd compaction after process
exit; all logs and mutable test pool/archive files remain retained.
