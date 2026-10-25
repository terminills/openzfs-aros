#!/usr/bin/env bash
set -euo pipefail
repo=/mnt/short-term/openzfs-official-sdk-audit.xi9RkF3r/publication
out=/mnt/short-term/openzfs-official-settings.CRed8uWI/hostid-retest
cli=/mnt/short-term/openzfs-official-deps.fVjvrmGW/cli-link.2TWxBYlW/OpenZFSHostIdentity
sha256sum "$out/HostIdentityTest" "$cli" "$repo/aros/ui/hostid-native-test.c" "$repo/aros/ui/hostid-settings.c" "$repo/aros/engine/hostid-record.c" "$repo/aros/ui/preferences.c" "$repo/aros/ui/preferences-file.c" "$repo/aros/engine/security-fixture/"* > "$out/inputs.sha256"
xorriso -indev /run/media/terminills/Codex/show-consolidation-01a16962/sdk-prerequisite-audit/nightly-20261023/pristine.iso -outdev "$out/test.iso" -map "$out/HostIdentityTest" /C/HostIdentityTest -map "$cli" /C/OpenZFSHostIdentity -map "$repo/aros/engine/security-fixture" /Security -map "$out/Startup-Sequence" /S/Startup-Sequence -map /mnt/short-term/openzfs-official-sdk-audit.xi9RkF3r/grub.cfg /boot/grub/grub.cfg -boot_image any discard -boot_image any system_area=/dev/zero -boot_image grub bin_path=/boot/grub/i386-pc/eltorito.img -boot_image grub boot_info_table=on -boot_image grub grub2_boot_info=on > "$out/iso-build.log" 2>&1
exec qemu-system-x86_64 -enable-kvm -cpu host -smp 8 -m 2048 -cdrom "$out/test.iso" -boot d -nic none -display none -serial file:"$out/serial.log" -qmp unix:"$out/qmp.sock",server=on,wait=off -no-reboot -no-shutdown
