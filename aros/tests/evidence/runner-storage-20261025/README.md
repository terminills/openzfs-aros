# QEMU runner snapshot placement

The public-service runner previously copied its script to checkout/build-aros
before consulting AROS_TEST_ARTIFACT_ROOT. It now validates the selected
filesystem and free-space reserve before allocating the snapshot. The later
run allocation also checks the existing checkout rather than requiring a
preexisting build-aros directory for external-only operation.

Checks performed without launching QEMU:

- External artifact directory: snapshot was created there, then the runner
  refused the deliberately absent AROS_TEST_ISO. This confirms early snapshot
  placement, not a guest pass.
- System-filesystem destination: refused before snapshot allocation.
- /tmp, a separate filesystem on this host: refused for less than 100 GiB
  available. It was not a valid system-filesystem rejection fixture.
- bash syntax and git diff whitespace checks passed.

Logs retained alongside this report. Working artifacts:
`/mnt/short-term/openzfs-runner-storage.9N9lHUVG`.
