# CLI recovery with acknowledged writes and cold readback

All runs use the selected shared engine `cmPmmLLk`, CLI `mmake-cli.f2fvfhdP`,
private runtime prerequisites and eight-CPU isolated QEMU/xHCI. No DUT, user
hosted session or physical storage device is involved. See `replay-inputs.sh`
and each run's hashes for exact inputs.

1. `AkME5QdT`: existing in-flight mode passed interruption/error handling,
   suspended-unmount refusal, actual CLI clear, ONLINE and readonly reimport
   with the earlier committed sentinel. This did not verify acknowledged
   chunks written during the fault loop, so stronger coverage was added.
2. `ejVLtkqL`: the strengthened test counts a chunk as committed only after
   Flush succeeds. After 57 committed 64-KiB chunks, Write returned -1 with
   DOS error 225 and Close failed. Both members had been unplugged. Reinsert,
   CLI clear and ONLINE passed, followed by readonly reimport and exact
   comparison of all 3,735,552 acknowledged bytes plus the sentinel.
3. `PAyGO7cw`: a separate QEMU process imported the completed writer's two
   qcow2 members through fresh overlays. Two readonly import cycles verified
   the same 57 chunks and sentinel. Both writer members, original backing
   files and reader overlays remained hash-identical. Thus the readback does
   not rely on the writer guest's caches or process memory.

Replay writer with `AROS_TEST_USB_INFLIGHT_WRITE=1`. For the cold reader set
`AROS_TEST_USB_COLD_SOURCE` to the completed writer directory,
`AROS_TEST_USB_COLD_KIND=acknowledged`, and USB_REMOVE, USB_REINSERT,
USB_ALL_LOST, USB_ALL_LOST_WRITABLE and USB_CLI_CLEAR all to 0. The runner
extracts a bounded positive chunk count from the writer's PASS record and
requires two matching cold-reader results plus unchanged hashes. Existing
single-survivor cold mode remains the default and retains its separate checks.

The test deliberately makes no guarantee for the unacknowledged tail. This
is software/virtual-device qualification, not physical flush-cache, cable
disconnect or abrupt power-loss durability. It does not implement rewind.
Three generated test ISOs were hashed and removed after the processes exited;
logs, recipes, inputs and small overlay chains remain for reproduction.
