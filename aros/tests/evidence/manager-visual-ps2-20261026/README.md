# Native Manager visual queries and post-close storage verification

## Scope and result

Actual native Manager and service, private 8-CPU QEMU, RAM-file mirror,
no network, physical disk, DUT or user-hosted session. The image from
`build-audit/native-gui-run.aKyWdL8v/test.iso` was reused read-only, not rebuilt.
Input binary hashes are retained in inputs.sha256. This uses the private
runtime pair, not a qualified pristine official SDK/runtime combination.

The earlier tablet runs reached Manager but never visibly completed a pool
query. This run omits the USB controller/tablet and uses QEMU's PS/2 mouse.
It establishes a working input configuration, not the root cause of the USB
behavior. Relative movement followed by a settling delay and a separate click
was used. One close attempt moved the title bar slightly; a subsequent click
on the observed close gadget closed Manager normally. All actions are retained.

Observed screenshots:

- pool-results.png: one imported public_mirror, ONLINE; 112.0 MiB raw size,
  2.1 MiB allocated and 109.8 MiB free.
- dataset-results.png: empty Root dataset rejected with Target is required.
- datasets-populated.png: public_mirror returns five datasets, including the
  bootenv volume, files, files/child, keep-pending and root. files is mounted
  at ZPUBLIC:, with measured used/available values.
- pool-health.png: pool/mirror/two-member topology returned with ONLINE states
  and zero reported read/write/checksum errors. This is not physical health proof.

After normal Manager close, serial.log records export and read-only reimport,
bootenv readonly preservation, accounting readonly gates, mount controls,
mount-snapshot lifecycle, exact DOS access checks and storage PASS (line=0).
Three earlier service lifecycle PASS markers are also present. No FAIL,
page-fault diagnostic or ASSERT marker was found. The already completed guest
was then stopped using its private QMP quit command; the launch process exited 0.

The public-service runner now offers AROS_TEST_POINTER=ps2 while retaining
tablet as its default and recording the selection. An invalid selection fails
before snapshot or image allocation. Shell syntax and negative admission pass;
the visual replay directly ran the equivalent PS/2 QEMU arguments against the
existing image, rather than claiming a second full runner rebuild.

Remaining: complete feature/topology qualification, official CRT prerequisites,
and other previously documented port gaps. These captures do not qualify all
Manager mutations or replace cold persistent multi-disk tests.
