# Damaged-pool readonly extreme rewind

Run `native-import-run.7HxY3FbK` passes in an isolated eight-CPU AROS QEMU
guest with the production shared engine `IiSPGMPM` and actual upstream
MetaMake CLI `EksGNipO`. No diagnostic engine objects are linked.

Fixture preparation uses copies of the retained 512-MiB `qemu_mirror`
MBR-partition images (`qemu-run.cli-clear.w9j57Aci`). Partition 0 begins
at sector 4097 and spans 524288 512-byte sectors. Host `zdb -l -u` reports
the newest uberblock at TXG 536, with three uncompressed 4096-byte MOS-root
copies at DVA offsets 0x95c000, 0x11cc000 and 0x2ad1000 (vdev 0).

The existing `corrupt-test-member.py` helper flips one byte at each physical
offset (DVA + 4-MiB label/boot area), in both extracted mirror members.
Every helper invocation checks the original block SHA256 and exclusively
creates a new regular image; source media are never modified. The final
members are inserted into new copies of the full disk fixture. Per-flip
JSON records and original hashes are retained here. Full scratch preparation
is external at `build-audit/rewind-fixture.fKM9vlXB`.

Independent host discriminator: latest-state `zdb -e -bcc` fails EIO, while
`zdb -e -t 520 -bcc` passes with no leaks. This verifies corruption of the
newest root while an older complete state survives. The native test does
not claim that SPA necessarily selects exactly TXG 520.

AROS performs two cycles in one guest:

1. Ordinary readonly import fails and `zpool list` confirms absence.
2. Real `zpool import -N -f -FX -o cachefile=none -o readonly=on` succeeds,
   using only the two explicitly supplied native partition paths.
3. The native DOS handler reads and compares the exact `hello.txt` payload;
   a write attempt is refused with disk-write-protected. Unmount/export pass.

Both backing images and both writable-capable QEMU overlays remain byte
identical after the guest exits. Thus readonly recovery did not silently
commit. This is real damaged-pool import recovery, not healthy CLEAR
acceptance, checkpoint rewind, or physical durability. Writable recovery,
separate-boot persistence and the Manager recovery UI are not established
by this run. Private SDK/runtime prerequisites still apply.

Replay: use `AROS_TEST_IMPORT_REWIND=1`, `AROS_TEST_CLI_DIR` containing the
actual packaged zpool, and the two corrupted full-disk images as the native
fixture. Keep the normal native runner's explicit compiler/SDK, production
service/library, handler and runtime inputs. The runner requires the two
recovery cycles, exact data and unchanged image hashes; it does not accept
the ordinary native-import completion message as a substitute.
