# Native explicit online expansion

The native CLI now admits `ZFS_ONLINE_EXPAND` for `zpool online -e`, matching
the Manager's existing `expand=on` request. Both use upstream `vdev_online`;
the CLI retains upstream pool-modify authorization and readonly checks.
Only the expansion bit is added to the online whitelist. Spare activation,
check-remove and force-fault flags remain unsupported.

Existing native device bindings still limit the accessible extent. This is
not permission to grow a partition, adopt another device or enlarge a binding
past its admitted capacity. Dedicated file vdevs can report their enlarged
length when upstream reopens them.

## QEMU evidence, 2026-10-03

Unchanged bundle `native-bundle.i8xXqim2` in `native-import-run.g6ywuB25`
passed Manager expansion, then failed the new CLI `online -e` check with
`operation not supported on this type of pool`. This preserves the negative
control rather than inferring the rejection solely from the source whitelist.

Corrected bundle `native-bundle.6Z2tUHi1` uses library
`shared-engine.bc1xdBz3` and service/Manager from
`cli-online-release-20261003`, with the existing all-fresh CLI, handler and
ZVOL package inputs. Run `native-import-run.Ugb2KWD6` exited 0. The fixture
extends only its own RAM-backed vdev file, first through the Manager and then
through the CLI. Reported size increased from 117440512 to 251658240 and then
385875968 bytes. Each step retained the exact 1048576-byte payload and the
reported capacity across export/reimport. Separate CLI checks refused
expansion on a readonly pool and by an unauthorized user.

The full selected CLI/native-handler regression passed, including checkpoint
rewind, and both original native backing-image SHA256 checks were unchanged.
Logs live on the external OpenZFS archive:
`cli-online-{before,build,library,library-inputs,stage,after}-20261003.log`.
This is same-guest file-vdev evidence, not physical device/partition resizing,
power-loss qualification, every topology, or stock-nightly compatibility.
