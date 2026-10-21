# Native GPT manager verification

2026-09-25, the development test archive. All runs below use private QEMU overlays. No DUT,
physical disk, user hosted session, or shared OS installation was changed.

The devices window now reads geometry independently of partition discovery,
so a blank disk can supply geometry even if its partition table is absent.
Replace GPT and Expand GPT call the authenticated native service through a
pthread worker. This is the actual upstream libefi-backed operation, not a
preview or simulated UI result. Inspection is not an ownership lease.

Before submission the dialog names the inspected device/unit and geometry,
warns about external raw users and possible partial writes, and requires an
explicit confirmation. Editing the device fields requires another inspection.
The manager disables repeat submission while pending, drains before exit,
invalidates the old catalog after submission, and never retries a write.
Replacing GPT does not create a pool; expanding GPT does not online-expand
an imported pool. The existing service authorization/claim guards apply.

Evidence under `build-aros/`:

- `native-gpt-ui-build.log`: native manager builds without warnings;
  executable/input hashes in `native-gpt-ui/OpenZFSManagerNative.inputs.sha256`.
- `native-import-run.VhH7sqqN`: exit 0. The exact manager GPT worker exercised
  unconfirmed EINVAL, non-root EPERM, retained-pool EBUSY, stale geometry
  ESTALE, actual label/write/readback, and already-full expansion. It also
  checks copied request ownership, unchanged output on errors, and refusal to
  free before poll/join. Two real mirror import/DOS-read/export cycles and
  actual libzfs GPT entrypoint/config-derived start tests pass. Host sgdisk
  validates the final table. Original images and untargeted overlay unchanged.
- `native-import-run.LeHmqkFh`: exit 0. The same worker receives EROFS for both
  GPT actions against a read-only service; both overlays and originals unchanged.
- `native-import-run.YazwaRJV`: exit 0. Interactive manager launches, Devices
  opens with write buttons disabled, ata.device unit 0 inspection populates
  its real GPT partition and enables the buttons. Replace GPT opens the
  destructive confirmation with 536870912 bytes and 512-byte sectors. Cancel,
  close devices, and close manager complete normally. Both overlays and both
  originals unchanged. `manager.png`, `panel.png`, `inspected.png`, and
  `confirm.png` are full guest-screen captures from that private QEMU monitor.

Follow-up `native-import-run.Yc70ttG8`: exit 0, interactive Apply/result/refresh
verified using the same manager binary. `AROS_TEST_GUI_GPT=1` explicitly permits
only overlay 0 changes and requires the final host GPT check to find the new
AROS OpenZFS data partition starting at sector 2048. Replacement confirmed,
the UI reported a verified change, and Inspect displayed both new partitions.
Expand then confirmed the already-full no-change result. Changing the unit
field to 1 without another Inspect rejected Replace before confirmation.
Screenshots: `confirm.png`, `applied.png`, `refreshed.png`,
`expand-confirm.png`, `expanded.png`, `selection-changed.png`. Normal UI close
completed; host sgdisk verified the table and both original images plus overlay
1 remained unchanged. The no-change note clipped at this window width; the
subsequent source change shortens it without changing the operation.

Coverage boundaries: blank-disk geometry presentation and close-during-pending
still need dedicated UI replay. Full native writable pool admission and full
CLI network share backend support are not established by these tests.
