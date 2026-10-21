#!/usr/bin/env bash
set -euo pipefail
if [[ ${OZ_FILE_BACKING_RUNNER_FROZEN:-0} != 1 ]];then
    export OZ_FILE_BACKING_RUNNER_FROZEN=1
    exec bash -c "$(<"$0")" "$0" "$@"
fi
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
release_bundle=${AROS_TEST_COLD_RELEASE_BUNDLE:-}
if [[ -n $release_bundle ]];then
    release_bundle=$(realpath -e "$release_bundle")
    (cd "$release_bundle" && sha256sum -c SHA256SUMS)
    rg -Fxq 'service_test_modes=0' "$release_bundle/Metadata/service-build-options.txt"
    rg -Fxq 'shared_engine=1' "$release_bundle/Metadata/service-build-options.txt"
    rg -Fxq 'full_dispatcher=1' "$release_bundle/Metadata/service-build-options.txt"
    : "${AROS_TEST_COLD_API_CLIENT:?release-mode cold client required}"
    strings "$AROS_TEST_COLD_API_CLIENT" | rg -F 'Cold release service: public admission ready; diagnostic harness absent' >/dev/null
    [[ ${AROS_TEST_COLD_API_CREATE:-0} == 1 && ${AROS_TEST_COLD_REMOVAL_RESUME:-0} == 0 ]]
    AROS_TEST_COLD_API_SERVICE="$release_bundle/C/OpenZFSServiceNative"
fi
base=${AROS_TEST_ISO:-/opt/aros-workspace/aros-amiwest-nightly-release/distfiles/aros-smp-pc-x86_64.iso}
dos=${AROS_TEST_DOS_OVERLAY:?private DOS overlay required}
ata=${AROS_TEST_ATA_OVERLAY:?private BSP overlay required}
binary=${AROS_TEST_COLD_DISCOVERY_BINARY:-${AROS_TEST_BACKING_BINARY:-build-aros/OpenZFSFileBackingTest}}
layout=${AROS_TEST_COLD_LAYOUT:-mirror}
case "$layout" in
    mirror|stripe) members=2 ;;
    raidz1) members=3 ;;
    raidz2) members=4 ;;
    raidz3) members=5 ;;
    *) echo 'Unknown cold fixture layout' >&2;exit 2 ;;
esac
missing=${AROS_TEST_COLD_MISSING:-0}
writable=${AROS_TEST_COLD_WRITABLE:-0}
api_create=${AROS_TEST_COLD_API_CREATE:-0}
destroy=${AROS_TEST_COLD_DESTROY:-0}
recover=${AROS_TEST_COLD_RECOVER:-0}
[[ $recover == 0 || $recover == 1 ]]
if ((recover)) && [[ $destroy != 1 ]];then
    echo 'Recovery suite requires the authenticated destruction suite' >&2;exit 2
fi
sync_crash=${AROS_TEST_COLD_SYNC_CRASH:-0}
flush_refusal=${AROS_TEST_COLD_FLUSH_REFUSAL:-0}
base_fat_reader=${AROS_TEST_COLD_BASE_FAT_READER:-0}
[[ $base_fat_reader == 0 || $base_fat_reader == 1 ]]
if ((base_fat_reader));then
    [[ $api_create == 1 && -n $release_bundle && $sync_crash == 1 ]]
    [[ -n ${AROS_TEST_FAT_HANDLER:-} ]]
fi
[[ $flush_refusal == 0 || $flush_refusal == 1 ]]
if ((flush_refusal));then
    [[ $api_create == 1 && -n $release_bundle && $sync_crash == 0 ]]
    [[ -z ${AROS_TEST_FAT_HANDLER:-} ]]
    strings "$AROS_TEST_COLD_API_CLIENT" | grep -F 'Cold writable file backing: unsupported flush refused PASS' >/dev/null
fi
rewind=${AROS_TEST_COLD_REWIND:-0}
detach=${AROS_TEST_COLD_DETACH:-0}
attach=${AROS_TEST_COLD_ATTACH:-0}
add=${AROS_TEST_COLD_ADD:-0}
aux=${AROS_TEST_COLD_AUX:-0}
remove=${AROS_TEST_COLD_REMOVE:-0}
removal_resume=${AROS_TEST_COLD_REMOVAL_RESUME:-0}
split=${AROS_TEST_COLD_SPLIT:-0}
promotion=${AROS_TEST_COLD_PROMOTION:-0}
relocate=${AROS_TEST_COLD_RELOCATE:-0}
[[ $relocate == 0 || $relocate == 1 ]]
if ((relocate)) && [[ $api_create != 1 || $missing != 0 || $writable != 0 || $destroy != 0 || $sync_crash != 0 || $rewind != 0 || $detach != 0 || $attach != 0 || $add != 0 || $promotion != 0 ]];then
    echo 'Relocation requires healthy API-created files and a cold readonly import' >&2;exit 2
fi
[[ $promotion == 0 || $promotion == 1 ]]
if ((promotion)) && [[ $api_create != 1 || $missing != 0 || $writable != 0 || $destroy != 0 || $sync_crash != 0 || $rewind != 0 || $detach != 0 || $attach != 0 || $add != 0 ]];then
    echo 'Promotion suite requires healthy API-created pools without another lifecycle suite' >&2;exit 2
fi
[[ $split == 0 || $split == 1 ]]
if ((split)) && [[ $add != 1 || $aux != 0 || $remove != 0 ]];then echo 'Split suite requires expansion without auxiliaries/removal' >&2;exit 2;fi
[[ $removal_resume == 0 || $removal_resume == 1 ]]
if ((removal_resume)) && [[ $remove != 1 ]];then echo 'Removal resumption requires removal suite' >&2;exit 2;fi
[[ $remove == 0 || $remove == 1 ]]
if ((remove)) && [[ $add != 1 || $aux != 0 ]];then echo 'Removal suite requires expansion without auxiliary suite' >&2;exit 2;fi
[[ $aux == 0 || $aux == 1 ]]
if ((aux)) && [[ $add != 1 ]];then echo 'Auxiliary suite requires pool expansion suite' >&2;exit 2;fi
[[ $add == 0 || $add == 1 ]]
if ((add)) && [[ $layout != mirror || $api_create != 1 || $missing != 0 || $writable != 0 || $destroy != 0 || $sync_crash != 0 || $rewind != 0 || $detach != 0 || $attach != 0 ]];then
    echo 'Expansion suite requires healthy API-created mirror and no other lifecycle suite' >&2;exit 2
fi
[[ $attach == 0 || $attach == 1 ]]
if ((attach)) && [[ $layout != mirror || $api_create != 1 || $missing != 0 || $writable != 0 || $destroy != 0 || $sync_crash != 0 || $rewind != 0 || $detach != 0 ]];then
    echo 'Attach suite requires healthy API-created mirror and no other lifecycle suite' >&2;exit 2
fi
[[ $detach == 0 || $detach == 1 ]]
if ((detach)) && [[ $layout != mirror || $api_create != 1 || $missing != 0 || $writable != 0 || $destroy != 0 || $sync_crash != 0 || $rewind != 0 ]];then
    echo 'Detach suite requires healthy API-created mirror and no other lifecycle suite' >&2;exit 2
fi
[[ $rewind == 0 || $rewind == 1 ]]
if ((rewind)) && [[ $api_create != 1 || $missing != 0 || $writable != 0 || $destroy != 0 || $sync_crash != 0 ]];then
    echo 'Checkpoint rewind requires healthy API-created pools and no other lifecycle suite' >&2;exit 2
fi
[[ $sync_crash == 0 || $sync_crash == 1 ]]
if ((sync_crash)) && [[ $api_create != 1 || $missing != 0 || $writable != 0 || $destroy != 0 ]];then
    echo 'Sync interruption requires API-created healthy files and no other mutation suite' >&2;exit 2
fi
[[ $destroy == 0 || $destroy == 1 ]]
if ((destroy)) && [[ $api_create != 1 || $missing != 0 || $writable != 0 ]];then
    echo 'Destroy suite requires new API-created healthy files and no writable-import suite' >&2;exit 2
fi
[[ $api_create == 0 || $api_create == 1 ]]
if [[ $layout == stripe && $api_create != 1 ]];then
    echo 'Stripe cold fixture requires authenticated API creation' >&2;exit 2
fi
if ((api_create)) && [[ -z ${AROS_TEST_COLD_API_SERVICE:-} ]];then
    echo 'API creation requires authenticated API client' >&2;exit 2
fi
[[ $writable == 0 || $writable == 1 ]]
if ((writable)) && [[ -z ${AROS_TEST_COLD_API_SERVICE:-} ]];then
    echo 'Writable cold import requires authenticated API client' >&2;exit 2
fi
[[ $missing =~ ^[0-9]$ ]] && ((missing<=members)) || { echo 'Invalid missing-member count' >&2;exit 2; }
tolerance=$((members-2));if [[ $layout == mirror ]];then tolerance=1;fi
if ((writable&&missing>tolerance));then
    echo 'Use read-only suite for beyond-redundancy refusal checks' >&2;exit 2
fi
if ((missing)) && [[ -z ${AROS_TEST_COLD_API_SERVICE:-} ]];then
    echo 'Missing-member test requires authenticated API client' >&2;exit 2
fi
if [[ $layout != mirror && -z ${AROS_TEST_COLD_DISCOVERY_BINARY:-} ]];then
    echo 'RAID layouts require the cold discovery fixture' >&2;exit 2
fi
extra_maps=()
if [[ -n ${AROS_TEST_COLD_API_SERVICE:-} ]];then
    : "${AROS_TEST_COLD_DISCOVERY_BINARY:?cold API requires the persistent creation fixture}"
    : "${AROS_TEST_COLD_API_CLIENT:?cold API client required}"
    extra_maps=(-map "$AROS_TEST_COLD_API_SERVICE" /C/OpenZFSAdmissionServiceHarness
        -map "$AROS_TEST_COLD_API_CLIENT" /C/OpenZFSColdImportTest)
fi
source "$repo/aros/engine/storage-preflight.sh"
artifact_root=$(oz_artifact_root "$repo/build-aros" "${AROS_TEST_ARTIFACT_ROOT:-}")
run=$(mktemp -d "$artifact_root/file-backing-run.XXXXXXXX")
echo "Evidence: $run"
cp "$0" "$run/test-qemu-file-backing.sh"
iso_dir=$run
if [[ -n ${AROS_TEST_ISO_TMPDIR:-} ]];then
    echo 'AROS_TEST_ISO_TMPDIR is retired; ISO evidence stays in the private repo run directory' >&2;exit 2
fi
printf '%s\n' "$iso_dir" >"$run/iso-location.txt"
printf '%s %s %s\n' "$layout" "$members" "$missing" >"$run/cold-layout.txt"
cp -- "$binary" "$run/OpenZFSFileBackingTest"
binary="$run/OpenZFSFileBackingTest"
if [[ -n ${AROS_TEST_COLD_API_SERVICE:-} ]];then
    cp -- "$AROS_TEST_COLD_API_SERVICE" "$run/OpenZFSAdmissionServiceHarness"
    cp -- "$AROS_TEST_COLD_API_CLIENT" "$run/OpenZFSColdImportTest"
    extra_maps=(-map "$run/OpenZFSAdmissionServiceHarness" /C/OpenZFSAdmissionServiceHarness
        -map "$run/OpenZFSColdImportTest" /C/OpenZFSColdImportTest)
fi
if [[ -n $release_bundle ]];then
    mv "$run/OpenZFSAdmissionServiceHarness" "$run/OpenZFSServiceNative"
    cp "$release_bundle/Libs/openzfs.library" "$run/openzfs.library"
    cp "$release_bundle/SHA256SUMS" "$run/release-bundle.sha256"
    cp "$release_bundle/Metadata/service-build-options.txt" "$run/release-service-build-options.txt"
    printf '%s\n' "$release_bundle" >"$run/release-source.txt"
    extra_maps=(-map "$run/OpenZFSServiceNative" /C/OpenZFSServiceNative
        -map "$run/openzfs.library" /Libs/openzfs.library
        -map "$run/OpenZFSColdImportTest" /C/OpenZFSColdImportTest)
fi
git rev-parse HEAD >"$run/source.txt"
git diff --binary >"$run/source.patch"
tar -czf "$run/test-source.tar.gz" aros/engine
sha256sum "$base" "$binary" "$dos/aros-base.pkg.xz" "$ata/aros-bsp.pkg.xz" >"$run/inputs.sha256"
if [[ -n ${AROS_TEST_COLD_API_SERVICE:-} ]];then
    if [[ -n $release_bundle ]];then
        sha256sum "$run/OpenZFSServiceNative" "$run/openzfs.library" "$run/OpenZFSColdImportTest" >>"$run/inputs.sha256"
    else
        sha256sum "$run/OpenZFSAdmissionServiceHarness" "$run/OpenZFSColdImportTest" >>"$run/inputs.sha256"
    fi
    tar -czf "$run/client-source.tar.gz" aros/ui
fi
if [[ -n ${AROS_TEST_FAT_HANDLER:-} ]];then
    cp -- "$AROS_TEST_FAT_HANDLER" "$run/fat-handler"
    sha256sum "$run/fat-handler" >>"$run/inputs.sha256"
    if ((!base_fat_reader));then
        extra_maps+=(-map "$run/fat-handler" /L/fat-handler)
    fi
fi
if [[ -n ${AROS_TEST_POSIX_LIBRARY:-} ]];then
    cp -- "$AROS_TEST_POSIX_LIBRARY" "$run/posixc.library"
    sha256sum "$run/posixc.library" >>"$run/inputs.sha256"
    extra_maps+=(-map "$run/posixc.library" /Libs/posixc.library)
fi
if [[ -n ${AROS_TEST_FAT_CACHE_BINARY:-} ]];then
    cp -- "$AROS_TEST_FAT_CACHE_BINARY" "$run/OpenZFSFATCacheTest"
    sha256sum "$run/OpenZFSFATCacheTest" >>"$run/inputs.sha256"
    extra_maps+=(-map "$run/OpenZFSFATCacheTest" /C/OpenZFSFATCacheTest)
fi
if [[ -n ${AROS_TEST_FSYNC_ERROR_BINARY:-} ]];then
    cp -- "$AROS_TEST_FSYNC_ERROR_BINARY" "$run/OpenZFSFsyncErrorTest"
    sha256sum "$run/OpenZFSFsyncErrorTest" >>"$run/inputs.sha256"
    extra_maps+=(-map "$run/OpenZFSFsyncErrorTest" /C/OpenZFSFsyncErrorTest)
fi
# This exact new regular file is the only format target. No physical device.
provisioned=$members;withheld=$missing
if ((attach));then provisioned=4;withheld=2;fi
if ((add));then provisioned=4;fi
if ((aux));then provisioned=7;fi
if ((remove));then provisioned=5;withheld=2;fi
backing_mib=$((provisioned*64+128))
truncate -s "${backing_mib}M" "$run/backing.raw"
sed "s/^HighCyl .*/HighCyl        = $((backing_mib*2048-1))/" aros/engine/OZBACK >"$run/OZBACK"
mkfs.vfat -F 16 -n OZFILES "$run/backing.raw" >"$run/format.log" 2>&1
mcopy -i "$run/backing.raw" aros/engine/file-backing-fixture.txt ::fixture.txt
if ((api_create));then
    # Same nonce marker as discovery fixtures; unused object-ID field is zero.
    # All ZFS creation and payload writes still happen through the native API.
    mcopy -i "$run/backing.raw" aros/engine/cold-api-manifest.txt ::manifest.txt
fi
if [[ -n ${AROS_TEST_COLD_DISCOVERY_BINARY:-} ]];then
    # Provision blank vdev files offline. Native FAT file extension currently
    # leaks clusters; keep that preserved failure distinct from OpenZFS IO.
    truncate -s 64M "$run/blank-member.raw"
    mmd -i "$run/backing.raw" ::OpenZFS-test-51a20519
    for ((member=0;member<provisioned;member++));do
        mcopy -i "$run/backing.raw" "$run/blank-member.raw" "::OpenZFS-test-51a20519/disk$member"
    done
fi
pid=
cleanup(){ if [[ -n $pid ]] && kill -0 "$pid" 2>/dev/null;then kill "$pid";wait "$pid" || true;fi; }
trap cleanup EXIT
phases=(write read)
if ((split));then phases=(write read split);fi
if ((removal_resume));then phases=(write resume read);fi
if ((writable||destroy));then phases+=(verify);fi
if ((recover));then phases+=(recover normal);fi
if ((rewind));then phases=(write preview current rewind final);fi
if ((flush_refusal));then phases=(write);fi
for phase in "${phases[@]}";do
    mode=0
    if ((writable));then
        if [[ $phase == read ]];then mode=1;fi
        if [[ $phase == verify ]];then mode=2;fi
    fi
    if ((api_create)) && [[ $phase == write ]];then mode=3;fi
    if [[ $phase == resume ]];then mode=14;fi
    if ((sync_crash)) && [[ $phase == write ]];then mode=6;fi
    if ((destroy));then
        if [[ $phase == read ]];then mode=4;fi
        if [[ $phase == verify ]];then mode=5;fi
    fi
    if ((recover));then
        if [[ $phase == verify ]];then mode=7;fi
        if [[ $phase == recover ]];then mode=8;fi
        if [[ $phase == normal ]];then mode=9;fi
    fi
    if ((rewind));then
        case "$phase" in
            preview) mode=10;; current) mode=11;; rewind) mode=12;; final) mode=13;;
        esac
    fi
    split_mode=0
    if ((split));then case "$phase" in write) split_mode=1;; read) split_mode=2;; split) split_mode=3;; esac;fi
    moved=0
    if ((relocate)) && [[ $phase == read ]];then moved=1;fi
    printf '%s %s %s %s %s %s %s %s %s %s %s %s %s %s\n' "$layout" "$members" "$missing" "$mode" "$api_create" "$detach" "$attach" "$add" "$aux" "$remove" "$removal_resume" "$split_mode" "$promotion" "$moved" >"$run/$phase-layout.txt"
    startup=aros/engine/Startup-FileBacking
    expected='RESULT backing write PASS'
    duration=120
    if [[ $phase != write ]];then
        expected='RESULT backing cold read PASS'
        startup=aros/engine/Startup-FileBacking-Read
    fi
    if [[ -n ${AROS_TEST_COLD_DISCOVERY_BINARY:-} ]];then
        duration=300
        if [[ $phase == write ]];then
            startup=aros/engine/Startup-ColdDiscovery-Create
            expected='RESULT cold discovery create PASS'
        else
            startup=aros/engine/Startup-ColdDiscovery-Verify
            expected='RESULT cold discovery verify PASS'
            if [[ -n ${AROS_TEST_COLD_API_SERVICE:-} ]];then
                startup=aros/engine/Startup-ColdAdmission-Verify
                expected='RESULT cold authenticated import PASS'
            fi
        fi
    fi
    if ((api_create)) && [[ $phase == write ]];then
        startup=aros/engine/Startup-ColdAdmission-Verify
        expected='RESULT cold authenticated import PASS'
        if ((sync_crash));then expected='RESULT authenticated pool sync ready for guest interruption';fi
        if ((removal_resume));then expected='RESULT removal synced and running ready for guest interruption';fi
    fi
    # Generated fixture startup only; checked-in startup templates stay intact.
    if [[ -n ${AROS_TEST_COLD_DISCOVERY_BINARY:-} ]];then
        sed "s|^C:OpenZFSFileBackingTest$|C:OpenZFSFileBackingTest $layout|" "$startup" >"$run/$phase-startup"
    else
        cp -- "$startup" "$run/$phase-startup"
    fi
    startup="$run/$phase-startup"
    if [[ -n $release_bundle ]];then
        sed '/^Run >T:cold-service.log C:OpenZFSAdmissionServiceHarness COLD$/d' "$startup" >"$run/$phase-release-startup"
        startup="$run/$phase-release-startup"
    fi
    if ((removal_resume)) && [[ $phase == write ]];then
        sed 's/OpenZFSAdmissionServiceHarness COLD/OpenZFSAdmissionServiceHarness COLD-CANCEL/' "$startup" >"$run/$phase-paused-startup"
        startup="$run/$phase-paused-startup"
    fi
    if [[ -n ${AROS_TEST_FAT_CACHE_BINARY:-} ]];then
        # FailAt 21 intentionally lets test failure reach serial reporting;
        # the host below rejects the FAIL marker independently of shell status.
        sed '/^Stack /a C:OpenZFSFATCacheTest' "$startup" >"$run/$phase-cache-startup"
        startup="$run/$phase-cache-startup"
    fi
    if [[ -n ${AROS_TEST_FSYNC_ERROR_BINARY:-} ]];then
        sed '/^Stack /a C:OpenZFSFsyncErrorTest' "$startup" >"$run/$phase-fsync-startup"
        startup="$run/$phase-fsync-startup"
    fi
    phase_maps=()
    if ((base_fat_reader));then
        if [[ $phase == write ]];then
            phase_maps=(-map "$run/fat-handler" /L/fat-handler)
            printf 'private flush-capable FAT\n' >"$run/$phase-fat-runtime.txt"
        else
            printf 'base ISO FAT; no handler overlay\n' >"$run/$phase-fat-runtime.txt"
        fi
    fi
xorriso -indev "$base" -outdev "$iso_dir/$phase.iso" \
    -map aros/engine/grub-qemu-test.cfg /boot/grub/grub.cfg \
    -map "$dos/aros-base.pkg.xz" /boot/aros-base.pkg.xz \
    -map "$ata/aros-bsp.pkg.xz" /boot/pc-smp/aros-bsp.pkg.xz \
    -map "$binary" /C/OpenZFSFileBackingTest \
    -map "$run/$phase-layout.txt" /cold-layout.txt \
    "${extra_maps[@]}" \
    "${phase_maps[@]}" \
    -map build-aros/hosted/Libs/z1.library /Libs/z1.library \
    -map aros/engine/security-fixture /Security \
    -map "$run/OZBACK" /OZBACK \
    -map "$startup" /S/Startup-Sequence \
    -boot_image any discard -boot_image grub bin_path=/boot/grub/i386-pc/eltorito.img \
    -boot_image grub boot_info_table=on -boot_image grub grub2_boot_info=on >"$run/pack-$phase.log" 2>&1
    sha256sum "$iso_dir/$phase.iso" >"$run/$phase-iso.sha256"
    qemu-system-x86_64 -name "openzfs-backing-$phase-$$" -machine pc,accel=kvm -cpu host -smp 2 -m 2048 \
        -drive "file=$iso_dir/$phase.iso,media=cdrom,if=ide,index=2,readonly=on" \
        -drive "file=$run/backing.raw,format=raw,if=ide,index=0" \
        -display none -nic none -boot d -no-reboot -no-shutdown \
        -serial "file:$run/$phase-serial.log" -monitor "unix:$run/$phase.monitor,server=on,wait=off" >"$run/$phase-qemu.log" 2>&1 &
    pid=$!;printf '%s\n' "$pid" >"$run/$phase.pid"
    deadline=$((SECONDS+duration))
    while :;do
        if [[ -f "$run/$phase-serial.log" ]];then
            if rg -a -q '^FAIL |^RESULT .*FAIL|\[PF-DBG\]|^ASSERT at' "$run/$phase-serial.log";then echo "FAIL $phase: $run";exit 1;fi
            if rg -a -q "^$expected" "$run/$phase-serial.log";then break;fi
        fi
        if ! kill -0 "$pid" 2>/dev/null || ((SECONDS>=deadline));then echo "FAIL deadline/exit $phase: $run";exit 1;fi
        sleep 1
    done
    if ((sync_crash||removal_resume)) && [[ $phase == write ]];then
        # The captured PID is the guest just launched above, not a discovered
        # user VM. SIGKILL prevents any guest shutdown/export or QEMU cleanup.
        kill -KILL "$pid"
        rc=0;wait "$pid" || rc=$?
        [[ $rc == 137 ]]
        printf 'Private QEMU SIGKILL after successful authenticated sync; exit=%s\n' "$rc" >"$run/$phase-interruption.log"
    else
        printf 'info blockstats\nquit\n' | socat - "UNIX-CONNECT:$run/$phase.monitor" >"$run/$phase-monitor.log"
        wait "$pid"
    fi
    pid=
    if [[ -n $release_bundle ]];then
        rg -q '^Cold release service: public admission ready; diagnostic harness absent\r?$' "$run/$phase-serial.log"
    fi
    sha256sum "$run/backing.raw" >"$run/$phase-disk.sha256"
    if [[ -n ${AROS_TEST_FAT_CACHE_BINARY:-} ]];then
        rg -q '^RESULT native FAT dirty retry PASS' "$run/$phase-serial.log"
    fi
    if [[ -n ${AROS_TEST_FSYNC_ERROR_BINARY:-} ]];then
        rg -q '^RESULT native fsync error paths PASS' "$run/$phase-serial.log"
    fi
    if [[ -n ${AROS_TEST_COLD_DISCOVERY_BINARY:-} ]];then
        mcopy -i "$run/backing.raw" ::manifest.txt "$run/$phase-manifest.txt"
        read -r nonce object <"$run/$phase-manifest.txt"
        [[ $nonce =~ ^[0-9a-f]{8}$ && $object =~ ^[0-9]+$ ]]
        for ((member=0;member<provisioned;member++));do
            suffix=
            if [[ $phase != write && $phase != resume ]] && ((member<withheld));then suffix=.withheld;fi
            if ((split)) && { { [[ $phase == read ]] && ((member%2==1)); } || { [[ $phase == split ]] && ((member%2==0)); }; };then suffix=.withheld;fi
            stem=disk;if ((moved));then stem=moved;fi
            mcopy -i "$run/backing.raw" "::OpenZFS-test-$nonce/$stem$member$suffix" "$run/$phase-member$member.raw"
        done
        if { ((!removal_resume)) && [[ $phase == write ]]; } || { ((removal_resume)) && [[ $phase == resume ]]; };then
            for ((member=0;member<withheld;member++));do
                mren -i "$run/backing.raw" "::OpenZFS-test-$nonce/disk$member" "disk$member.withheld"
            done
        fi
        if ((split)) && [[ $phase == write ]];then
            for member in 1 3;do mren -i "$run/backing.raw" "::OpenZFS-test-$nonce/disk$member" "disk$member.withheld";done
        fi
        if ((split)) && [[ $phase == read ]];then
            for member in 1 3;do mren -i "$run/backing.raw" "::OpenZFS-test-$nonce/disk$member.withheld" "disk$member";done
            for member in 0 2;do mren -i "$run/backing.raw" "::OpenZFS-test-$nonce/disk$member" "disk$member.withheld";done
        fi
        if ((relocate)) && [[ $phase == write ]];then
            for ((member=0;member<provisioned;member++));do
                mren -i "$run/backing.raw" "::OpenZFS-test-$nonce/disk$member" "moved$member"
            done
        fi
    fi
done
if ((flush_refusal));then
    rg -q '^Cold writable file backing: unsupported flush refused PASS' "$run/write-serial.log"
    for ((member=0;member<provisioned;member++));do
        cmp "$run/blank-member.raw" "$run/write-member$member.raw"
        sha256sum "$run/write-member$member.raw" >>"$run/refused-members.sha256"
    done
    echo "PASS unsupported flush refused, all member bytes unchanged: $run"
    exit 0
fi
if ((relocate));then
    rg -q '^Cold relocation: old names absent, discovered moved members ONLINE PASS' "$run/read-serial.log"
    paths=()
    for ((member=0;member<provisioned;member++));do paths+=(-p "$run/read-member$member.raw");done
    LD_LIBRARY_PATH="$repo/build-control/.libs:$repo/build-control/deps/usr/lib/x86_64-linux-gnu" \
        build-control/zdb -e "${paths[@]}" -bcc "aros_${layout}_$nonce" >"$run/relocated-zdb.log" 2>&1
    rg -q 'No leaks' "$run/relocated-zdb.log"
fi
if ((split));then
    rg -q '^Cold split: populated mirrors split, destination exported, source healthy PASS' "$run/write-serial.log"
    for phase in read split;do
        cmp "$run/write-manifest.txt" "$run/$phase-manifest.txt"
        rg -q '^Cold split: independent pool exact 8MiB DOS payload and allocated leaves PASS' "$run/$phase-serial.log"
        rg -q '^Cold authenticated import: native directory enumeration PASS' "$run/$phase-serial.log"
        for member in 0 1 2 3;do cmp "$run/write-member$member.raw" "$run/$phase-member$member.raw";done
        parity=0;pool="aros_${layout}_$nonce"
        if [[ $phase == split ]];then parity=1;pool="aros_split_$nonce";fi
        LD_LIBRARY_PATH="$repo/build-control/.libs:$repo/build-control/deps/usr/lib/x86_64-linux-gnu" \
            build-control/zdb -e -p "$run/$phase-member$parity.raw" -p "$run/$phase-member$((parity+2)).raw" \
            -bcc "$pool" >"$run/$phase-zdb.log" 2>&1
        rg -q 'No leaks' "$run/$phase-zdb.log"
    done
    echo "PASS split pools independently cold imported, native DOS payload, unchanged files and zdb: $run"
    exit 0
fi
if ((add));then
    before=write
    if ((removal_resume));then
        before=resume
        cmp "$run/write-manifest.txt" "$run/resume-manifest.txt"
        rg -q '^RESULT removal synced and running ready for guest interruption' "$run/write-serial.log"
        rg -q '^Cold removal resume: fresh writable import completed persisted evacuation PASS' "$run/resume-serial.log"
        rg -q '^Cold removal resume: completed topology and native DOS payload PASS' "$run/resume-serial.log"
    fi
    cmp "$run/$before-manifest.txt" "$run/read-manifest.txt"
    zdb_paths=()
    for ((member=0;member<provisioned;member++));do
        cmp "$run/$before-member$member.raw" "$run/read-member$member.raw"
        if ((!remove||member>=2));then zdb_paths+=(-p "$run/read-member$member.raw");fi
    done
    rg -q 'Cold expansion: increased capacity, new mirror allocated, 8MiB DOS payload PASS' "$run/write-serial.log"
    rg -q 'Cold expansion: fresh import, new mirror allocated, exact 8MiB DOS payload PASS' "$run/read-serial.log"
    if ((aux));then
        rg -q 'Cold auxiliary: log/cache/spare added and native payload PASS' "$run/write-serial.log"
        rg -q 'Cold auxiliary: fresh read-only log ONLINE cache/spare INACTIVE and native payload PASS' "$run/read-serial.log"
    fi
    if ((remove));then
        if ((!removal_resume));then rg -q 'Cold removal: original populated mirror evacuated, copied bytes positive, indirect topology PASS' "$run/write-serial.log";fi
        rg -q 'Cold removal: original files absent, surviving-only import and exact native DOS payload PASS' "$run/read-serial.log"
    fi
    LD_LIBRARY_PATH="$repo/build-control/.libs:$repo/build-control/deps/usr/lib/x86_64-linux-gnu" \
        build-control/zdb -e "${zdb_paths[@]}" -bcc "aros_${layout}_$nonce" >"$run/add-zdb.log" 2>&1
    rg -q 'No leaks' "$run/add-zdb.log"
    echo "PASS cold pool expansion, unchanged read-only members, exact native DOS payload and independent zdb: $run"
    exit 0
fi
if ((attach));then
    cmp "$run/write-manifest.txt" "$run/read-manifest.txt"
    for ((member=0;member<provisioned;member++));do
        cmp "$run/write-member$member.raw" "$run/read-member$member.raw"
    done
    rg -q 'Cold attach: resilver complete zero errors PASS' "$run/write-serial.log"
    rg -q 'Cold replace: resilver complete zero errors PASS' "$run/write-serial.log"
    rg -q 'Cold attach/replace: original members absent, new members native DOS read/enumeration PASS' "$run/read-serial.log"
    LD_LIBRARY_PATH="$repo/build-control/.libs:$repo/build-control/deps/usr/lib/x86_64-linux-gnu" \
        build-control/zdb -e -p "$run/read-member2.raw" -p "$run/read-member3.raw" \
        -bcc "aros_${layout}_$nonce" >"$run/attach-zdb.log" 2>&1
    rg -q 'No leaks' "$run/attach-zdb.log"
    for member in 2 3;do
        LD_LIBRARY_PATH="$repo/build-control/.libs:$repo/build-control/deps/usr/lib/x86_64-linux-gnu" \
            build-control/zdb -e -p "$run/read-member$member.raw" -bcc "aros_${layout}_$nonce" \
            >"$run/individual-member$member-zdb.log" 2>&1
        rg -q 'No leaks' "$run/individual-member$member-zdb.log"
    done
    echo "PASS cold attach/replace, original members withheld, native DOS payload and independent new-member zdb: $run"
    exit 0
fi
if ((rewind));then
    for ((member=0;member<members;member++));do
        cmp "$run/write-member$member.raw" "$run/preview-member$member.raw"
        cmp "$run/write-member$member.raw" "$run/current-member$member.raw"
        if cmp -s "$run/current-member$member.raw" "$run/rewind-member$member.raw";then
            echo 'FAIL writable checkpoint rewind did not change member' >&2;exit 1
        fi
        cmp "$run/rewind-member$member.raw" "$run/final-member$member.raw"
    done
    for phase in preview current rewind final;do
        cmp "$run/write-manifest.txt" "$run/$phase-manifest.txt"
        rg -q '^Cold checkpoint rewind mode=.* payload/enumeration PASS' "$run/$phase-serial.log"
    done
    zdb_paths=()
    for ((member=0;member<members;member++));do zdb_paths+=(-p "$run/final-member$member.raw");done
    LD_LIBRARY_PATH="$repo/build-control/.libs:$repo/build-control/deps/usr/lib/x86_64-linux-gnu" \
        build-control/zdb -e "${zdb_paths[@]}" -bcc "aros_${layout}_$nonce" >"$run/rewind-zdb.log" 2>&1
    rg -q 'No leaks' "$run/rewind-zdb.log"
    echo "PASS five-boot checkpoint preview, unchanged current state, persistent writable rewind and independent zdb: $run"
    exit 0
fi
if [[ -n ${AROS_TEST_COLD_DISCOVERY_BINARY:-} ]];then
    cmp "$run/write-manifest.txt" "$run/read-manifest.txt"
    zdb_paths=()
    for ((member=0;member<members;member++));do
        if (((writable||destroy)&&member>=missing));then
            if cmp -s "$run/write-member$member.raw" "$run/read-member$member.raw";then
                echo "FAIL: surviving member $member did not change after writes" >&2;exit 1
            fi
        else
            cmp "$run/write-member$member.raw" "$run/read-member$member.raw"
        fi
        if ((writable||destroy));then cmp "$run/read-member$member.raw" "$run/verify-member$member.raw";fi
        if ((!writable||member>=missing));then zdb_paths+=(-p "$run/read-member$member.raw");fi
    done
    if ((destroy));then
        rg -q 'Cold authenticated creation: native DOS payload PASS' "$run/write-serial.log"
        rg -q 'Cold authenticated destroy: namespace removed PASS' "$run/read-serial.log"
        if ((recover));then
            rg -q 'Cold destroyed recovery: read-only recovery retains destroyed state PASS' "$run/verify-serial.log"
        else
            rg -q 'Cold authenticated destroy: fresh-service import refusal and lock release PASS' "$run/verify-serial.log"
        fi
        cmp "$run/read-manifest.txt" "$run/verify-manifest.txt"
        for ((member=0;member<members;member++));do
            LD_LIBRARY_PATH="$repo/build-control/.libs:$repo/build-control/deps/usr/lib/x86_64-linux-gnu" \
                build-control/zdb -l "$run/read-member$member.raw" >"$run/destroyed-labels-$member.log" 2>&1
            # zdb coalesces identical configs and lists their label numbers.
            awk '/^    state:/ {if($2!=2)exit 1; states++}
                /^    labels =/ {for(i=3;i<=NF;i++){if($i!~/^[0-3]$/)exit 1;seen[$i]=1}}
                END {if(!states||length(seen)!=4)exit 1}' "$run/destroyed-labels-$member.log"
        done
        if ((recover));then
            zdb_paths=()
            for ((member=0;member<members;member++));do
                if cmp -s "$run/verify-member$member.raw" "$run/recover-member$member.raw";then
                    echo 'FAIL: writable recovery did not update member' >&2;exit 1
                fi
                cmp "$run/recover-member$member.raw" "$run/normal-member$member.raw"
                LD_LIBRARY_PATH="$repo/build-control/.libs:$repo/build-control/deps/usr/lib/x86_64-linux-gnu" \
                    build-control/zdb -l "$run/recover-member$member.raw" >"$run/recovered-labels-$member.log" 2>&1
                awk '/^    state:/ {if($2!=1)exit 1; states++}
                    /^    labels =/ {for(i=3;i<=NF;i++){if($i!~/^[0-3]$/)exit 1;seen[$i]=1}}
                    END {if(!states||length(seen)!=4)exit 1}' "$run/recovered-labels-$member.log"
                zdb_paths+=(-p "$run/recover-member$member.raw")
            done
            LD_LIBRARY_PATH="$repo/build-control/.libs:$repo/build-control/deps/usr/lib/x86_64-linux-gnu" \
                build-control/zdb -e "${zdb_paths[@]}" -bcc "aros_${layout}_$nonce" >"$run/recovered-zdb.log" 2>&1
            rg -q 'No leaks' "$run/recovered-zdb.log"
            rg -q 'Cold destroyed recovery: writable payload PASS' "$run/recover-serial.log"
            rg -q 'Cold destroyed recovery: normal import payload PASS' "$run/normal-serial.log"
            echo "PASS five-boot destroyed-pool recovery, RO byte preservation, RW payload and independent zdb: $run"
        else
            echo "PASS cold authenticated pool destruction, persisted labels and fresh import refusal: $run"
        fi
        exit 0
    fi
    LD_LIBRARY_PATH="$repo/build-control/.libs:$repo/build-control/deps/usr/lib/x86_64-linux-gnu" \
        build-control/zdb -e "${zdb_paths[@]}" \
        -bcc "aros_${layout}_$nonce" >"$run/linux-zdb.log" 2>&1
    rg -q 'No leaks' "$run/linux-zdb.log"
    if ((detach));then
        rg -q 'Cold detach: one ONLINE leaf after mirror collapse PASS' "$run/write-serial.log"
        rg -q 'Cold detach: fresh import topology and DOS payload PASS' "$run/read-serial.log"
        echo "PASS cold detach, native DOS read/enumeration, unchanged read-only members and independent zdb: $run"
        exit 0
    fi
    if ((sync_crash));then
        rg -q '^RESULT authenticated pool sync ready for guest interruption' "$run/write-serial.log"
        rg -q 'Cold authenticated import: exact native file read PASS' "$run/read-serial.log"
        echo "PASS authenticated sync, abrupt guest loss without close/export, cold DOS read and independent zdb: $run"
    fi
    if ((api_create));then
        rg -q 'Cold authenticated creation: native DOS payload PASS' "$run/write-serial.log"
        rg -q 'Cold authenticated creation: nonblank refusal without partial labels PASS' "$run/write-serial.log"
    fi
    if ((promotion));then
        rg -q '^Cold promotion: created origins and snapshot GUID PASS' "$run/write-serial.log"
        rg -q '^Cold promotion: fresh boot origins snapshot GUID and readonly refusal PASS' "$run/read-serial.log"
        rg -q '^Cold bookmark: snapshot deleted and GUID retained PASS' "$run/write-serial.log"
        rg -q '^Cold bookmark: fresh boot GUID survived snapshot and readonly mutations denied PASS' "$run/read-serial.log"
    fi
    if ((writable));then
        rg -q 'Cold authenticated import: writable degraded payload PASS' "$run/read-serial.log"
        rg -q 'Cold authenticated import: cold degraded payload PASS' "$run/verify-serial.log"
        cmp "$run/read-manifest.txt" "$run/verify-manifest.txt"
        echo "PASS writable cold import, degraded DOS writes, third-boot readback and unchanged absent members: $run"
        exit 0
    fi
    if [[ -n ${AROS_TEST_COLD_API_SERVICE:-} ]];then
        tolerance=$((members-2));if [[ $layout == mirror ]];then tolerance=1;fi
        if ((missing>tolerance));then
            rg -q 'Cold authenticated import: beyond-redundancy refusal and lock release PASS' "$run/read-serial.log"
            echo "PASS cold beyond-redundancy refusal, absent pool and unchanged members: $run"
        else
            echo "PASS cold authenticated file import, native DOS payload/enumeration and unchanged members: $run"
        fi
    else
        echo "PASS cold upstream file-mirror discovery, exact payload and unchanged member files: $run"
    fi
    exit 0
fi
mcopy -i "$run/backing.raw" ::proof.txt "$run/proof.txt"
rg -q '^RESULT backing identity probe PASS' "$run/write-serial.log"
[[ $(<"$run/proof.txt") == 'Native AROS persistent backing IO' ]]
echo "PASS native FAT backing cold read and independent host extraction: $run"
