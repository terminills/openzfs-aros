#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
source "$repo/aros/engine/storage-preflight.sh"
artifact_root=$(oz_artifact_root "$repo/build-aros" "${AROS_ARTIFACT_ROOT:-}")
base=${AROS_TEST_ISO:-/opt/aros-workspace/aros-amiwest-nightly-release/distfiles/aros-smp-pc-x86_64.iso}
platform_maps=() platform_inputs=()
if [[ ${AROS_TEST_BASE_PLATFORM:-0} == 1 ]];then
    [[ -z ${AROS_TEST_DOS_OVERLAY:-}${AROS_TEST_ATA_OVERLAY:-} ]]
    if [[ -n ${AROS_TEST_BASE_DOS_PACKAGE:-} ]];then
        python3 aros/engine/verify-dos-package-overlay.py "$base" \
            "$AROS_TEST_BASE_DOS_PACKAGE" "${AROS_PACKAGE_TOOL:?package tool required}"
        platform_maps=(-map "$AROS_TEST_BASE_DOS_PACKAGE" /boot/aros-base.pkg.xz)
        platform_inputs=("$AROS_TEST_BASE_DOS_PACKAGE")
    fi
else
    dos=${AROS_TEST_DOS_OVERLAY:?private DOS overlay required}
    ata=${AROS_TEST_ATA_OVERLAY:?private ATA overlay required}
    platform_maps=(-map "$dos/aros-base.pkg.xz" /boot/aros-base.pkg.xz
        -map "$ata/aros-bsp.pkg.xz" /boot/pc-smp/aros-bsp.pkg.xz)
    platform_inputs=("$dos/aros-base.pkg.xz" "$ata/aros-bsp.pkg.xz")
fi
posix=${AROS_TEST_POSIX_LIBRARY:?private POSIX library required}
qemu=$(command -v "${AROS_TEST_QEMU:-qemu-system-x86_64}")
gui=${AROS_PREFERENCES_GUI:-0}
prefix=preferences-run
startup=aros/ui/Startup-Preferences-Test
marker='RESULT native IFF preferences PASS'
timeout=180
extra_maps=()
if [[ $gui == 1 || $gui == failures || $gui == startup || $gui == simple ]];then
    prefix=native-gui-run
    startup=aros/ui/Startup-Preferences-GUI
    marker='RESULT native preferences GUI PASS'
    timeout=1800
fi
if [[ $gui == failures ]];then startup=aros/ui/Startup-Preferences-Failures;fi
if [[ $gui == startup ]];then startup=aros/ui/Startup-Pool-Preferences-GUI;fi
if [[ $gui == simple ]];then startup=aros/ui/Startup-Preferences-Simple;fi
run=$(mktemp -d "$artifact_root/$prefix.XXXXXXXX")
echo "Evidence: $run"
cp "${AROS_TEST_PREFERENCES_BINARY:-build-aros/OpenZFSPreferencesTest}" "$run/OpenZFSPreferencesTest"
cp aros/ui/preferences.c aros/ui/preferences-file.c aros/ui/preferences.h aros/ui/preferences-test.c "$run/"
cp aros/ui/startup-settings.c aros/ui/startup-settings.h aros/ui/startup-settings-test.c "$run/"
cp aros/ui/startup-settings-file.c aros/ui/settings-file.h "$run/"
cp aros/ui/build-preferences-test.sh aros/ui/test-qemu-preferences.sh "$run/"
cp "$startup" "$run/Startup-Sequence"
cp -a aros/engine/security-fixture "$run/security-fixture"
if [[ $gui == 1 || $gui == failures || $gui == startup || $gui == simple ]];then
    cp "${AROS_TEST_MANAGER_BINARY:-build-aros/typed-import-service/OpenZFSManager}" "$run/OpenZFSManagerNative"
    cp build-aros/OpenZFSPreferencesGuiTest "$run/"
    tar -czf "$run/ui-source.tar.gz" aros/ui
    extra_maps=(-map "$run/OpenZFSManagerNative" /C/OpenZFSManagerNative
        -map "$run/OpenZFSPreferencesGuiTest" /C/OpenZFSPreferencesGuiTest)
    sha256sum "$run/OpenZFSManagerNative" "$run/OpenZFSPreferencesGuiTest" >"$run/gui-inputs.sha256"
fi
if [[ -f build-aros/preferences-build-inputs.sha256 ]];then
    cp build-aros/preferences-build-inputs.sha256 "$run/"
fi
git rev-parse HEAD >"$run/source.txt"
git diff --binary >"$run/source.patch"
sha256sum "$qemu" "$base" "$run/OpenZFSPreferencesTest" "${platform_inputs[@]}" "$posix" >"$run/inputs.sha256"
xorriso -indev "$base" -outdev "$run/test.iso" \
    -map aros/engine/grub-qemu-test.cfg /boot/grub/grub.cfg \
    "${platform_maps[@]}" \
    -map "$posix" /Libs/posixc.library \
    -map "$run/security-fixture" /Security \
    -map "$run/OpenZFSPreferencesTest" /C/OpenZFSPreferencesTest \
    "${extra_maps[@]}" -map "$run/Startup-Sequence" /S/Startup-Sequence \
    -boot_image any discard -boot_image grub bin_path=/boot/grub/i386-pc/eltorito.img \
    -boot_image grub boot_info_table=on -boot_image grub grub2_boot_info=on >"$run/pack.log" 2>&1
"$qemu" -name "openzfs-prefs-$$" -machine pc,accel=kvm -cpu host -smp 2 -m 2048 \
    -drive "file=$run/test.iso,media=cdrom,if=ide,index=2,readonly=on" \
    -vga std -display none -nic none -boot d -no-reboot -no-shutdown \
    -device qemu-xhci -device usb-tablet \
    -qmp "unix:$run/qmp.sock,server=on,wait=off" \
    -serial "file:$run/serial.log" -monitor "unix:$run/monitor,server=on,wait=off" >"$run/qemu.log" 2>&1 &
pid=$!
cleanup(){ if kill -0 "$pid" 2>/dev/null;then kill "$pid";wait "$pid" || true;fi; }
trap cleanup EXIT
deadline=$((SECONDS+timeout))
while :;do
    if [[ -f "$run/serial.log" ]];then
        sed -E 's/^[[:xdigit:]]{16} 0x[[:xdigit:]]{16} \| [0-9]+ \| //' "$run/serial.log" >"$run/serial-results.log"
        if rg -q '^RESULT .*FAIL|\[PF-DBG\]|^ASSERT at' "$run/serial-results.log";then exit 1;fi
        if rg -q "^$marker" "$run/serial-results.log";then break;fi
    fi
    if ! kill -0 "$pid" 2>/dev/null || ((SECONDS>=deadline));then echo "Timeout/exit: $run" >&2;exit 1;fi
    sleep 1
done
printf 'quit\n' | socat - "UNIX-CONNECT:$run/monitor" >/dev/null
wait "$pid"
trap - EXIT
echo "PASS: native IFF preferences. Evidence: $run"
if [[ $prefix == preferences-run ]];then
    python3 aros/distribution/prune-public-test-images.py --run "$(basename "$run")" --apply
fi
