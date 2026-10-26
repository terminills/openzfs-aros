# Mounted legacy accounting conversion: failing reproduction

Private two-vCPU diskless QEMU run `default-quota-run.seujZQnr` exits 1.
The fixture creates a pool at SPA_VERSION_USERSPACE-1 on exclusive guest RAM
mirror files, creates a filesystem and writes a payload, verifies accounting
is disabled and incomplete, upgrades the pool while keeping the filesystem
mounted, then invokes the upstream USERSPACE_UPGRADE dispatcher.

That ioctl returns EINVAL at test line 549. The accounting conversion and
subsequent data-preservation checks therefore have NOT passed. Earlier
current-format and delegation tests do not exercise this old object-set
expansion path. The exact source delta, inputs and serial output are retained.

The test was freshly linked against the source-built engine objects from
`build-audit/source-engine.sa4fWOFY` and rebuilt AROS adapters. It uses the
private alpha18c5 runtime with the recorded DOS/posixc pair. The BSP package
was extracted unmodified from that base ISO. It is not official-runtime or
physical-hardware qualification. No physical disks or user sessions are used.

Investigation must distinguish dispatcher rejection, suspend failure and
resume failure. In particular, upstream refreshes ownership between suspend
and resume; the AROS resume guard currently compares the dataset pointer
against its pre-refresh pointer. This is a hypothesis, not yet the proven
cause. Keep ownership validation intact while investigating.

The quota runner now validates storage before allocation and honors
AROS_TEST_ARTIFACT_ROOT, allowing these iterations to remain on the external
artifact drive. The failed ISO is retained for diagnosis, not silently reused.
