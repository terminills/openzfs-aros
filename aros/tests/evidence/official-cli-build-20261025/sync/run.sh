#!/usr/bin/env bash
set -euo pipefail
cd /mnt/short-term/openzfs-official-sdk-audit.xi9RkF3r/publication
out=/mnt/short-term/openzfs-official-sync.U6UNoJ2L
sdk=/run/media/terminills/Codex/show-consolidation-01a16962/sdk-prerequisite-audit/nightly-20261023/iso-Developer
cc=/home/terminills/Documents/GitHub/aros-adt-crosstools/x86_64-aros-gcc
"$cc" --sysroot="$sdk" -B"$sdk/lib/" -Ilib/libspl/include -Ilib/libspl/include/os/linux -Iinclude -include aros/engine/config.h -D_GNU_SOURCE -O2 -Wall -Wextra -Werror aros/cli/sync-native-test.c -o "$out/OpenZFSSyncTest"
sha256sum aros/cli/sync-native-test.c lib/libspl/include/unistd.h "$out/OpenZFSSyncTest" > "$out/inputs.sha256"
xorriso -indev /run/media/terminills/Codex/show-consolidation-01a16962/sdk-prerequisite-audit/nightly-20261023/pristine.iso -outdev "$out/test.iso" -map "$out/OpenZFSSyncTest" /C/OpenZFSSyncTest -map "$out/Startup-Sequence" /S/Startup-Sequence -map /mnt/short-term/openzfs-official-sdk-audit.xi9RkF3r/grub.cfg /boot/grub/grub.cfg -boot_image any discard -boot_image any system_area=/dev/zero -boot_image grub bin_path=/boot/grub/i386-pc/eltorito.img -boot_image grub boot_info_table=on -boot_image grub grub2_boot_info=on > "$out/iso-build.log" 2>&1
exec qemu-system-x86_64 -enable-kvm -cpu host -smp 8 -m 2048 -cdrom "$out/test.iso" -boot d -nic none -display none -serial file:"$out/serial.log" -qmp unix:"$out/qmp.sock",server=on,wait=off -no-reboot -no-shutdown
