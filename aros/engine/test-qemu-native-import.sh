#!/usr/bin/env bash
set -euo pipefail
repo=${OZ_NATIVE_RUNNER_REPO:-$(cd -- "$(dirname -- "$0")/../.." && pwd)}
repo=$(realpath -e "$repo")
cd "$repo"
source "$repo/aros/engine/storage-preflight.sh"
artifact_root=$(oz_artifact_root "$repo" "${AROS_TEST_ARTIFACT_ROOT:-}")
if [[ -z ${AROS_TEST_ARTIFACT_ROOT:-} ]];then
    artifact_root="$repo/build-aros"
    mkdir -p "$artifact_root"
fi
# Bash reads later commands lazily. Run an immutable private copy so source
# edits during a long GUI session cannot splice new text into the live parser.
if [[ ${OZ_NATIVE_RUNNER_FROZEN:-0} != 1 ]];then
    frozen=$(mktemp -d "$artifact_root/native-runner.XXXXXXXX")
    cp "$0" "$frozen/test-qemu-native-import.sh"
    export OZ_NATIVE_RUNNER_FROZEN=1
    export OZ_NATIVE_RUNNER_REPO="$repo"
    exec bash "$frozen/test-qemu-native-import.sh" "$@"
fi
fixture=${AROS_TEST_NATIVE_FIXTURE:?existing disposable partition pool fixture required}
fixture=$(realpath -e "$fixture")
case "$fixture" in "$repo"/build-aros/qemu-run.*|"$repo"/build-aros/native-raidz-run.*|"$artifact_root"/qemu-run.*|"$artifact_root"/native-raidz-run.*) ;; *) exit 2;; esac
bundle=${AROS_TEST_NATIVE_BUNDLE:-}
if [[ -n $bundle ]];then
    bundle=$(realpath -e "$bundle")
    case "$bundle" in "$repo"/build-aros/native-bundle.*|"$artifact_root"/native-bundle.*) ;; *) exit 2;; esac
    (cd "$bundle" && sha256sum -c SHA256SUMS)
    if [[ -f "$bundle/Metadata/service-build-options.txt" ]];then
        rg -Fxq 'service_test_modes=0' "$bundle/Metadata/service-build-options.txt"
        AROS_TEST_SERVICE_RELEASE=1
    fi
    AROS_TEST_SERVICE_BINARY="$bundle/C/OpenZFSServiceNative"
    # The in-flight hot-unplug fixture owns a distinct writable lifecycle and
    # deliberately runs with AROS_TEST_NATIVE_WRITABLE=0.  Keep the exact
    # bundle's service/library/handler inputs, but leave its normal writable
    # whole-CLI matrix for a separate package regression.
    if [[ ${AROS_TEST_USB_INFLIGHT_WRITE:-0} == 1 ]];then
        AROS_TEST_WHOLE_CLI_LINK=
    else
        AROS_TEST_WHOLE_CLI_LINK="$bundle/Metadata"
    fi
    if rg -Fxq 'shared_engine=1' "$bundle/Metadata/service-build-options.txt";then
        AROS_TEST_ENGINE_LIBRARY="$bundle/Libs/openzfs.library"
    fi
    if [[ -f "$bundle/L/OpenZFS-Handler" ]];then
        AROS_TEST_HANDLER_BINARY="$bundle/L/OpenZFS-Handler"
    fi
fi
service=${AROS_TEST_SERVICE_BINARY:?service required}
if [[ -n ${AROS_TEST_DEVICE_INVENTORY:-} ]];then
    [[ ${AROS_TEST_NATIVE_BUS:-ide} == ide && ${AROS_TEST_NATIVE_MEMBERS:-both} == both ]] || {
        echo 'Inventory fixture requires both IDE members' >&2;exit 2;
    }
fi
if [[ ${AROS_TEST_CLI_POOL_CREATE:-0} == 1 && -z ${AROS_TEST_WHOLE_CLI_LINK:-} ]];then
    echo 'CLI_POOL_CREATE requires AROS_TEST_WHOLE_CLI_LINK; refusing a baseline-only run.' >&2
    exit 2
fi
if [[ ${AROS_TEST_ZVOL_TRANSPORT:-0} == 1 && -z ${AROS_TEST_WHOLE_CLI_LINK:-} ]];then
    echo 'ZVOL_TRANSPORT requires AROS_TEST_WHOLE_CLI_LINK; refusing a baseline-only run.' >&2
    exit 2
fi
runtime_profile=${AROS_TEST_RUNTIME_PROFILE:-private}
[[ $runtime_profile == private || $runtime_profile == base || $runtime_profile == dos-only || $runtime_profile == dos-posix || $runtime_profile == dos-posix-ata || $runtime_profile == posix-only || $runtime_profile == posix-pipe || $runtime_profile == posix-pipe-stdio || $runtime_profile == dos-posix-pipe-stdio ]]
if [[ $runtime_profile != private ]];then
    # This mode measures the selected ISO, not a silently repaired runtime.
    for override in AROS_TEST_DOS_OVERLAY AROS_TEST_ATA_OVERLAY AROS_TEST_POSIX_LIBRARY \
        AROS_TEST_PARTITION_LIBRARY AROS_TEST_FS_PACKAGE AROS_TEST_PIPE_POLICY_HANDLER \
        AROS_TEST_AFS_HANDLER AROS_TEST_FAT_HANDLER AROS_TEST_STDCIO_LIBRARY;do
        if [[ $runtime_profile == dos-posix-ata && ( $override == AROS_TEST_DOS_OVERLAY || $override == AROS_TEST_ATA_OVERLAY || $override == AROS_TEST_POSIX_LIBRARY ) ]];then continue;fi
        if [[ ( $runtime_profile == dos-only || $runtime_profile == dos-posix || $runtime_profile == dos-posix-pipe-stdio ) && $override == AROS_TEST_DOS_OVERLAY ]];then continue;fi
        if [[ ( $runtime_profile == posix-only || $runtime_profile == dos-posix || $runtime_profile == posix-pipe || $runtime_profile == posix-pipe-stdio || $runtime_profile == dos-posix-pipe-stdio ) && $override == AROS_TEST_POSIX_LIBRARY ]];then continue;fi
        if [[ ( $runtime_profile == posix-pipe || $runtime_profile == posix-pipe-stdio || $runtime_profile == dos-posix-pipe-stdio ) && $override == AROS_TEST_PIPE_POLICY_HANDLER ]];then continue;fi
        if [[ ( $runtime_profile == posix-pipe-stdio || $runtime_profile == dos-posix-pipe-stdio ) && $override == AROS_TEST_STDCIO_LIBRARY ]];then continue;fi
        if [[ -n ${!override:-} ]];then
            echo "$runtime_profile runtime refuses OS overlay: $override" >&2;exit 2
        fi
    done
else
    dos=${AROS_TEST_DOS_OVERLAY:?private DOS required}
    ata=${AROS_TEST_ATA_OVERLAY:?private ATA required}
    posix=${AROS_TEST_POSIX_LIBRARY:?private POSIX required}
fi
if [[ $runtime_profile == dos-posix-ata ]];then
    dos=${AROS_TEST_DOS_OVERLAY:?explicit DOS overlay required}
    posix=${AROS_TEST_POSIX_LIBRARY:?explicit POSIX library required}
    ata=${AROS_TEST_ATA_OVERLAY:?explicit ATA overlay required}
fi
if [[ $runtime_profile == dos-only || $runtime_profile == dos-posix || $runtime_profile == dos-posix-pipe-stdio ]];then dos=${AROS_TEST_DOS_OVERLAY:?private DOS required};fi
if [[ $runtime_profile == posix-only || $runtime_profile == dos-posix || $runtime_profile == posix-pipe || $runtime_profile == posix-pipe-stdio || $runtime_profile == dos-posix-pipe-stdio ]];then posix=${AROS_TEST_POSIX_LIBRARY:?private POSIX required};fi
if [[ $runtime_profile == posix-pipe || $runtime_profile == posix-pipe-stdio || $runtime_profile == dos-posix-pipe-stdio ]];then : "${AROS_TEST_PIPE_POLICY_HANDLER:?disconnect-policy pipe handler required}";fi
if [[ $runtime_profile == posix-pipe-stdio || $runtime_profile == dos-posix-pipe-stdio ]];then : "${AROS_TEST_STDCIO_LIBRARY:?stdio ownership library required}";fi
cc=${AROS_CC:?explicit AROS compiler required}
sdk=${AROS_SDK:?explicit ABI-matched AROS SDK required}
base=${AROS_TEST_ISO:?explicit AROS test ISO required}
pthread=${AROS_PRIVATE_PTHREAD:?explicit ABI-matched pthread archive required}
pthread_include=${AROS_PTHREAD_INCLUDE:?explicit matching pthread headers required}
[[ -x $cc && -f $pthread && -f $pthread_include/pthread.h ]]
partition=${AROS_TEST_PARTITION_LIBRARY:-${sdk%/Developer}/Libs/partition.library}
qemu=${AROS_TEST_QEMU:-qemu-system-x86_64}
accel=${AROS_TEST_ACCEL:-kvm}
smp=${AROS_TEST_SMP:-2}
case "$smp" in 1|2|4|8|16) ;; *) echo 'AROS_TEST_SMP must be 1, 2, 4, 8 or 16' >&2;exit 2;; esac
case "$accel" in
    kvm) cpu=host ;;
    tcg) cpu=max ;;
    *) echo 'AROS_TEST_ACCEL must be kvm or tcg' >&2;exit 2 ;;
esac
gui=${AROS_TEST_GUI:-0}
memory=${AROS_TEST_MEMORY_MB:-2048}
case "$memory" in 2048|4096|8192) ;; *) echo 'AROS_TEST_MEMORY_MB must be 2048, 4096 or 8192' >&2;exit 2;; esac
[[ $gui == 0 || $gui == 1 || $gui == 2 || $gui == 3 ]]
if [[ $gui == 3 ]];then [[ ${AROS_TEST_NATIVE_WRITABLE:-0} == 1 ]];fi
members=${AROS_TEST_NATIVE_MEMBERS:-both}
layout=${AROS_TEST_NATIVE_LAYOUT:-mirror}
[[ $layout == mirror || $layout == raidz1 || $layout == raidz2 || $layout == raidz3 ]]
units=(0 1)
if [[ $layout != mirror ]];then units+=(3);fi
if [[ $layout == raidz3 ]];then units+=(4);fi
gpt=${AROS_TEST_GPT:-0}
writable=${AROS_TEST_NATIVE_WRITABLE:-0}
labelclear_only=${AROS_TEST_NATIVE_LABELCLEAR_ONLY:-0}
[[ $labelclear_only == 0 || $labelclear_only == 1 ]]
dataset_admin_only=${AROS_TEST_DATASET_ADMIN_ONLY:-0}
[[ $dataset_admin_only == 0 || $dataset_admin_only == 1 ]]
live_create_only=${AROS_TEST_LIVE_CREATE_ONLY:-0}
[[ $live_create_only == 0 || $live_create_only == 1 ]]
if (( labelclear_only + dataset_admin_only + live_create_only > 1 ));then
    echo 'NATIVE_LABELCLEAR_ONLY, DATASET_ADMIN_ONLY and LIVE_CREATE_ONLY are mutually exclusive' >&2
    exit 2
fi
if [[ $dataset_admin_only == 1 && ${AROS_TEST_ZVOL_TRANSPORT:-0} != 1 ]];then
    echo 'DATASET_ADMIN_ONLY requires AROS_TEST_ZVOL_TRANSPORT=1' >&2
    exit 2
fi
native_bus=${AROS_TEST_NATIVE_BUS:-ide}
usb_controller=${AROS_TEST_USB_CONTROLLER:-pci-ohci}
case "$usb_controller" in pci-ohci|qemu-xhci) ;; *) echo 'Unsupported USB controller' >&2;exit 2;; esac
usb_remove=${AROS_TEST_USB_REMOVE:-0}
usb_reinsert=${AROS_TEST_USB_REINSERT:-0}
usb_all_lost=${AROS_TEST_USB_ALL_LOST:-0}
usb_all_lost_writable=${AROS_TEST_USB_ALL_LOST_WRITABLE:-0}
usb_suspend_guard=${AROS_TEST_USB_SUSPEND_GUARD:-0}
usb_swap=${AROS_TEST_USB_SWAP_MEMBERS:-0}
usb_rounds=${AROS_TEST_USB_RECOVERY_ROUNDS:-1}
usb_created=${AROS_TEST_USB_CREATED_RECOVERY:-0}
usb_inflight=${AROS_TEST_USB_INFLIGHT_WRITE:-0}
usb_cli_clear=${AROS_TEST_USB_CLI_CLEAR:-0}
import_rewind=${AROS_TEST_IMPORT_REWIND:-0}
[[ $import_rewind == 0 || $import_rewind == 1 ]]
import_rewind_mode=${AROS_TEST_IMPORT_REWIND_MODE:-preview}
clear_rewind=${AROS_TEST_CLEAR_REWIND:-0}
clear_assessment_only=${AROS_TEST_CLEAR_ASSESSMENT_ONLY:-0}
clear_gui=${AROS_TEST_CLEAR_GUI:-0}
[[ $clear_gui == 0 || ( $clear_gui == 1 && $clear_assessment_only == 1 && $gui == 1 ) ]]
[[ $clear_assessment_only == 0 || ( $clear_assessment_only == 1 && $clear_rewind == 1 ) ]]
clear_manager=${AROS_TEST_CLEAR_MANAGER:-0}
[[ $clear_manager == 0 || ( $clear_manager == 1 && $clear_rewind == 1 && -z ${AROS_TEST_WHOLE_CLI_LINK:-} ) ]]
[[ $clear_rewind == 0 || ( $clear_rewind == 1 && $import_rewind == 1 && $import_rewind_mode == commit ) ]]
import_rewind_manager=${AROS_TEST_IMPORT_REWIND_MANAGER:-0}
import_rewind_cut=${AROS_TEST_IMPORT_REWIND_CUT:-0}
[[ $clear_rewind == 0 || $import_rewind_manager == 0 ]]
[[ $clear_rewind == 0 || $import_rewind_cut == 0 || ( $clear_assessment_only == 0 && $clear_manager == 0 && $clear_gui == 0 ) ]]
[[ $import_rewind_cut == 0 || ( $import_rewind_cut == 1 && $import_rewind == 1 && $import_rewind_mode == commit ) ]]
import_rewind_normal=${AROS_TEST_IMPORT_REWIND_NORMAL:-0}
import_assessment=${AROS_TEST_IMPORT_ASSESSMENT:-0}
import_assess_healthy=${AROS_TEST_IMPORT_ASSESS_HEALTHY:-0}
[[ $import_assessment == 0 || ( $import_assessment == 1 && $import_rewind_manager == 1 && $import_rewind == 1 && $import_rewind_mode == preview ) ]]
[[ $import_assess_healthy == 0 || ( $import_assess_healthy == 1 && $import_assessment == 1 ) ]]
import_rewind_dryrun=${AROS_TEST_IMPORT_REWIND_DRYRUN:-0}
[[ $import_rewind_dryrun == 0 || ( $import_rewind_dryrun == 1 && $import_rewind == 1 && $import_rewind_mode == preview ) ]]
import_rewind_refuse_normal=${AROS_TEST_IMPORT_REWIND_REFUSE_NORMAL:-0}
[[ $import_assess_healthy == 0 || ( $import_rewind_dryrun == 0 && $import_rewind_refuse_normal == 0 ) ]]
[[ $import_rewind_refuse_normal == 0 || ( $import_rewind_refuse_normal == 1 && $import_rewind == 1 && $import_rewind_mode != cold && $import_rewind_normal == 0 ) ]]
[[ $import_rewind_normal == 0 || ( $import_rewind_normal == 1 && $import_rewind == 1 && $import_rewind_mode != cold ) ]]
import_rewind_flag=FX
if [[ $import_rewind_normal == 1 ]];then import_rewind_flag=F;fi
[[ $import_rewind_manager == 0 || ( $import_rewind_manager == 1 && $import_rewind == 1 && $import_rewind_mode != cold ) ]]
[[ $import_rewind_mode == preview || $import_rewind_mode == commit || $import_rewind_mode == cold || $import_rewind_mode == seed ]]
if [[ $import_rewind_mode == seed ]];then
    [[ $import_rewind_manager == 0 && $import_rewind_normal == 0 && $import_rewind_refuse_normal == 0 ]]
fi
[[ $import_rewind == 1 || $import_rewind_mode == preview ]]
if [[ $import_rewind == 1 ]];then
    [[ $native_bus == ide && $members == both && $layout == mirror && $writable == 0 && $gpt == 0 && ( $gui == 0 || $clear_gui == 1 ) && -z ${AROS_TEST_WHOLE_CLI_LINK:-} ]]
    [[ -f ${AROS_TEST_CLI_DIR:?rewind test needs packaged CLI}/zpool ]]
fi
[[ $usb_cli_clear == 0 || $usb_cli_clear == 1 ]]
if [[ $usb_cli_clear == 1 ]];then
    [[ $usb_all_lost_writable == 1 && -z ${AROS_TEST_WHOLE_CLI_LINK:-} ]]
    [[ -f ${AROS_TEST_CLI_DIR:?CLI clear needs current packaged binaries}/zpool ]]
fi
[[ $usb_created == 0 || $usb_created == 1 ]]
[[ $usb_inflight == 0 || $usb_inflight == 1 ]]
if [[ $usb_created == 1 ]];then
    [[ $native_bus == usb && $usb_all_lost_writable == 1 && $usb_rounds == 1 && $usb_swap == 0 && $usb_suspend_guard == 0 && $writable == 0 ]]
fi
if [[ $usb_inflight == 1 ]];then
    [[ $native_bus == usb && $usb_controller == qemu-xhci &&
        $usb_all_lost_writable == 1 && $usb_rounds == 1 &&
        $usb_swap == 0 && $usb_suspend_guard == 0 &&
        $usb_created == 0 && $writable == 0 ]]
fi
[[ $usb_rounds =~ ^[1-3]$ ]]
if [[ $usb_rounds != 1 ]];then
    [[ $usb_all_lost_writable == 1 && $usb_suspend_guard == 0 && $usb_swap == 0 ]]
fi
[[ $usb_swap == 0 || $usb_swap == 1 ]]
if [[ $usb_swap == 1 ]];then [[ $usb_all_lost_writable == 1 && $usb_suspend_guard == 0 ]];fi
[[ $usb_suspend_guard == 0 || $usb_suspend_guard == 1 ]]
if [[ $usb_suspend_guard == 1 ]];then [[ $usb_all_lost_writable == 1 ]];fi
[[ $usb_all_lost_writable == 0 || $usb_all_lost_writable == 1 ]]
if [[ $usb_all_lost_writable == 1 ]];then [[ $usb_all_lost == 1 && $usb_remove == 1 && $usb_reinsert == 1 ]];fi
[[ $usb_all_lost == 0 || $usb_all_lost == 1 ]]
if [[ $usb_all_lost == 1 ]];then [[ $usb_reinsert == 1 ]];fi
[[ $usb_reinsert == 0 || $usb_reinsert == 1 || $usb_reinsert == 2 ]]
if [[ $usb_reinsert != 0 ]];then [[ $usb_remove == 1 ]];fi
usb_cold=${AROS_TEST_USB_COLD_SOURCE:-}
usb_cold_kind=${AROS_TEST_USB_COLD_KIND:-survivor}
[[ $usb_cold_kind == survivor || $usb_cold_kind == acknowledged ]]
if [[ $usb_cold_kind == acknowledged ]];then [[ -n $usb_cold ]];fi
if [[ -n $usb_cold ]];then
    usb_cold=$(realpath -e "$usb_cold")
    case "$usb_cold" in "$artifact_root"/native-import-run.*) ;; *) exit 2;; esac
    [[ $native_bus == usb && $usb_remove == 0 ]]
    if [[ $usb_cold_kind == acknowledged ]];then
        ack_chunks=$(sed -n 's/^RESULT native USB acknowledged writes PASS chunks=\([0-9]*\) bytes=.*readonly-reimport=1 exact-prefix=1\r*$/\1/p' "$usb_cold/serial.log")
        [[ $ack_chunks =~ ^[1-9][0-9]{0,3}$ ]] && ((ack_chunks<=2048))
        rg -q '^RESULT native CLI suspended clear PASS returned=0' "$usb_cold/serial.log"
    else
        rg -q 'RESULT native USB degraded write PASS flush=1 survivor-reimport=1 exact-read=1' "$usb_cold/serial.log"
    fi
    rg -qx 'OZ-NATIVE-COMPLETION-v1 PASS line=0' "$usb_cold/completion.log"
    sha256sum -c "$usb_cold/backing.sha256"
fi
[[ $usb_remove == 0 || $usb_remove == 1 || $usb_remove == 2 ]]
if [[ $usb_remove != 0 ]];then [[ $native_bus == usb ]];fi
[[ $native_bus == ide || $native_bus == ahci || $native_bus == usb ]]
if [[ $native_bus == usb ]];then
    [[ $layout == mirror && $members == both && $gpt == 0 && $gui == 0 ]]
    [[ ${AROS_TEST_ZVOL_DEVICE:-0} == 0 ]]
    if [[ $writable == 1 ]];then
        # Keep the first writable USB proof isolated from hotplug recovery and
        # the separately tracked pci-ohci completion/allocator deadlock.
        [[ $usb_controller == qemu-xhci && ${AROS_TEST_HISTORY_RACE:-0} == 1 && $usb_remove == 0 &&
            $usb_reinsert == 0 && $usb_all_lost == 0 &&
            $usb_all_lost_writable == 0 && $usb_suspend_guard == 0 &&
            $usb_swap == 0 && $usb_created == 0 && -z $usb_cold ]]
    elif [[ $writable == 2 ]];then
        # A cold USB proof consumes the retained dataset and command history
        # from an independently completed xHCI writer run.  Keep recovery and
        # hotplug state out of this topology so the second boot is read-only.
        [[ $usb_controller == qemu-xhci && ${AROS_TEST_HISTORY_RACE:-0} == 0 &&
            ${AROS_TEST_COMMAND_HISTORY:-0} == 1 && ${AROS_TEST_CLI_POOL_CREATE:-0} == 1 &&
            -n ${AROS_TEST_WHOLE_CLI_LINK:-} && $usb_remove == 0 &&
            $usb_reinsert == 0 && $usb_all_lost == 0 &&
            $usb_all_lost_writable == 0 && $usb_suspend_guard == 0 &&
            $usb_swap == 0 && $usb_created == 0 && -z $usb_cold ]]
    else
        # Existing removal/recovery fixtures manage their own write phases.
        [[ $writable == 0 ]]
    fi
fi
if [[ $native_bus == ahci ]];then
    [[ $layout == mirror && $members == both && $writable == 1 && $gpt == 0 &&
        $gui == 0 && ${AROS_TEST_HISTORY_RACE:-0} == 1 ]]
fi
draid_persist=${AROS_TEST_DRAID_PERSIST:-none}
draid_expand=${AROS_TEST_DRAID_EXPAND_PARITY:-0}
[[ $draid_expand == 0 || $draid_expand == 1 || $draid_expand == 2 || $draid_expand == 3 ]]
draid_members=(a b c)
if [[ $draid_expand != 0 ]];then
    [[ $draid_persist != none && ${AROS_TEST_TYPED_DRAID:-0} == 1 ]]
    draid_members=()
    for ((member=0;member<(draid_expand+2)*2;member++));do draid_members+=("$member");done
fi
[[ $draid_persist == none || $draid_persist == write || $draid_persist == read ]]
if [[ $draid_persist != none ]];then
    [[ ${AROS_TEST_CLI_DRAID:-0} == 1 && $writable == 1 && $layout == mirror && $members == both && $gui == 0 && $gpt == 0 ]]
    [[ ${AROS_TEST_NATIVE_CREATE:-0} == 0 && ${AROS_TEST_NATIVE_ADD:-0} == 0 && ${AROS_TEST_NATIVE_REPLACE:-0} == 0 && ${AROS_TEST_NATIVE_SPLIT:-0} == 0 ]]
fi
keep_names=${AROS_TEST_KEEP_NAMES:-0}
keep_cli=${AROS_TEST_KEEP_CLI:-0}
split_keep=${AROS_TEST_NATIVE_SPLIT_KEEP:-0}
split_cold=${AROS_TEST_NATIVE_SPLIT_COLD:-0}
create_cold=${AROS_TEST_NATIVE_CREATE_COLD:-0}
[[ $create_cold == 0 || ( $create_cold == 1 && $writable == 2 && $layout == mirror && $members == both && -n ${AROS_TEST_WHOLE_CLI_LINK:-} && $split_cold == 0 && ${AROS_TEST_NATIVE_CREATE:-0} == 0 && ${AROS_TEST_NATIVE_ADD:-0} == 0 && ${AROS_TEST_NATIVE_REPLACE:-0} == 0 && ${AROS_TEST_NATIVE_SPLIT:-0} == 0 ) ]]
[[ $split_keep == 0 || ( $split_keep == 1 && ${AROS_TEST_NATIVE_SPLIT:-0} == 1 && $keep_cli == 1 ) ]]
[[ $split_cold == 0 || ( $split_cold == 1 && $writable == 2 && $layout == mirror && $members == both && -n ${AROS_TEST_WHOLE_CLI_LINK:-} && ${AROS_TEST_NATIVE_SPLIT:-0} == 0 && ${AROS_TEST_NATIVE_ADD:-0} == 0 && ${AROS_TEST_NATIVE_REPLACE:-0} == 0 ) ]]
[[ $keep_cli == 0 || ( $keep_cli == 1 && $writable == 1 && -n ${AROS_TEST_WHOLE_CLI_LINK:-} ) ]]
[[ $keep_names == 0 || ( $keep_names == 1 && $gui == 0 && ( $writable == 1 || $writable == 2 ) ) ]]
[[ ( $gui != 2 && $gui != 3 ) || ( $writable == 1 && $layout == mirror && $members == both && $gpt == 0 ) ]]
[[ $writable == 0 || ( ( $writable == 1 || $writable == 2 ) && $gpt == 0 && ( $gui == 0 || $gui == 1 || $gui == 2 || $gui == 3 ) &&
    ( $members == both || $members == 0 || $members == 1-empty-peer || $members == 01 ||
      ( $layout == raidz3 && $members == 4 && $writable == 2 ) ) ) ]]
if [[ $layout != mirror ]];then
    [[ $gpt == 0 && $gui == 0 && ( $members == both || $members == 01 ||
        ( ( $layout == raidz2 || $layout == raidz3 ) && $members == 0 && $writable == 2 ) ||
        ( $layout == raidz3 && $members == 4 && $writable == 2 ) ) ]]
    fixture_count=three
    if [[ $layout == raidz3 ]];then fixture_count=four;fi
    rg -q "^PASS: native $fixture_count-device ${layout^^} fixture and DOS tests:" "$fixture/runner.log"
    sha256sum -c "$fixture/created.sha256"
else
    [[ $members != 01 ]]
fi
verify_run=
if [[ $writable == 2 ]];then
    verify_run=$(realpath -e "${AROS_TEST_NATIVE_VERIFY_RUN:?successful writable run required}")
    case "$verify_run" in "$repo"/build-aros/native-import-run.*|"$artifact_root"/native-import-run.*) ;; *) exit 2;; esac
    rg -q "^PASS: native writable $layout import/DOS write/export/reimport:" "$verify_run/runner.log"
    if [[ $native_bus == usb ]];then
        rg -Fxq 'Native storage bus: usb' "$verify_run/runner.log"
        rg -Fxq 'USB controller: qemu-xhci' "$verify_run/runner.log"
    fi
    if [[ -n ${AROS_TEST_WHOLE_CLI_LINK:-} && $create_cold == 0 ]];then
        rg -q 'RESULT native CLI retained PASS dataset=qemu_mirror/cli-import' "$verify_run/serial.log"
    fi
    if [[ $create_cold == 1 ]];then
        rg -q 'RESULT native CLI partition create PASS native-only=1 normalized-read=1 readonly=1 exported=1 payload-bytes=1048576' "$verify_run/serial.log"
        rg -q 'No problems found' "$verify_run/add-partition-result-verify.log"
        rg -q 'No leaks' "$verify_run/create-zdb.log"
    fi
    if [[ $split_cold == 1 ]];then
        rg -q 'RESULT native CLI split retained PASS pool=cli_split_new member=/AROS/ata.device/3/partition/0 payload-bytes=1048576' "$verify_run/serial.log"
        rg -q 'No problems found' "$verify_run/add-partition-result-verify.log"
    fi
    if [[ $keep_names == 1 ]];then
        rg -q '^RESULT native namespace retained PASS' "$verify_run/serial.log"
        rg -q '^RESULT native reopened namespace PASS' "$verify_run/serial.log"
    fi
    if ! rg -Fxq "Native members attached: $members" "$verify_run/runner.log";then
        if [[ ( $layout == raidz2 && $members == 0 ) ||
              ( $layout == raidz3 && ( $members == 0 || $members == 4 ) ) ]];then
            # A healthy writer supplies the backing chain. Only unit 0 is
            # attached in this independent, strictly read-only recovery boot.
            rg -Fxq 'Native members attached: both' "$verify_run/runner.log"
        else
            [[ $layout == mirror ]]
            rg -Fxq "Native mirror members attached: $members" "$verify_run/runner.log"
        fi
    fi
    if [[ $layout != mirror ]];then rg -Fxq "Native pool layout: $layout" "$verify_run/runner.log";fi
fi
gui_gpt=${AROS_TEST_GUI_GPT:-0}
[[ $gui_gpt == 0 || ( $gui_gpt == 1 && $gui == 1 && $gpt == 0 && $members == both ) ]]
[[ $gpt == 0 || ( ( $gpt == 1 || $gpt == 2 || $gpt == 3 ) && $members == both && $gui == 0 ) ]]
[[ $members == both || $members == 0 || $members == 1 || $members == 1-empty-peer || $members == 01 ||
   ( $layout == raidz3 && $members == 4 && $writable == 2 ) ]]
drives=()
gui_flags=();gui_maps=();gui_devices=()
if [[ $native_bus == usb ]];then
    gui_flags+=(-DOZ_NATIVE_USB_MEMBERS)
    if [[ $usb_created == 1 ]];then gui_flags+=(-DOZ_NATIVE_USB_CREATED_RECOVERY);fi
    gui_flags+=(-DOZ_NATIVE_USB_RECOVERY_ROUNDS="$usb_rounds")
    if [[ $usb_remove != 0 ]];then gui_flags+=(-DOZ_NATIVE_USB_REMOVE);fi
    if [[ $usb_remove == 2 ]];then gui_flags+=(-DOZ_NATIVE_USB_REMOVE_WRITABLE);fi
    if [[ $usb_reinsert != 0 ]];then gui_flags+=(-DOZ_NATIVE_USB_REINSERT);fi
    if [[ $usb_reinsert == 2 ]];then gui_flags+=(-DOZ_NATIVE_USB_RELOCATE);fi
    if [[ $usb_all_lost == 1 ]];then gui_flags+=(-DOZ_NATIVE_USB_ALL_LOST);fi
    if [[ $usb_all_lost_writable == 1 ]];then gui_flags+=(-DOZ_NATIVE_USB_ALL_LOST_WRITABLE);fi
    if [[ $usb_suspend_guard == 1 ]];then gui_flags+=(-DOZ_NATIVE_USB_SUSPEND_GUARD);fi
    if [[ $usb_swap == 1 ]];then gui_flags+=(-DOZ_NATIVE_USB_SWAP_MEMBERS);fi
    if [[ $usb_inflight == 1 ]];then gui_flags+=(-DOZ_NATIVE_USB_INFLIGHT_WRITE);fi
    if [[ -n $usb_cold ]];then
        if [[ $usb_cold_kind == acknowledged ]];then
            gui_flags+=(-DOZ_NATIVE_USB_COLD_ACK_CHUNKS="$ack_chunks")
        else
            gui_flags+=(-DOZ_NATIVE_USB_COLD -DOZ_NATIVE_IMPORT_DEGRADED)
        fi
    fi
fi
if [[ $native_bus == ahci ]];then
    gui_flags+=(-DOZ_NATIVE_AHCI_MEMBERS -DOZ_CLIENT_TIMEOUT_SECONDS=10)
fi
case ${AROS_TEST_SERVICE_RELEASE:-0} in
    0) ;;
    1) gui_flags+=(-DOZ_NATIVE_SERVICE_RELEASE) ;;
    *) echo 'AROS_TEST_SERVICE_RELEASE must be 0 or 1' >&2;exit 2 ;;
esac
if [[ $gui != 0 ]];then gui_devices=(-usb -device usb-tablet);fi
if [[ $layout != mirror ]];then gui_flags+=(-DOZ_NATIVE_IMPORT_RAIDZ);fi
if [[ $layout == raidz3 ]];then gui_flags+=(-DOZ_NATIVE_IMPORT_RAIDZ3);fi
if [[ $writable == 1 ]];then gui_flags+=(-DOZ_NATIVE_WRITABLE_TEST);fi
if [[ $writable == 2 ]];then gui_flags+=(-DOZ_NATIVE_VERIFY_WRITES);fi
if [[ $keep_names == 1 ]];then gui_flags+=(-DOZ_NATIVE_KEEP_NAMES);fi
if [[ -n ${AROS_TEST_FS_PACKAGE:-} ]];then
    gui_maps+=(-map "$AROS_TEST_FS_PACKAGE" /boot/aros-fs.pkg.xz)
fi
if [[ $gpt == 1 || $gpt == 3 ]];then gui_flags+=(-DOZ_NATIVE_GPT_TEST);fi
if [[ $gpt == 2 ]];then gui_flags+=(-DOZ_NATIVE_GPT_READONLY);fi
if [[ $members != both ]];then gui_flags+=(-DOZ_NATIVE_IMPORT_DEGRADED);fi
run=$(mktemp -d "$artifact_root/native-import-run.XXXXXXXX")
mkdir "$run/tmp"
export TMPDIR="$run/tmp"
if [[ -n ${AROS_TEST_STDCIO_LIBRARY:-} ]];then
    cp "$AROS_TEST_STDCIO_LIBRARY" "$run/stdcio.library"
    sha256sum "$run/stdcio.library" >"$run/stdcio.sha256"
    gui_maps+=(-map "$run/stdcio.library" /Libs/stdcio.library)
fi
echo "Evidence: $run"
exec > >(tee "$run/runner.log") 2>&1
echo "Native members attached: $members"
echo "Native pool layout: $layout"
echo "Native storage bus: $native_bus"
echo "QEMU acceleration: $accel CPU: $cpu"
echo "Guest virtual CPUs: $smp"
echo "OS runtime profile: $runtime_profile"
if [[ -n $bundle ]];then
    echo "Native bundle: $bundle"
    cp "$bundle/SHA256SUMS" "$run/bundle.sha256"
    (cd "$bundle" && sha256sum -c SHA256SUMS) >"$run/bundle-verify.log"
fi
git rev-parse HEAD >"$run/source-head.txt"
git diff --binary >"$run/source.patch"
cp "$0" aros/engine/native-import-test.c aros/engine/Startup-Native-Import "$run/"
sha256sum "$cc" "$pthread" "$pthread_include/pthread.h" > "$run/client-toolchain.sha256"
if [[ $import_rewind == 1 ]];then
    cp "$AROS_TEST_CLI_DIR/zpool" "$run/zpool"
    sha256sum "$run/zpool" > "$run/rewind-cli.sha256"
    gui_maps+=(-map "$run/zpool" /C/zpool)
    gui_flags+=(-DOZ_NATIVE_IMPORT_REWIND)
    if [[ $clear_rewind == 1 ]];then gui_flags+=(-DOZ_NATIVE_CLEAR_REWIND);fi
    if [[ $clear_assessment_only == 1 ]];then gui_flags+=(-DOZ_NATIVE_CLEAR_ASSESSMENT_ONLY);fi
    if [[ $clear_gui == 1 ]];then gui_flags+=(-DOZ_NATIVE_CLEAR_GUI);fi
    if [[ $clear_rewind == 1 && $import_rewind_cut == 1 ]];then gui_flags+=(-DOZ_NATIVE_CLEAR_CUT);fi
    if [[ $clear_manager == 1 ]];then gui_flags+=(-DOZ_NATIVE_CLEAR_MANAGER);fi
    if [[ $import_rewind_mode == seed ]];then gui_flags+=(-DOZ_NATIVE_IMPORT_REWIND_SEED);fi
    if [[ $import_rewind_normal == 1 ]];then gui_flags+=(-DOZ_NATIVE_IMPORT_REWIND_NORMAL);fi
    if [[ $import_assessment == 1 ]];then gui_flags+=(-DOZ_NATIVE_IMPORT_ASSESSMENT);fi
    if [[ $import_assess_healthy == 1 ]];then gui_flags+=(-DOZ_NATIVE_IMPORT_ASSESS_HEALTHY);fi
    if [[ $import_rewind_dryrun == 1 ]];then gui_flags+=(-DOZ_NATIVE_IMPORT_REWIND_DRYRUN);fi
    if [[ $import_rewind_refuse_normal == 1 ]];then gui_flags+=(-DOZ_NATIVE_IMPORT_REWIND_REFUSE_NORMAL);fi
    if [[ $import_rewind_manager == 1 ]];then gui_flags+=(-DOZ_NATIVE_IMPORT_REWIND_MANAGER);fi
    if [[ $import_rewind_mode == commit ]];then gui_flags+=(-DOZ_NATIVE_IMPORT_REWIND_COMMIT);fi
    if [[ $import_rewind_mode == cold ]];then gui_flags+=(-DOZ_NATIVE_IMPORT_REWIND_COLD);fi
    printf '%s\n' "$import_rewind_mode" > "$run/rewind-mode.txt"
fi
if [[ $usb_cli_clear == 1 ]];then
    cp "$AROS_TEST_CLI_DIR/zpool" "$run/zpool"
    sha256sum "$run/zpool" > "$run/clear-cli.sha256"
    gui_maps+=(-map "$run/zpool" /C/zpool)
    gui_flags+=(-DOZ_NATIVE_USB_CLI_CLEAR)
fi
if [[ $native_bus == usb ]];then
    cp aros/engine/Startup-Native-USB-Import "$run/Startup-Native-Import"
fi
if [[ $draid_persist != none ]];then
    cp aros/engine/Startup-DRAID-Import "$run/Startup-Native-Import"
    cp aros/engine/OZBACK-DRAID "$run/OZBACK"
    if [[ $draid_expand != 0 ]];then cp aros/engine/OZBACK-DRAID-EXPAND "$run/OZBACK";fi
    gui_maps+=(-map "$run/OZBACK" /OZBACK)
    if [[ $draid_persist == write ]];then
        truncate -s 512M "$run/draid-backing.raw"
        if [[ $draid_expand != 0 ]];then truncate -s 2G "$run/draid-backing.raw";fi
        mkfs.vfat -F 16 -n OZFILES "$run/draid-backing.raw" >"$run/draid-format.log" 2>&1
        truncate -s 128M "$run/blank-draid-member.raw"
        for member in "${draid_members[@]}";do
            mcopy -i "$run/draid-backing.raw" "$run/blank-draid-member.raw" "::cli-draid-$member.vdev"
        done
    else
        draid_source=$(realpath -e "${AROS_TEST_DRAID_SOURCE:?successful private dRAID writer required}")
        case "$draid_source" in "$repo"/build-aros/native-import-run.*|"$artifact_root"/native-import-run.*) ;; *) exit 2;; esac
        rg -q '^PASS: native readonly mirror import and DOS read;' "$draid_source/runner.log"
        rg -q '^PASS: persistent dRAID write' "$draid_source/runner.log"
        if [[ $draid_expand != 0 ]];then
            rg -q "RESULT native typed dRAID expansion retained PASS parity=$draid_expand members=${#draid_members[@]} " "$draid_source/serial.log"
        else
            rg -q 'RESULT native CLI dRAID retained PASS active-spare=1' "$draid_source/serial.log"
        fi
        sha256sum -c "$draid_source/draid-retained.sha256"
        cp --reflink=auto "$draid_source/draid-backing.raw" "$run/draid-backing.raw"
        sha256sum "$draid_source/draid-backing.raw" >"$run/draid-source.sha256"
    fi
    drives+=(-drive "file=$run/draid-backing.raw,format=raw,if=ide,index=3,aio=threads")
fi
if [[ ${AROS_TEST_SPLIT_RETAINED:-0} == 1 ]];then
    cp aros/engine/split-retained-test.c "$run/"
fi
cp aros/ui/startup-import.c aros/ui/startup-import.h aros/ui/startup-settings.c aros/ui/startup-settings.h aros/ui/startup-settings-file.c aros/ui/settings-file.h aros/ui/preferences.c aros/ui/preferences-file.c "$run/"
cp aros/ui/gpt-api.h aros/ui/zvol-api.h aros/ui/project-api.h aros/ui/admission-client.c aros/ui/admission-worker.c aros/ui/admission-worker.h "$run/"
sha256sum aros/ui/gpt-api.h aros/ui/zvol-api.h aros/ui/project-api.h aros/ui/admission-client.c aros/ui/admission-worker.c aros/ui/admission-worker.h aros/engine/admission.c >"$run/api-inputs.sha256"
cp "$service" "$run/OpenZFSServiceNative"
inventory_objects=()
if [[ -n ${AROS_TEST_DEVICE_INVENTORY:-} ]];then
    cp aros/ui/inventory-worker-test.c "$run/"
    inventory_objects+=(aros/ui/inventory-worker-test.c)
    cp "$AROS_TEST_DEVICE_INVENTORY" "$run/OpenZFSInventory"
    gui_flags+=(-DOZ_NATIVE_INVENTORY)
    gui_maps+=(-map "$run/OpenZFSInventory" /C/OpenZFSInventory)
    sha256sum "$run/OpenZFSInventory" >"$run/inventory.sha256"
fi
if [[ -n ${AROS_TEST_ENGINE_LIBRARY:-} ]];then
    cp "$AROS_TEST_ENGINE_LIBRARY" "$run/openzfs.library"
    if [[ -n ${AROS_TEST_ENGINE_LIBRARY_LIFETIME:-} ]];then
        cp "$AROS_TEST_ENGINE_LIBRARY_LIFETIME" "$run/OpenZFSLibraryTest"
        gui_flags+=(-DOZ_NATIVE_ENGINE_LIBRARY_LIFETIME)
        gui_maps+=(-map "$run/OpenZFSLibraryTest" /C/OpenZFSLibraryTest)
    fi
    gui_maps+=(-map "$run/openzfs.library" /Libs/openzfs.library)
fi
if [[ -n ${AROS_TEST_HANDLER_BINARY:-} ]];then
    cp "$AROS_TEST_HANDLER_BINARY" "$run/OpenZFS-Handler"
    sha256sum "$run/OpenZFS-Handler" >"$run/handler.sha256"
    if [[ $writable == 1 && $layout == mirror && $gui == 0 ]];then
        gui_flags+=(-DOZ_NATIVE_HANDLER_STARTUP)
    fi
    gui_maps+=(-map "$run/OpenZFS-Handler" /L/OpenZFS-Handler)
fi
pressure=${AROS_TEST_DOS_CACHE_PRESSURE:-0}
[[ $pressure == 0 || $pressure == 1 ]]
if [[ $pressure == 1 ]];then
    [[ -n ${AROS_TEST_HANDLER_BINARY:-} && -n ${AROS_TEST_WHOLE_CLI_LINK:-} && $writable == 1 && $layout == mirror && $gui == 0 ]]
    gui_flags+=(-DOZ_NATIVE_DOS_CACHE_PRESSURE)
fi
if [[ -n ${AROS_TEST_WHOLE_CLI_LINK:-} ]];then
    # GUI mode 1 inspects the completed regression; modes 2/3 own pool changes.
    [[ ( $writable == 1 || $writable == 2 ) && ( $gui == 0 || $gui == 1 ) && $gpt == 0 && $layout == mirror && $members == both ]]
    rg -Fxq 'PASS zfs link' "$AROS_TEST_WHOLE_CLI_LINK/summary.txt"
    rg -Fxq 'PASS zpool link' "$AROS_TEST_WHOLE_CLI_LINK/summary.txt"
    cli_binaries=${bundle:+$bundle/C}
    cli_binaries=${cli_binaries:-$AROS_TEST_WHOLE_CLI_LINK}
    cp "$cli_binaries/zfs" "$cli_binaries/zpool" "$run/"
    cp aros/cli/service-command-test.c "$run/"
    cp aros/ui/iostat-rates.h aros/ui/iostat-refresh.h aros/ui/iostat-rates-test.c "$run/"
    cli_flags=()
    history_wire_flags=()
    if [[ $native_bus == usb ]];then
        cli_flags+=(-DOZ_TEST_USB_MEMBERS)
    elif [[ $native_bus == ahci ]];then
        cli_flags+=(-DOZ_TEST_AHCI_MEMBERS -DOZ_CLIENT_TIMEOUT_SECONDS=10)
    fi
    if [[ ${AROS_TEST_MANAGER_VDEV_GUI:-0} == 1 ]];then
        [[ $gui == 1 && ${AROS_TEST_MANAGER_VDEV_PROPERTIES:-0} == 1 ]]
        cli_flags+=(-DOZ_TEST_MANAGER_VDEV_GUI)
    fi
    if [[ ${AROS_TEST_MANAGER_VDEV_PROPERTIES:-0} == 1 ]];then
        [[ ${AROS_TEST_VDEV_PROPERTIES:-0} == 1 ]]
    fi
    if [[ ${AROS_TEST_VDEV_PROPERTIES:-0} == 1 ]];then
        [[ ${AROS_TEST_CLI_POOL_CREATE:-0} == 1 ]]
        cli_flags+=(-DOZ_TEST_VDEV_PROPERTIES)
        if [[ ${AROS_TEST_MANAGER_VDEV_PROPERTIES:-0} == 1 ]];then
            cli_flags+=(-DOZ_TEST_MANAGER_VDEV_PROPERTIES)
        fi
    fi
    if [[ ${AROS_TEST_COMMAND_HISTORY:-0} == 1 ]];then
        [[ ${AROS_TEST_CLI_POOL_CREATE:-0} == 1 ]]
        cli_flags+=(-DOZ_TEST_COMMAND_HISTORY)
        if [[ -n ${AROS_TEST_COMMAND_HISTORY_BINARY:-} ]];then
            cp "$AROS_TEST_COMMAND_HISTORY_BINARY" "$run/OpenZFSCommandHistoryTest"
            sha256sum "$run/OpenZFSCommandHistoryTest" >"$run/command-history-test.sha256"
            gui_maps+=(-map "$run/OpenZFSCommandHistoryTest" /C/OpenZFSCommandHistoryTest)
            cli_flags+=(-DOZ_TEST_COMMAND_HISTORY_ISOLATION)
        fi
    fi
    if [[ ${AROS_TEST_HISTORY_MEGARECORD:-0} == 1 ]];then
        [[ ${AROS_TEST_CLI_POOL_CREATE:-0} == 1 && ${AROS_TEST_COMMAND_HISTORY:-0} == 1 &&
            -n ${AROS_TEST_COMMAND_HISTORY_BINARY:-} && $memory -ge 8192 &&
            ${AROS_TEST_LARGE_HISTORY:-0} == 0 && ${AROS_TEST_HISTORY_RACE:-0} == 0 ]]
        cli_flags+=(-DOZ_TEST_HISTORY_MEGARECORD)
        history_wire_flags+=(-DOZ_TEST_HISTORY_MEGARECORD)
    fi
    if [[ ${AROS_TEST_LARGE_HISTORY:-0} == 1 ]];then
        [[ ${AROS_TEST_CLI_POOL_CREATE:-0} == 1 && $memory -ge 4096 &&
            ${AROS_TEST_HISTORY_RACE:-0} == 0 ]]
        cli_flags+=(-DOZ_TEST_LARGE_HISTORY)
    fi
    if [[ ${AROS_TEST_HISTORY_RACE:-0} == 1 ]];then
        [[ ${AROS_TEST_CLI_POOL_CREATE:-0} == 1 && ${AROS_TEST_COMMAND_HISTORY:-0} == 1 &&
            -n ${AROS_TEST_COMMAND_HISTORY_BINARY:-} && $memory -ge 4096 &&
            ${AROS_TEST_HISTORY_MEGARECORD:-0} == 0 && ${AROS_TEST_LARGE_HISTORY:-0} == 0 ]]
        cli_flags+=(-DOZ_TEST_HISTORY_RACE)
        history_wire_flags+=(-DOZ_TEST_HISTORY_RACE)
    fi
    if [[ ${AROS_TEST_POOL_PAGINATION:-0} == 1 ]];then
        [[ $writable == 1 && $memory == 8192 ]]
        cli_flags+=(-DOZ_TEST_POOL_PAGINATION)
    fi
    if [[ ${AROS_TEST_ZVOL_DEVICE:-0} == 1 ]];then
        [[ $writable == 1 || ( $writable == 2 && ${AROS_TEST_ZVOL_DEVICE_COLD:-0} == 1 ) ]]
        cli_flags+=(-DOZ_TEST_ZVOL_DEVICE)
        if [[ ${AROS_TEST_ZVOL_READONLY_POLICY:-0} == 1 ]];then
            [[ $writable == 1 && ${AROS_TEST_ZVOL_DEVICE_CAPACITY:-0} == 0 && ${AROS_TEST_ZVOL_DEVICE_READONLY:-0} == 0 ]]
            cli_flags+=(-DOZ_TEST_ZVOL_READONLY_POLICY)
        fi
        if [[ ${AROS_TEST_ZVOL_RESIZE_API:-0} == 1 ]];then
            [[ ( $writable == 1 || ( $writable == 2 && ${AROS_TEST_ZVOL_RESIZE_PERSIST:-none} == read ) ) && ${AROS_TEST_ZVOL_DEVICE_CAPACITY:-0} == 0 && ${AROS_TEST_ZVOL_DEVICE_READONLY:-0} == 0 ]]
            cli_flags+=(-DOZ_TEST_ZVOL_RESIZE_API)
            if [[ ${AROS_TEST_ZVOL_RESIZE_GUI:-0} == 1 ]];then
                [[ $gui == 1 ]]
                cli_flags+=(-DOZ_TEST_ZVOL_RESIZE_GUI)
            fi
        fi
        if [[ ${AROS_TEST_ZVOL_DEVICE_EMPTY:-0} == 1 ]];then
            [[ $writable == 1 && $gui == 0 && ${AROS_TEST_ZVOL_DEVICE_MULTI:-0} == 0 && ${AROS_TEST_ZVOL_DEVICE_CAPACITY:-0} == 0 && ${AROS_TEST_ZVOL_DEVICE_RACE:-0} == 0 && ${AROS_TEST_ZVOL_DEVICE_READONLY:-0} == 0 ]]
            cli_flags+=(-DOZ_TEST_ZVOL_DEVICE_EMPTY)
            gui_flags+=(-DOZ_NATIVE_EMPTY_ZVOL)
        fi
        if [[ ${AROS_TEST_ZVOL_DEVICE_CAPACITY:-0} == 1 ]];then
            [[ $writable == 1 && ${AROS_TEST_ZVOL_DEVICE_RACE:-0} == 0 && ${AROS_TEST_ZVOL_DEVICE_MULTI:-0} == 0 && ${AROS_TEST_ZVOL_DEVICE_READONLY:-0} == 0 && ${AROS_TEST_ZVOL_DEVICE_FILESYSTEM:-0} == 0 ]]
            cli_flags+=(-DOZ_TEST_ZVOL_DEVICE_CAPACITY)
        fi
        if [[ ${AROS_TEST_ZVOL_DEVICE_MULTI:-0} == 1 ]];then
            [[ $writable == 1 && ${AROS_TEST_ZVOL_DEVICE_READONLY:-0} == 0 ]]
            cli_flags+=(-DOZ_TEST_ZVOL_DEVICE_MULTI)
        fi
        if [[ ${AROS_TEST_ZVOL_MAPPING_GUI:-0} == 1 ]];then
            [[ $gui == 1 && $writable == 1 && ${AROS_TEST_ZVOL_DEVICE_MULTI:-0} == 1 && ${AROS_TEST_ZVOL_DEVICE_FILESYSTEM:-0} == 0 ]]
            cli_flags+=(-DOZ_TEST_ZVOL_MAPPING_GUI)
        fi
        if [[ ${AROS_TEST_ZVOL_REMAP_GUI:-0} != 0 ]];then
            [[ ${AROS_TEST_ZVOL_REMAP_GUI} == 1 || ${AROS_TEST_ZVOL_REMAP_GUI} == 2 ]]
            [[ $gui == 1 && $writable == 1 && ${AROS_TEST_ZVOL_DEVICE_MULTI:-0} == 1 && ${AROS_TEST_ZVOL_MAPPING_GUI:-0} == 0 && ${AROS_TEST_ZVOL_DEVICE_FILESYSTEM:-0} == 0 ]]
            cli_flags+=(-DOZ_TEST_ZVOL_REMAP_GUI)
            if [[ ${AROS_TEST_ZVOL_REMAP_GUI} == 2 ]];then cli_flags+=(-DOZ_TEST_ZVOL_REMAP_GUI_WRITE);fi
        fi
        if [[ ${AROS_TEST_ZVOL_MOUNT_GUI:-0} == 1 ]];then
            [[ ${AROS_TEST_ZVOL_REMAP_GUI:-0} == 2 ]]
            cli_flags+=(-DOZ_TEST_ZVOL_MOUNT_GUI)
        fi
        if [[ ${AROS_TEST_ZVOL_EJECT_GUI:-0} == 1 ]];then
            [[ ${AROS_TEST_ZVOL_REMAP_GUI:-0} == 2 && ${AROS_TEST_ZVOL_MOUNT_GUI:-0} == 0 ]]
            cli_flags+=(-DOZ_TEST_ZVOL_EJECT_GUI)
        fi
        if [[ ${AROS_TEST_ZVOL_DEVICE_RACE:-0} == 1 ]];then cli_flags+=(-DOZ_TEST_ZVOL_DEVICE_RACE);fi
        if [[ ${AROS_TEST_ZVOL_UNMAP:-0} != 0 ]];then
            [[ ${AROS_TEST_ZVOL_DEVICE_CAPACITY:-0} == 0 && ${AROS_TEST_ZVOL_DEVICE_READONLY:-0} == 0 ]]
            if [[ ${AROS_TEST_ZVOL_UNMAP} == 3 ]];then
                [[ $writable == 2 && ${AROS_TEST_ZVOL_RESIZE_PERSIST:-none} == read ]]
                rg -q 'RESULT native zvol UNMAP PASS VPD=1 full-block-zero=1 edges-preserved=1 invalid-list-no-write=1 malformed=3' "$verify_run/serial.log"
            else
                [[ $writable == 1 && ( ${AROS_TEST_ZVOL_UNMAP} == 1 || ${AROS_TEST_ZVOL_UNMAP} == 2 ) ]]
            fi
            cli_flags+=(-DOZ_TEST_ZVOL_UNMAP)
            if [[ ${AROS_TEST_ZVOL_UNMAP} == 2 ]];then cli_flags+=(-DOZ_TEST_ZVOL_UNMAP_UNSUPPORTED);fi
        fi
        if [[ ${AROS_TEST_ZVOL_READONLY_GUI:-0} == 1 ]];then
            [[ $gui == 1 && ${AROS_TEST_ZVOL_READONLY_POLICY:-0} == 1 ]]
            cli_flags+=(-DOZ_TEST_ZVOL_READONLY_GUI)
        fi
        if [[ ${AROS_TEST_ZVOL_DEVICE_KEEP:-0} == 1 ]];then
            [[ $writable == 1 && $keep_cli == 1 && ${AROS_TEST_ZVOL_DEVICE_FILESYSTEM:-0} == 1 ]]
            cli_flags+=(-DOZ_TEST_ZVOL_DEVICE_KEEP)
        fi
        if [[ ${AROS_TEST_ZVOL_DEVICE_COLD:-0} == 1 ]];then
            [[ $writable == 2 && ${AROS_TEST_ZVOL_DEVICE_FILESYSTEM:-0} == 1 ]]
            rg -q 'RESULT native zvol retained PASS dataset=qemu_mirror/device-volume' "$verify_run/serial.log"
            cli_flags+=(-DOZ_TEST_ZVOL_DEVICE_COLD)
        fi
        case ${AROS_TEST_ZVOL_RESIZE_PERSIST:-none} in
          none) ;;
          write)
            [[ ${AROS_TEST_ZVOL_DEVICE_KEEP:-0} == 1 ]]
            cli_flags+=(-DOZ_TEST_ZVOL_RESIZE_KEEP) ;;
          read)
            [[ ${AROS_TEST_ZVOL_DEVICE_COLD:-0} == 1 ]]
            rg -q 'RESULT native zvol resize retained PASS bytes=33554432 prefix=512 truncated-offset=25165824' "$verify_run/serial.log"
            cli_flags+=(-DOZ_TEST_ZVOL_RESIZE_COLD) ;;
          *) exit 2 ;;
        esac
        if [[ ${AROS_TEST_ZVOL_DEVICE_READONLY:-0} == 1 ]];then cli_flags+=(-DOZ_TEST_ZVOL_DEVICE_READONLY);fi
        if [[ ${AROS_TEST_ZVOL_DEVICE_FILESYSTEM:-0} == 1 ]];then
            [[ ${AROS_TEST_ZVOL_DEVICE_READONLY:-0} == 0 ]]
            [[ ${AROS_TEST_ZVOL_DEVICE_AFS:-0} == 0 || ${AROS_TEST_ZVOL_DEVICE_AFS:-0} == 1 ]]
            zvol_fs=FAT
            if [[ ${AROS_TEST_ZVOL_DEVICE_AFS:-0} == 1 ]];then zvol_fs=FFS;fi
            echo "Zvol filesystem: $zvol_fs"
            if [[ ${AROS_TEST_ZVOL_DEVICE_COLD:-0} == 1 ]];then
                if rg -q '^Zvol filesystem:' "$verify_run/runner.log";then
                    rg -Fxq "Zvol filesystem: $zvol_fs" "$verify_run/runner.log"
                else
                    # Older retained-volume fixtures supported FFS only.
                    [[ $zvol_fs == FFS ]]
                fi
            fi
            cli_flags+=(-DOZ_TEST_ZVOL_DEVICE_FILESYSTEM)
            if [[ ${AROS_TEST_ZVOL_DEVICE_AFS:-0} == 1 ]];then cli_flags+=(-DOZ_TEST_ZVOL_DEVICE_AFS);fi
        fi
        cp aros/device/device-test.c aros/device/zvol-device.c aros/device/mapping.h aros/device/mountfile.h aros/device/build.sh "$run/"
        if [[ -n $bundle ]];then
            [[ ${AROS_TEST_ZVOL_DEVICE_RACE:-0} == 0 ]]
            rg -Fxq 'diagnostics=0' "$bundle/Metadata/device-build-options.txt"
            rg -Fxq 'startup-test=0' "$bundle/Metadata/device-build-options.txt"
            mkdir "$run/zvol-device"
            cp "$bundle/C/OpenZFSDevice" "$run/zvol-device/"
            cp "$bundle/Metadata/device-build-options.txt" "$run/zvol-device/build-options.txt"
            cp "$bundle/Metadata/device-inputs.sha256" "$run/zvol-device/inputs.sha256"
            cmp "$bundle/C/OpenZFSDevice" "$run/zvol-device/OpenZFSDevice"
        else
            bash aros/device/build.sh "$run/zvol-device" >"$run/zvol-device-build.log" 2>&1
        fi
        gui_maps+=(-map "$run/zvol-device/OpenZFSDevice" /C/OpenZFSDevice)
        if [[ ${AROS_TEST_ZVOL_DEVICE_CAPACITY:-0} == 1 ]];then
            cp aros/device/client-test.c "$run/"
            "$cc" --sysroot="$sdk" -B"$sdk/lib/" -O2 -g -Wall -Wextra "$run/client-test.c" -o "$run/OpenZFSDeviceClientTest" >"$run/client-build.log" 2>&1
            gui_maps+=(-map "$run/OpenZFSDeviceClientTest" /C/OpenZFSDeviceClientTest)
        fi
        if [[ -n ${AROS_TEST_AFS_HANDLER:-} ]];then
            cp "$AROS_TEST_AFS_HANDLER" "$run/afs-handler"
            sha256sum "$run/afs-handler" >"$run/afs-handler.sha256"
            gui_maps+=(-map "$run/afs-handler" /L/afs-handler)
        fi
        if [[ -n ${AROS_TEST_FAT_HANDLER:-} ]];then
            [[ ${AROS_TEST_ZVOL_DEVICE_FILESYSTEM:-0} == 1 && ${AROS_TEST_ZVOL_DEVICE_AFS:-0} == 0 ]]
            cp "$AROS_TEST_FAT_HANDLER" "$run/fat-handler"
            sha256sum "$run/fat-handler" >"$run/fat-handler.sha256"
            gui_maps+=(-map "$run/fat-handler" /L/fat-handler)
        fi
    fi
    if [[ -f "$cli_binaries/OpenZFSStartupMount" ]];then
        cp "$cli_binaries/OpenZFSStartupMount" "$run/"
        cp aros/cli/startup-mount.c "$run/"
        cli_flags+=(-DOZ_TEST_STARTUP_MOUNT)
        gui_maps+=(-map "$run/OpenZFSStartupMount" /C/OpenZFSStartupMount)
        sha256sum "$run/OpenZFSStartupMount" >"$run/startup-mount.sha256"
    fi
    if [[ -f "$cli_binaries/OpenZFSRuntimeCheck" ]];then
        cp "$cli_binaries/OpenZFSRuntimeCheck" "$run/"
        cp aros/cli/runtime-check.c "$run/"
        cli_flags+=(-DOZ_TEST_RUNTIME_CHECK)
        gui_maps+=(-map "$run/OpenZFSRuntimeCheck" /C/OpenZFSRuntimeCheck)
        sha256sum "$run/OpenZFSRuntimeCheck" >"$run/runtime-check.sha256"
    fi
    if [[ -f "$cli_binaries/OpenZFSStartup" ]];then
        cp "$cli_binaries/OpenZFSStartup" "$run/";cp aros/cli/startup.c "$run/"
        cli_flags+=(-DOZ_TEST_STARTUP_COMMAND)
        gui_maps+=(-map "$run/OpenZFSStartup" /C/OpenZFSStartup)
        sha256sum "$run/OpenZFSStartup" >"$run/startup-command.sha256"
    fi
    if [[ ${AROS_TEST_CLI_POOL_CREATE:-0} == 1 ]];then cli_flags+=(-DOZ_TEST_CLI_POOL_CREATE);fi
    if [[ ${AROS_TEST_POOL_WIZARD_GUI:-0} == 1 ]];then
        [[ $gui == 1 && ${AROS_TEST_CLI_POOL_CREATE:-0} == 1 ]]
        cli_flags+=(-DOZ_TEST_POOL_WIZARD_GUI)
    fi
    if [[ ${AROS_TEST_TEMPORARY_CREATE_GUI:-0} == 1 ]];then
        [[ $gui == 1 && ${AROS_TEST_CLI_POOL_CREATE:-0} == 1 ]]
        cli_flags+=(-DOZ_TEST_TEMPORARY_CREATE_GUI)
    fi
    if [[ ${AROS_TEST_DOS_PUBLICATION_FAILURE:-0} == 1 ]];then
        [[ $writable == 1 ]];cli_flags+=(-DOZ_TEST_DOS_PUBLICATION_FAILURE)
    fi
    if [[ ${AROS_TEST_MOUNT_REGISTRY_ALLOC:-0} == 1 ]];then
        [[ $writable == 1 ]];cli_flags+=(-DOZ_TEST_MOUNT_REGISTRY_ALLOC)
    fi
    if [[ ${AROS_TEST_ERROR_LOG:-0} == 1 ]];then
        [[ $writable == 1 ]]
        cli_flags+=(-DOZ_TEST_ERROR_LOG)
        if [[ ${AROS_TEST_ERROR_LOG_HEAD:-0} == 1 ]];then
            [[ ${AROS_TEST_ERROR_LOG_SEEDED:-0} == 1 ]];cli_flags+=(-DOZ_TEST_ERROR_LOG_HEAD)
        fi
        if [[ ${AROS_TEST_ERROR_LOG_GUI:-0} == 1 ]];then
            [[ $gui == 1 ]];cli_flags+=(-DOZ_TEST_ERROR_LOG_GUI)
        fi
        if [[ ${AROS_TEST_ERROR_LOG_SEEDED:-0} == 1 ]];then cli_flags+=(-DOZ_TEST_ERROR_LOG_SEEDED);fi
    fi
    if [[ ${AROS_TEST_RECEIVED_PROPERTIES:-0} == 1 ]];then
        [[ $writable == 1 ]]
        cli_flags+=(-DOZ_TEST_RECEIVED_PROPERTIES)
        if [[ ${AROS_TEST_RECEIVED_EXPANSION:-0} == 1 ]];then cli_flags+=(-DOZ_TEST_RECEIVED_EXPANSION);fi
        if [[ ${AROS_TEST_RECEIVED_GUI:-0} == 1 ]];then
            [[ $gui == 1 ]]
            cli_flags+=(-DOZ_TEST_RECEIVED_GUI)
        fi
    fi
    if [[ ${AROS_TEST_ZVOL_TRANSPORT:-0} == 1 ]];then
        [[ $writable == 1 ]]
        cli_flags+=(-DOZ_TEST_ZVOL_TRANSPORT)
        if [[ $dataset_admin_only == 1 ]];then
            [[ $layout == mirror && $members == both && $gui == 0 ]]
            cli_flags+=(-DOZ_TEST_DATASET_ADMIN_ONLY)
            gui_flags+=(-DOZ_NATIVE_DATASET_ADMIN_ONLY)
        fi
        if [[ ${AROS_TEST_QUEUE_HOLD:-0} == 1 ]];then
            cli_flags+=(-DOZ_TEST_QUEUE_HOLD)
            if [[ ${AROS_TEST_QUEUE_GUI:-0} == 1 ]];then
                [[ $gui == 1 ]]
                cli_flags+=(-DOZ_TEST_QUEUE_GUI)
            fi
        fi
        if [[ ${AROS_TEST_WAIT_PENDING:-0} == 1 ]];then
            cli_flags+=(-DOZ_TEST_WAIT_PENDING)
        fi
        if [[ ${AROS_TEST_POOL_LIST_GUI:-0} == 1 ]];then
            [[ $gui == 1 ]]
            cli_flags+=(-DOZ_TEST_POOL_LIST_GUI)
        fi
        if [[ ${AROS_TEST_ROLLBACK_GUI:-0} == 1 ]];then
            [[ $gui == 1 ]]
            cli_flags+=(-DOZ_TEST_ROLLBACK_GUI)
        fi
        if [[ ${AROS_TEST_PROMOTION_GUI:-0} == 1 ]];then
            [[ $gui == 1 ]]
            cli_flags+=(-DOZ_TEST_PROMOTION_GUI)
        fi
        if [[ ${AROS_TEST_RECEIVE_GUI:-0} == 1 ]];then
            [[ $gui == 1 ]]
            cli_flags+=(-DOZ_TEST_RECEIVE_GUI)
        fi
        if [[ ${AROS_TEST_RAW_MOUNT_WRITABLE:-0} == 1 ]];then
            cli_flags+=(-DOZ_TEST_RAW_MOUNT_WRITABLE)
        fi
        if [[ ${AROS_TEST_RAW_MOUNT_FORCE:-0} == 1 ]];then
            [[ ${AROS_TEST_RAW_MOUNT_WRITABLE:-0} == 1 ]]
            cli_flags+=(-DOZ_TEST_RAW_MOUNT_FORCE)
        fi
        if [[ ${AROS_TEST_FORCE_RECEIVE_GUI:-0} == 1 ]];then
            [[ $gui == 1 && ${AROS_TEST_RECEIVE_GUI:-0} == 1 ]]
            cli_flags+=(-DOZ_TEST_FORCE_RECEIVE_GUI)
        fi
        if [[ ${AROS_TEST_ENCRYPTED_CREATE_GUI:-0} == 1 ]];then
            [[ $gui == 1 ]]
            cli_flags+=(-DOZ_TEST_ENCRYPTED_CREATE_GUI)
        fi
        if [[ ${AROS_TEST_PROJECT_GUI:-0} == 1 ]];then
            [[ $gui == 1 ]]
            cli_flags+=(-DOZ_TEST_PROJECT_GUI)
        fi
        if [[ ${AROS_TEST_ENGINE_POLICY_GUI:-0} == 1 ]];then
            [[ $gui == 1 && $writable == 1 ]]
            cli_flags+=(-DOZ_TEST_ENGINE_POLICY_GUI)
        fi
        if [[ ${AROS_TEST_ADMISSION_LIST_GUI:-0} == 1 ]];then
            [[ $gui == 1 && $writable == 1 ]]
            cli_flags+=(-DOZ_TEST_ADMISSION_LIST_GUI)
        fi
        if [[ ${AROS_TEST_DIFF_GUI:-0} == 1 ]];then
            [[ $gui == 1 && $writable == 1 ]]
            cli_flags+=(-DOZ_TEST_DIFF_GUI)
        fi
        if [[ ${AROS_TEST_PERMISSION_GUI:-0} == 1 ]];then
            [[ $gui == 1 ]]
            cli_flags+=(-DOZ_TEST_PERMISSION_GUI)
        fi
        if [[ ${AROS_TEST_REVOKE_ALL_GUI:-0} == 1 ]];then
            [[ $gui == 1 ]]
            cli_flags+=(-DOZ_TEST_REVOKE_ALL_GUI)
        fi
        if [[ ${AROS_TEST_RECURSIVE_REVOKE_GUI:-0} == 1 ]];then
            [[ $gui == 1 ]]
            cli_flags+=(-DOZ_TEST_RECURSIVE_REVOKE_GUI)
        fi
        if [[ ${AROS_TEST_ZVOL_CREATE_GUI:-0} == 1 ]];then
            [[ $gui == 1 ]]
            cli_flags+=(-DOZ_TEST_ZVOL_CREATE_GUI)
        fi
    fi
    if [[ $live_create_only == 1 ]];then
        [[ $writable == 1 && $layout == mirror && $members == both && $gui == 0 ]]
        cli_flags+=(-DOZ_TEST_LIVE_CREATE_ONLY)
        gui_flags+=(-DOZ_NATIVE_LIVE_CREATE_ONLY)
    fi
    case ${AROS_TEST_ZVOL_DISCARD_PERSIST:-none} in
        none) ;;
        write)
            [[ $writable == 1 && ${AROS_TEST_ZVOL_TRANSPORT:-0} == 1 && $keep_cli == 1 ]]
            cli_flags+=(-DOZ_TEST_ZVOL_DISCARD_KEEP) ;;
        read)
            [[ $writable == 2 && ${AROS_TEST_ZVOL_TRANSPORT:-0} == 0 ]]
            rg -q 'RESULT native zvol discard retained PASS bytes=24576 zero-offset=8192 zero-length=8192' "$verify_run/serial.log"
            cli_flags+=(-DOZ_TEST_ZVOL_TRANSPORT -DOZ_TEST_ZVOL_DISCARD_COLD) ;;
        *) exit 2 ;;
    esac
    case ${AROS_TEST_PERMISSION_PERSIST:-none} in
        none) ;;
        write)
            [[ ${AROS_TEST_ZVOL_DISCARD_PERSIST:-none} == write ]]
            cli_flags+=(-DOZ_TEST_PERMISSION_KEEP) ;;
        read|authorize)
            [[ ${AROS_TEST_ZVOL_DISCARD_PERSIST:-none} == read ]]
            rg -q 'RESULT native permissions retained PASS synced=1' "$verify_run/serial.log"
            cli_flags+=(-DOZ_TEST_PERMISSION_COLD)
            if [[ ${AROS_TEST_PERMISSION_PERSIST} == authorize ]];then
                [[ $layout == mirror && $members == both && ( $gui == 0 || $gui == 1 ) && $gpt == 0 ]]
                cli_flags+=(-DOZ_TEST_PERMISSION_AUTHORIZE)
                if [[ ${AROS_TEST_PERMISSION_LIST_GUI:-0} == 1 ]];then
                    [[ $gui == 1 ]]
                    cli_flags+=(-DOZ_TEST_PERMISSION_LIST_GUI)
                fi
            fi ;;
        *) exit 2 ;;
    esac
    if [[ ${AROS_TEST_ACCOUNTING_SET_GUI:-0} == 1 ]];then
        [[ $gui == 1 && $writable == 1 && ${AROS_TEST_ZVOL_TRANSPORT:-0} == 1 ]]
        cli_flags+=(-DOZ_TEST_ACCOUNTING_SET_GUI)
    fi
    case ${AROS_TEST_ENCRYPTION_PERSIST:-none} in
        none) ;;
        write)
            [[ ${AROS_TEST_ZVOL_DISCARD_PERSIST:-none} == write ]]
            cli_flags+=(-DOZ_TEST_ENCRYPTION_KEEP) ;;
        read|resume)
            [[ ${AROS_TEST_ZVOL_DISCARD_PERSIST:-none} == read ]]
            rg -q 'RESULT native CLI encryption retained PASS synced=1 replacement-key=1' "$verify_run/serial.log"
            rg -q 'RESULT native API encryption retained PASS hex=1 passphrase=1 volume=1 synced=1' "$verify_run/serial.log"
            rg -q 'RESULT native raw receive retained PASS incremental=1 synced=1' "$verify_run/serial.log"
            cli_flags+=(-DOZ_TEST_ENCRYPTION_COLD)
            if [[ ${AROS_TEST_ENCRYPTION_PERSIST} == resume ]];then
                [[ $layout == mirror && $members == both && $gui == 0 && $gpt == 0 && ${AROS_TEST_PERMISSION_PERSIST:-none} != authorize ]]
                rg -q 'RESULT native raw resume retained PASS pending=1 source-locked=1 synced=1 CLI=1' "$verify_run/serial.log"
                cli_flags+=(-DOZ_TEST_RAW_RESUME_COLD)
            fi ;;
        *) exit 2 ;;
    esac
    case ${AROS_TEST_PROJECT_PERSIST:-none} in
        none) ;;
        write)
            [[ ${AROS_TEST_ZVOL_DISCARD_PERSIST:-none} == write ]]
            cli_flags+=(-DOZ_TEST_PROJECT_KEEP) ;;
        read)
            [[ ${AROS_TEST_ZVOL_DISCARD_PERSIST:-none} == read ]]
            rg -q 'RESULT native project retained PASS synced=1' "$verify_run/serial.log"
            cli_flags+=(-DOZ_TEST_PROJECT_COLD) ;;
        *) exit 2 ;;
    esac
    case ${AROS_TEST_ACCOUNTING_PERSIST:-none} in
        none) ;;
        write)
            [[ ${AROS_TEST_ZVOL_DISCARD_PERSIST:-none} == write ]]
            cli_flags+=(-DOZ_TEST_ACCOUNTING_KEEP) ;;
        read)
            [[ ${AROS_TEST_ZVOL_DISCARD_PERSIST:-none} == read ]]
            rg -q 'RESULT native accounting retained PASS synced=1' "$verify_run/serial.log"
            cli_flags+=(-DOZ_TEST_ACCOUNTING_COLD)
            if [[ ${AROS_TEST_ACCOUNTING_GUI:-0} == 1 ]];then
                [[ $gui == 1 ]]
                cli_flags+=(-DOZ_TEST_ACCOUNTING_GUI)
            fi ;;
        *) exit 2 ;;
    esac
    if [[ ${AROS_TEST_CLI_DRAID:-0} == 1 ]];then
        [[ $writable == 1 && ${AROS_TEST_CLI_POOL_CREATE:-0} == 1 ]]
        cli_flags+=(-DOZ_TEST_CLI_DRAID)
        if [[ ${AROS_TEST_TYPED_DRAID:-0} == 1 ]];then
            [[ $draid_persist != read || $draid_expand != 0 ]]
            cli_flags+=(-DOZ_TEST_TYPED_DRAID)
        fi
        if [[ $draid_persist != none ]];then cli_flags+=(-DOZ_TEST_DRAID_PERSIST);fi
        if [[ $draid_persist == read ]];then cli_flags+=(-DOZ_TEST_DRAID_COLD);fi
        if [[ $draid_expand != 0 ]];then cli_flags+=("-DOZ_TEST_DRAID_EXPAND_PARITY=$draid_expand");fi
    fi
    if [[ ${AROS_TEST_NATIVE_CREATE:-0} == 1 ]];then
        [[ $writable == 1 && $layout == mirror && $members == both && ${AROS_TEST_CLI_POOL_CREATE:-0} == 1 && ${AROS_TEST_NATIVE_ADD:-0} == 0 && ${AROS_TEST_NATIVE_REPLACE:-0} == 0 && ${AROS_TEST_NATIVE_SPLIT:-0} == 0 && ${AROS_TEST_SPLIT_RETAINED:-0} == 0 ]]
        cli_flags+=(-DOZ_TEST_NATIVE_CREATE)
    fi
    if [[ ${AROS_TEST_TYPED_NATIVE_CREATE:-0} == 1 ]];then
        [[ ${AROS_TEST_NATIVE_CREATE:-0} == 1 ]]
        cli_flags+=(-DOZ_TEST_TYPED_NATIVE_CREATE)
    fi
    if [[ ${AROS_TEST_TYPED_NATIVE_MIRROR:-0} == 1 ]];then
        [[ ${AROS_TEST_TYPED_NATIVE_CREATE:-0} == 1 || $create_cold == 1 ]]
        cli_flags+=(-DOZ_TEST_TYPED_NATIVE_MIRROR)
        if [[ $create_cold == 1 ]];then
            rg -q 'RESULT native typed mixed mirror PASS file-member-deleted=1 native-survivor-read=1' "$verify_run/serial.log"
        fi
    fi
    if [[ ${AROS_TEST_SPLIT_RETAINED:-0} == 1 ]];then cli_flags+=(-DOZ_TEST_SPLIT_RETAINED);fi
    if [[ $split_keep == 1 ]];then cli_flags+=(-DOZ_TEST_NATIVE_SPLIT_KEEP);fi
    if [[ $split_cold == 1 ]];then cli_flags+=(-DOZ_TEST_NATIVE_SPLIT_COLD);fi
    if [[ $create_cold == 1 ]];then cli_flags+=(-DOZ_TEST_NATIVE_CREATE_COLD);fi
    if [[ ${AROS_TEST_NATIVE_ADD:-0} == 1 ]];then
        [[ $writable == 1 && $layout == mirror && $members == both && ${AROS_TEST_CLI_POOL_CREATE:-0} == 1 ]]
        cli_flags+=(-DOZ_TEST_NATIVE_ADD)
    fi
    if [[ ${AROS_TEST_TYPED_NATIVE_ADD:-0} == 1 ]];then
        [[ ${AROS_TEST_NATIVE_ADD:-0} == 1 ]]
        cli_flags+=(-DOZ_TEST_TYPED_NATIVE_ADD)
    fi
    if [[ ${AROS_TEST_NATIVE_REPLACE:-0} == 1 ]];then
        [[ $writable == 1 && $layout == mirror && $members == both && ${AROS_TEST_CLI_POOL_CREATE:-0} == 1 && ${AROS_TEST_NATIVE_ADD:-0} == 0 ]]
        cli_flags+=(-DOZ_TEST_NATIVE_REPLACE)
    fi
    if [[ ${AROS_TEST_TYPED_NATIVE_REPLACE:-0} == 1 ]];then
        [[ ${AROS_TEST_NATIVE_REPLACE:-0} == 1 ]]
        cli_flags+=(-DOZ_TEST_TYPED_NATIVE_REPLACE)
    fi
    if [[ ${AROS_TEST_NATIVE_SPLIT:-0} == 1 ]];then
        [[ $writable == 1 && $layout == mirror && $members == both && ${AROS_TEST_CLI_POOL_CREATE:-0} == 1 && ${AROS_TEST_NATIVE_ADD:-0} == 0 && ${AROS_TEST_NATIVE_REPLACE:-0} == 0 && ${AROS_TEST_SPLIT_RETAINED:-0} == 0 ]]
        cli_flags+=(-DOZ_TEST_NATIVE_SPLIT)
    fi
    if [[ ${AROS_TEST_TYPED_NATIVE_ATTACH:-0} == 1 ]];then
        [[ ${AROS_TEST_NATIVE_SPLIT:-0} == 1 ]]
        cli_flags+=(-DOZ_TEST_TYPED_NATIVE_ATTACH)
    fi
    if [[ ${AROS_TEST_CLI_TINY_REPLY:-0} == 1 ]];then
        [[ $writable == 2 ]]
        rg -Fxq 'import_tiny_reply=1' "$AROS_TEST_WHOLE_CLI_LINK/build-options.txt"
        cli_flags+=(-DOZ_TEST_CLI_TINY_REPLY)
    elif [[ -f "$AROS_TEST_WHOLE_CLI_LINK/build-options.txt" ]];then
        rg -Fxq 'import_tiny_reply=0' "$AROS_TEST_WHOLE_CLI_LINK/build-options.txt"
    fi
    if [[ $writable == 2 ]];then cli_flags+=(-DOZ_TEST_CLI_COLD);fi
    if [[ $keep_cli == 1 ]];then cli_flags+=(-DOZ_TEST_CLI_KEEP);fi
    if [[ ${AROS_TEST_PIPE_POLICY_LEGACY:-0} == 1 ]];then
        [[ -n ${AROS_TEST_PIPE_POLICY_HANDLER:-} ]]
        cli_flags+=(-DOZ_TEST_PIPE_LEGACY)
    fi
    if [[ ${AROS_TEST_CLI_DISCOVERY:-1} == 1 ]];then cli_flags+=(-DOZ_TEST_CLI_DISCOVERY);fi
    media_sources=()
    if [[ ${AROS_TEST_NATIVE_REPATH:-0} == 1 ]];then
        [[ $writable == 1 && $layout == mirror && $members == both && $split_cold == 0 && $create_cold == 0 && ${AROS_TEST_NATIVE_CREATE:-0} == 0 && ${AROS_TEST_NATIVE_ADD:-0} == 0 && ${AROS_TEST_NATIVE_REPLACE:-0} == 0 && ${AROS_TEST_NATIVE_SPLIT:-0} == 0 && ${AROS_TEST_BACKING_MEDIA_CHANGE:-0} == 0 ]]
        cli_flags+=(-DOZ_TEST_NATIVE_REPATH)
        if [[ ${AROS_TEST_NATIVE_REPATH_RAM:-0} == 1 ]];then cli_flags+=(-DOZ_TEST_NATIVE_REPATH_RAM);fi
        media_sources+=(aros/engine/device-io.c aros/engine/partition-io.c aros/platform/partitions.c aros/engine/repath-copy-test.c)
        cp "${media_sources[@]}" "$run/"
    fi
    if [[ ${AROS_TEST_NATIVE_LABELCLEAR:-0} == 1 ]];then
        [[ $writable == 1 && $layout == mirror && $members == both && $gui == 0 ]]
        cli_flags+=(-DOZ_TEST_NATIVE_LABELCLEAR)
        if [[ $labelclear_only == 1 ]];then
            [[ -n ${AROS_TEST_WHOLE_CLI_LINK:-} ]]
            cli_flags+=(-DOZ_TEST_NATIVE_LABELCLEAR_ONLY)
            gui_flags+=(-DOZ_NATIVE_LABELCLEAR_ONLY)
        fi
    elif [[ $labelclear_only == 1 ]];then
        echo 'NATIVE_LABELCLEAR_ONLY requires AROS_TEST_NATIVE_LABELCLEAR=1' >&2
        exit 2
    fi
    if [[ ${AROS_TEST_BACKING_MEDIA_CHANGE:-0} == 1 ]];then
        [[ ${AROS_TEST_ZVOL_DEVICE:-0} == 1 ]]
        cli_flags+=(-DOZ_TEST_BACKING_MEDIA_CHANGE)
        media_sources+=(aros/engine/device-io.c)
        cp aros/engine/device-io.c aros/engine/device-io.h "$run/"
    fi
    "$cc" --sysroot="$sdk" -B"$sdk/lib/" \
        -O2 -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -include aros/engine/config.h "${history_wire_flags[@]}" \
        -Iaros/cli/include -Ibuild-aros/include -Iaros/engine/include -Ilib/libspl/include \
        -Ilib/libspl/include/os/linux -Iinclude -Iinclude/os/linux/zfs \
        -c aros/cli/history-wire-test.c -o "$run/history-wire-test.o" >"$run/history-wire-build.log" 2>&1
    cp aros/cli/history-wire-test.c "$run/"
    sha256sum aros/cli/history-wire-test.c "$run/history-wire-test.o" >"$run/history-wire.sha256"
    received_objects=()
    if [[ ${AROS_TEST_ERROR_LOG:-0} == 1 ]];then
        "$cc" --sysroot="$sdk" -B"$sdk/lib/" \
            -O2 -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -include aros/engine/config.h \
            -Iaros/cli/include -Ibuild-aros/include -Iaros/engine/include -Ilib/libspl/include \
            -Ilib/libspl/include/os/linux -Iinclude -Iinclude/os/linux/zfs \
            -c aros/cli/error-log-wire-test.c -o "$run/error-log-wire-test.o" >"$run/error-log-wire-build.log" 2>&1
        cp aros/cli/error-log-wire-test.c "$run/"
        sha256sum aros/cli/error-log-wire-test.c "$run/error-log-wire-test.o" >"$run/error-log-wire.sha256"
        received_objects+=("$run/error-log-wire-test.o")
    fi
    if [[ ${AROS_TEST_RECEIVED_PROPERTIES:-0} == 1 ]];then
        "$cc" --sysroot="$sdk" -B"$sdk/lib/" \
            -O2 -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -include aros/engine/config.h \
            -Iaros/cli/include -Ibuild-aros/include -Iaros/engine/include -Ilib/libspl/include \
            -Ilib/libspl/include/os/linux -Iinclude -Iinclude/os/linux/zfs \
            -c aros/cli/received-wire-test.c -o "$run/received-wire-test.o" >"$run/received-wire-build.log" 2>&1
        cp aros/cli/received-wire-test.c "$run/"
        sha256sum aros/cli/received-wire-test.c "$run/received-wire-test.o" >"$run/received-wire.sha256"
        received_objects+=("$run/received-wire-test.o")
    fi
    "$cc" --sysroot="$sdk" -B"$sdk/lib/" \
        -I"$pthread_include" -O2 -Wall -Wextra "${cli_flags[@]}" aros/cli/service-command-test.c "$run/history-wire-test.o" "${received_objects[@]}" aros/ui/project-worker.c aros/ui/iostat-rates-test.c aros/cli/process.c aros/cli/identity.c \
        aros/cli/arguments.c aros/cli/output.c aros/ui/api.c aros/ui/admission-client.c "${media_sources[@]}" \
        aros/ui/startup-settings.c aros/ui/startup-settings-file.c aros/ui/preferences.c aros/ui/preferences-file.c -liffparse \
        "$pthread" -o "$run/OpenZFSServiceCommandTest" \
        >"$run/service-command-build.log" 2>&1
    sha256sum "$run/zfs" "$run/zpool" "$run/OpenZFSServiceCommandTest" \
        aros/cli/service-command-test.c aros/cli/identity.c aros/cli/process.c aros/cli/arguments.c aros/cli/output.c \
        aros/ui/api.c aros/ui/admission-client.c aros/ui/project-worker.c aros/ui/project-worker.h "$pthread_include/pthread.h" "$pthread" \
        >"$run/whole-cli.sha256"
    gui_flags+=(-DOZ_NATIVE_WHOLE_CLI)
    gui_maps+=(-map "$run/zfs" /C/zfs -map "$run/zpool" /C/zpool
        -map "$run/OpenZFSServiceCommandTest" /C/OpenZFSServiceCommandTest)
fi
if [[ $gui != 0 ]];then
    cp "${AROS_TEST_MANAGER_BINARY:?GUI requires manager executable}" "$run/OpenZFSManagerNative"
    if [[ $gui == 2 ]];then gui_flags+=(-DOZ_NATIVE_CREATE_GUI);
    elif [[ $gui == 3 ]];then gui_flags+=(-DOZ_NATIVE_SNAPRENAME_GUI);
    else gui_flags+=(-DOZ_NATIVE_IMPORT_GUI);fi
    gui_maps+=(-map "$run/OpenZFSManagerNative" /C/OpenZFSManagerNative)
    sha256sum "$run/OpenZFSManagerNative" >"$run/manager.sha256"
fi
"$qemu" --version >"$run/qemu-version.txt"
sha256sum "$(command -v "$qemu")" >"$run/qemu.sha256"
cli_objects=()
if [[ $gpt == 3 ]];then
    gui_flags+=(-DOZ_NATIVE_GPT_CLI)
    for source in aros/cli/gpt.c aros/cli/gpt-test.c aros/cli/control-client.c aros/cli/stream-client.c aros/cli/stream-channel.c;do
        stem=${source//\//_}
        "$cc" --sysroot="$sdk" -B"$sdk/lib/" \
            -O2 -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -include aros/engine/config.h \
            -Iaros/cli/include -Ibuild-aros/include -Iaros/engine/include -Ilib/libspl/include \
            -Ilib/libspl/include/os/linux -Iinclude -Iinclude/os/linux/zfs \
            -c "$source" -o "$run/$stem.o" >"$run/$stem.log" 2>&1
        cli_objects+=("$run/$stem.o")
        cp "$source" "$run/"
    done
    cli_objects+=("$repo/build-aros/cli-probe.9yNVFf1o/upstream-init-selected.o"
        "$repo/build-aros/full-native-libzfs-init/client/lib_libzfs_core_libzfs_core.c.o"
        "$repo/build-aros/full-native-libzfs-init/client/lib_libnvpair_libnvpair.c.o"
        "$repo/build-aros/libnvpair-adt-consistent.a"
        "$pthread"
        /opt/aros-workspace/chromium-aros/src/out/aros-chrome/obj/third_party/boringssl/libboringssl.a)
    sha256sum "${cli_objects[@]}" >"$run/cli-inputs.sha256"
fi
completion_flags=(-DOZ_QEMU_COMPLETION_PORT=0xe9)
clear_objects=()
if [[ $clear_manager == 1 ]];then
    clear_objects=(aros/ui/project-worker.c aros/cli/process.c aros/cli/arguments.c aros/cli/output.c)
    sha256sum "${clear_objects[@]}" >"$run/manager-clear-inputs.sha256"
fi
case ${AROS_TEST_COMPLETION_FAIL:-0} in
    0) ;;
    1) completion_flags+=(-DOZ_QEMU_COMPLETION_FAIL) ;;
    *) exit 2 ;;
esac
"$cc" --sysroot="$sdk" -B"$sdk/lib/" -O2 -Wall -Wextra \
    -I"$pthread_include" \
    "${completion_flags[@]}" "${gui_flags[@]}" aros/engine/native-import-test.c aros/ui/api.c aros/ui/admission-client.c \
    aros/ui/startup-import.c aros/ui/startup-settings.c aros/ui/startup-settings-file.c aros/ui/preferences.c aros/ui/preferences-file.c \
    aros/ui/admission-worker.c "${inventory_objects[@]}" "${cli_objects[@]}" "${clear_objects[@]}" "$pthread" \
    -liffparse -o "$run/OpenZFSNativeImportTest" >"$run/build.log" 2>&1
if [[ $native_bus == usb ]];then
    echo "USB controller: $usb_controller"
    drives+=(-device "$usb_controller,id=ozusb")
elif [[ $native_bus == ahci ]];then
    drives+=(-device ich9-ahci,id=ozahci)
fi
for unit in "${units[@]}";do
    [[ -f "$fixture/disk$unit.raw" && $(stat -c %s "$fixture/disk$unit.raw") == 536870912 ]]
    sha256sum "$fixture/disk$unit.raw" >>"$run/backing.sha256"
    if [[ $usb_created == 1 ]];then
        qemu-img create -f raw "$run/created-member$unit.raw" 512M
        sgdisk --clear --new=1:4097:528384 --typecode=1:BF01 "$run/created-member$unit.raw" >"$run/created-member$unit-gpt.log" 2>&1
        sgdisk --verify "$run/created-member$unit.raw" >>"$run/created-member$unit-gpt.log" 2>&1
        rg -q 'No problems found' "$run/created-member$unit-gpt.log"
        sha256sum "$run/created-member$unit.raw" >>"$run/backing.sha256"
        qemu-img create -f qcow2 -F raw -b "$run/created-member$unit.raw" "$run/disk$unit.qcow2"
    elif [[ -n $usb_cold && $unit == 0 && $usb_cold_kind == survivor ]];then
        # Fresh empty member, not the removed disk or any of its labels.
        qemu-img create -f raw "$run/usb-empty.raw" 512M
        sgdisk --clear "$run/usb-empty.raw" >"$run/usb-empty-create.log" 2>&1
        sgdisk --verify --print "$run/usb-empty.raw" >"$run/usb-empty-verify.log" 2>&1
        rg -q 'No problems found' "$run/usb-empty-verify.log"
        sha256sum "$run/usb-empty.raw" >>"$run/backing.sha256"
        qemu-img create -f qcow2 -F raw -b "$run/usb-empty.raw" "$run/disk0.qcow2"
    elif [[ -n $usb_cold ]];then
        sha256sum "$usb_cold/disk$unit.qcow2" >>"$run/backing.sha256"
        qemu-img create -f qcow2 -F qcow2 -b "$usb_cold/disk$unit.qcow2" "$run/disk$unit.qcow2"
    elif [[ $writable == 2 ]];then
        sha256sum "$verify_run/disk$unit.qcow2" >>"$run/backing.sha256"
        qemu-img create -f qcow2 -F qcow2 -b "$verify_run/disk$unit.qcow2" "$run/disk$unit.qcow2"
    else
        qemu-img create -f qcow2 -F raw -b "$fixture/disk$unit.raw" "$run/disk$unit.qcow2"
    fi
    if [[ $members == both || $members == "$unit" || ( $members == 1-empty-peer && $unit == 1 ) || ( $members == 01 && ( $unit == 0 || $unit == 1 ) ) ]];then
        if [[ $import_rewind_cut == 1 && $unit == 0 ]];then
            # Observe qcow2's file-child flush after its cached metadata has
            # been written, not a filter above qcow2 that cannot see it.
            drives+=(-blockdev "driver=qcow2,node-name=ozcut0,file.driver=blkdebug,file.image.driver=file,file.image.filename=$run/disk0.qcow2"
                -device ide-hd,drive=ozcut0,bus=ide.0,unit=0)
        elif [[ $native_bus == usb ]];then
            drives+=(-drive "file=$run/disk$unit.qcow2,format=qcow2,if=none,id=ozusb$unit,aio=threads"
                -device "usb-storage,drive=ozusb$unit,bus=ozusb.0,port=$((unit+1)),id=ozmember$unit,serial=oz-member-$unit,removable=on")
        elif [[ $native_bus == ahci ]];then
            drives+=(-drive "file=$run/disk$unit.qcow2,format=qcow2,if=none,id=ozahci$unit,aio=threads"
                -device "ide-hd,drive=ozahci$unit,bus=ozahci.$unit")
        elif [[ $unit == 4 ]];then
            drives+=(-device ich9-ahci,id=ozahci
                -drive "file=$run/disk$unit.qcow2,format=qcow2,if=none,id=ozfour,aio=threads"
                -device ide-hd,drive=ozfour,bus=ozahci.0)
        else
            drives+=(-drive "file=$run/disk$unit.qcow2,format=qcow2,if=ide,index=$unit,aio=threads")
        fi
    fi
done
if [[ $members == 1-empty-peer ]];then
    # ATA needs an identifiable master to enumerate the surviving slave.
    # No original member bytes: this is a fresh valid GPT with no partitions.
    qemu-img create -f raw "$run/empty-peer.raw" 512M
    sgdisk --clear "$run/empty-peer.raw" >"$run/empty-peer-create.log" 2>&1
    sgdisk --verify --print "$run/empty-peer.raw" >"$run/empty-peer-verify.log" 2>&1
    rg -q 'No problems found' "$run/empty-peer-verify.log"
    sha256sum "$run/empty-peer.raw" >>"$run/backing.sha256"
    qemu-img create -f qcow2 -F raw -b "$run/empty-peer.raw" "$run/empty-peer.qcow2"
    drives+=(-drive "file=$run/empty-peer.qcow2,format=qcow2,if=ide,index=0,aio=threads")
fi
if [[ ${AROS_TEST_NATIVE_REPATH:-0} == 1 ]];then
    qemu-img create -f raw "$run/repath.raw" 512M
    sgdisk --clear --new=1:2048:+256M --typecode=1:BF01 "$run/repath.raw" >"$run/repath-create.log" 2>&1
    sgdisk --verify --print "$run/repath.raw" >"$run/repath-verify.log" 2>&1
    rg -q 'No problems found' "$run/repath-verify.log"
    sha256sum "$run/repath.raw" >>"$run/backing.sha256"
    qemu-img create -f qcow2 -F raw -b "$run/repath.raw" "$run/repath.qcow2"
    drives+=(-drive "file=$run/repath.qcow2,format=qcow2,if=ide,index=3,aio=threads")
fi
if [[ ${AROS_TEST_NATIVE_ADD:-0} == 1 || ${AROS_TEST_NATIVE_REPLACE:-0} == 1 || ${AROS_TEST_NATIVE_SPLIT:-0} == 1 || ${AROS_TEST_NATIVE_CREATE:-0} == 1 ]];then
    # Fresh test-only third disk; no physical disk or existing partition is
    # formatted. The original mirror remains behind separate qcow2 overlays.
    qemu-img create -f raw "$run/add-partition.raw" 256M
    sgdisk --clear --new=1:2048:0 --typecode=1:BF01 "$run/add-partition.raw" >"$run/add-partition-create.log" 2>&1
    sgdisk --verify --print "$run/add-partition.raw" >"$run/add-partition-verify.log" 2>&1
    rg -q 'No problems found' "$run/add-partition-verify.log"
    sha256sum "$run/add-partition.raw" >>"$run/backing.sha256"
    qemu-img create -f qcow2 -F raw -b "$run/add-partition.raw" "$run/add-partition.qcow2"
    drives+=(-drive "file=$run/add-partition.qcow2,format=qcow2,if=ide,index=3,aio=threads")
fi
if [[ $split_cold == 1 || $create_cold == 1 ]];then
    # Fresh guest, only the persisted split member; the writer's RAM source
    # cannot exist here. Hash both backing and new overlay to detect any write.
    [[ -f "$verify_run/add-partition-result.raw" && $(stat -c %s "$verify_run/add-partition-result.raw") == 268435456 ]]
    sha256sum "$verify_run/add-partition-result.raw" >>"$run/backing.sha256"
    qemu-img create -f qcow2 -F raw -b "$verify_run/add-partition-result.raw" "$run/split-partition.qcow2"
    drives+=(-drive "file=$run/split-partition.qcow2,format=qcow2,if=ide,index=3,aio=threads")
fi
sha256sum "$run"/*.qcow2 >"$run/overlays.sha256"
if [[ $usb_remove == 2 || $usb_all_lost_writable == 1 ]];then
    cp "$run/overlays.sha256" "$run/overlays-before.sha256"
    : >"$run/overlays.sha256"
fi
if [[ ${AROS_TEST_PERMISSION_PERSIST:-none} == authorize || ${AROS_TEST_ENCRYPTION_PERSIST:-none} == resume ]];then
    # Only fresh child overlays may change; the retained writer and every
    # original backing remain in backing.sha256 and must remain unchanged.
    cp "$run/overlays.sha256" "$run/overlays-before.sha256"
    : >"$run/overlays.sha256"
fi
if [[ $writable == 1 ]];then
    # Only attached original-member overlays may change, never an empty peer.
    # Original backing images remain mandatory immutable hash checks.
    : >"$run/overlays.sha256"
    if [[ $members == 0 ]];then sha256sum "$run/disk1.qcow2" >>"$run/overlays.sha256";fi
    if [[ $members == 01 ]];then
        sha256sum "$run/disk3.qcow2" >>"$run/overlays.sha256"
        if [[ $layout == raidz3 ]];then sha256sum "$run/disk4.qcow2" >>"$run/overlays.sha256";fi
    fi
    if [[ $members == 1-empty-peer ]];then
        sha256sum "$run/disk0.qcow2" "$run/empty-peer.qcow2" >>"$run/overlays.sha256"
    fi
fi
if [[ $gpt == 1 || $gpt == 3 || $gui_gpt == 1 ]];then
    # Explicit format mode may alter only overlay 0, never either raw fixture.
    sha256sum "$run/disk1.qcow2" >"$run/overlays.sha256"
fi
sha256sum "$base" "$run/OpenZFSServiceNative" "$run/OpenZFSNativeImportTest" >"$run/inputs.sha256"
if [[ -n ${AROS_TEST_ENGINE_LIBRARY:-} ]];then
    sha256sum "$run/openzfs.library" >>"$run/inputs.sha256"
    if [[ -n ${AROS_TEST_ENGINE_LIBRARY_LIFETIME:-} ]];then
        sha256sum "$run/OpenZFSLibraryTest" >>"$run/inputs.sha256"
    fi
fi
runtime_maps=()
if [[ $runtime_profile == dos-posix-ata ]];then
    sha256sum "$dos/aros-base.pkg.xz" "$posix" "$ata/aros-bsp.pkg.xz" >>"$run/inputs.sha256"
    runtime_maps+=(-map "$dos/aros-base.pkg.xz" /boot/aros-base.pkg.xz
        -map "$posix" /Libs/posixc.library
        -map "$ata/aros-bsp.pkg.xz" /boot/pc-smp/aros-bsp.pkg.xz)
fi
if [[ $runtime_profile == private || $runtime_profile == dos-only || $runtime_profile == dos-posix || $runtime_profile == dos-posix-pipe-stdio ]];then
    sha256sum "$dos/aros-base.pkg.xz" >>"$run/inputs.sha256"
    runtime_maps+=(-map "$dos/aros-base.pkg.xz" /boot/aros-base.pkg.xz)
fi
if [[ $runtime_profile == posix-only || $runtime_profile == dos-posix || $runtime_profile == posix-pipe || $runtime_profile == posix-pipe-stdio || $runtime_profile == dos-posix-pipe-stdio ]];then
    sha256sum "$posix" >>"$run/inputs.sha256"
    runtime_maps+=(-map "$posix" /Libs/posixc.library)
fi
if [[ $runtime_profile == private ]];then
    sha256sum "$ata/aros-bsp.pkg.xz" "$posix" >>"$run/inputs.sha256"
    sha256sum "$partition" >"$run/partition-library.sha256"
    runtime_maps+=(-map "$ata/aros-bsp.pkg.xz" /boot/pc-smp/aros-bsp.pkg.xz
        -map "$posix" /Libs/posixc.library
        -map "$partition" /Libs/partition.library
        -map build-aros/hosted/Libs/z1.library /Libs/z1.library)
fi
if [[ -n ${AROS_TEST_FS_PACKAGE:-} ]];then sha256sum "$AROS_TEST_FS_PACKAGE" >>"$run/partition-library.sha256";fi
if [[ -n ${AROS_TEST_PIPE_POLICY_HANDLER:-} ]];then
    cp "$AROS_TEST_PIPE_POLICY_HANDLER" "$run/pipe-handler"
    cp aros/cli/PIPE-Disconnect-Test "$run/PIPE"
    if [[ ${AROS_TEST_PIPE_POLICY_LEGACY:-0} == 1 ]];then
        sed -i 's/Startup = 1/Startup = 0/' "$run/PIPE"
    fi
    sha256sum "$run/pipe-handler" "$run/PIPE" >"$run/pipe-policy.sha256"
    gui_maps+=(-map "$run/pipe-handler" /L/pipe-handler -map "$run/PIPE" /Devs/DOSDrivers/PIPE)
fi
xorriso -indev "$base" -outdev "$run/test.iso" \
    "${gui_maps[@]}" \
    -map aros/engine/grub-qemu-test.cfg /boot/grub/grub.cfg \
    "${runtime_maps[@]}" \
    -map aros/engine/security-fixture /Security \
    -map "$run/OpenZFSServiceNative" /C/OpenZFSServiceNative \
    -map "$run/OpenZFSNativeImportTest" /C/OpenZFSNativeImportTest \
    -map "$run/Startup-Native-Import" /S/Startup-Sequence \
    -boot_image any discard -boot_image grub bin_path=/boot/grub/i386-pc/eltorito.img \
    -boot_image grub boot_info_table=on -boot_image grub grub2_boot_info=on >"$run/pack.log" 2>&1
echo "Guest memory MiB: $memory"
cut_start=()
if [[ $import_rewind_cut == 1 ]];then
    cut_start=(-S)
    cp aros/engine/qmp-recovery-cut.py "$run/qmp-recovery-cut.py"
fi
"$qemu" -name "openzfs-native-import-$$" -machine "pc,accel=$accel" -cpu "$cpu" -smp "$smp" -m "$memory" \
    "${cut_start[@]}" \
    -drive "file=$run/test.iso,media=cdrom,if=ide,index=2,readonly=on" \
    "${drives[@]}" \
    -display none -nic none -boot d -no-reboot -no-shutdown "${gui_devices[@]}" \
    -qmp "unix:$run/qmp,server=on,wait=off" \
    -debugcon "file:$run/completion.log" -global isa-debugcon.iobase=0xe9 \
    -serial "file:$run/serial.log" -monitor "unix:$run/monitor,server=on,wait=off" >"$run/qemu.log" 2>&1 &
pid=$!
trap 'if kill -0 "$pid" 2>/dev/null;then kill "$pid";wait "$pid" || true;fi' EXIT
if [[ $import_rewind_cut == 1 ]];then
    # A deliberate blkdebug suspension prevents graceful QEMU draining.
    # This PID is the private child just launched above, never another VM.
    trap 'if kill -0 "$pid" 2>/dev/null;then kill -KILL "$pid";wait "$pid" || true;fi' EXIT
fi
timeout_seconds=600
if [[ $gui != 0 ]];then timeout_seconds=1800;fi
timeout_seconds=${AROS_TEST_TIMEOUT_SECONDS:-$timeout_seconds}
[[ $timeout_seconds =~ ^[1-9][0-9]{1,4}$ ]] &&
    ((timeout_seconds>=60 && timeout_seconds<=14400)) || {
    echo 'AROS_TEST_TIMEOUT_SECONDS must be between 60 and 14400' >&2;exit 2;
}
echo "Guest completion deadline: $timeout_seconds seconds"
if [[ $import_rewind_cut == 1 ]];then
    cut_args=()
    if [[ $clear_rewind == 1 ]];then cut_args=(--clear-ready);fi
    python3 "$run/qmp-recovery-cut.py" "$run" "$pid" --timeout "$timeout_seconds" "${cut_args[@]}" >"$run/recovery-cut.log" 2>&1
    cut_status=0
    wait "$pid" || cut_status=$?
    [[ $cut_status == 137 ]]
    trap - EXIT
    rg -q '^CUT: private VM at member 0 flush_to_disk, before request return' "$run/recovery-cut.log"
    sha256sum -c "$run/backing.sha256"
    compare_status=0
    qemu-img compare -f qcow2 -F raw "$run/disk0.qcow2" "$fixture/disk0.raw" >"$run/recovery-cut-compare.log" 2>&1 || compare_status=$?
    [[ $compare_status == 1 ]] || { echo 'FAIL: no changed guest-visible member bytes at cut';exit 1; }
    echo "CAPTURED: interrupted writable recovery; cold recovery not yet verified: $run"
    exit 0
fi
deadline=$((SECONDS+timeout_seconds))
usb_removed=0
usb_reinserted=0
while :;do
    if [[ $usb_reinsert != 0 && -f "$run/serial.log" ]] &&
        (( $(rg -c '^READY native USB member reinsertion member=ozmember0' "$run/serial.log" || echo 0) > usb_reinserted ));then
        # QEMU auto-removes the legacy -drive backend when unplug completes.
        usb_port=1;usb_serial=oz-member-0
        reinsert_first=0;reinsert_second=1
        if [[ $usb_swap == 1 ]];then
            reinsert_first=1;reinsert_second=0
            sha256sum "$run/disk0.qcow2" "$run/disk1.qcow2" >"$run/swapped-before.sha256"
        fi
        if [[ $usb_reinsert == 2 ]];then usb_port=3;usb_serial=oz-relocated-0;fi
        printf 'drive_add 0 if=none,id=ozusb0,file=%s/disk%s.qcow2,format=qcow2\ndevice_add usb-storage,drive=ozusb0,bus=ozusb.0,port=%s,id=ozmember0,serial=%s,removable=on\ninfo usb\n' "$run" "$reinsert_first" "$usb_port" "$usb_serial" | socat - "UNIX-CONNECT:$run/monitor" >"$run/usb-reinsert.log"
        if rg -q '^Error:' "$run/usb-reinsert.log";then
            echo "FAIL: QEMU reinsertion command rejected; see $run/usb-reinsert.log";exit 1
        fi
        rg -q 'ID: ozmember0' "$run/usb-reinsert.log"
        if [[ $usb_all_lost == 1 ]];then
            printf 'drive_add 0 if=none,id=ozusb1,file=%s/disk%s.qcow2,format=qcow2\ndevice_add usb-storage,drive=ozusb1,bus=ozusb.0,port=2,id=ozmember1,serial=oz-member-1,removable=on\ninfo usb\n' "$run" "$reinsert_second" | socat - "UNIX-CONNECT:$run/monitor" >"$run/usb-reinsert-second.log"
            if rg -q '^Error:' "$run/usb-reinsert-second.log";then exit 1;fi
            rg -q 'ID: ozmember1' "$run/usb-reinsert-second.log"
        fi
        usb_reinserted=$((usb_reinserted+1))
        cp "$run/usb-reinsert.log" "$run/usb-reinsert-round-$usb_reinserted.log"
        if [[ $usb_all_lost == 1 ]];then cp "$run/usb-reinsert-second.log" "$run/usb-reinsert-second-round-$usb_reinserted.log";fi
    fi
    if [[ $usb_remove != 0 && -f "$run/serial.log" ]] &&
        (( $(rg -c '^READY native USB member removal member=ozmember0' "$run/serial.log" || echo 0) > usb_removed ));then
        printf 'device_del ozmember0\ninfo usb\n' | socat - "UNIX-CONNECT:$run/monitor" >"$run/usb-remove.log"
        if [[ $usb_all_lost == 1 ]];then
            printf 'device_del ozmember1\ninfo usb\n' | socat - "UNIX-CONNECT:$run/monitor" >"$run/usb-remove-second.log"
        fi
        usb_removed=$((usb_removed+1))
        cp "$run/usb-remove.log" "$run/usb-remove-round-$usb_removed.log"
        if [[ $usb_all_lost == 1 ]];then cp "$run/usb-remove-second.log" "$run/usb-remove-second-round-$usb_removed.log";fi
    fi
    if [[ -f "$run/completion.log" ]];then
        if rg -q '^OZ-NATIVE-COMPLETION-v1 FAIL line=[0-9]+$' "$run/completion.log";then
            echo "FAIL: guest completion marker; see $run/completion.log";exit 1
        fi
    fi
    if [[ -f "$run/serial.log" ]];then
        if rg -q 'RESULT native (pool import|GPT readonly test|service CLI|zvol device|zvol transport|events follow) FAIL|\[PF-DBG\]|^ASSERT at|signal=6 default action=terminate' "$run/serial.log";then
            echo "FAIL: guest failure diagnostic; see $run/serial.log";exit 1
        fi
    fi
    if [[ -f "$run/completion.log" ]] && rg -qx 'OZ-NATIVE-COMPLETION-v1 PASS line=0' "$run/completion.log";then break;fi
    if ! kill -0 "$pid" 2>/dev/null;then
        echo "FAIL: QEMU exited before guest completion; see $run/qemu.log";exit 1
    fi
    if ((SECONDS>=deadline));then
        echo "INCOMPLETE: harness deadline before guest completion (GUI=$gui); stopping isolated QEMU. This alone does not establish a guest crash. Evidence: $run"
        exit 1
    fi
    sleep 1
done
printf 'quit\n' | socat - "UNIX-CONNECT:$run/monitor" >/dev/null
wait "$pid"
trap - EXIT
if [[ $import_rewind == 1 ]];then
    if [[ $import_rewind_mode == seed ]];then
        rg -q '^RESULT native recovery seed PASS generations=8 sync=8 exported=0' "$run/serial.log"
        rg -q '^RESULT native damaged rewind test PASS line=0' "$run/serial.log"
        sha256sum -c "$run/backing.sha256"
        if sha256sum -c "$run/overlays.sha256" > "$run/seed-overlay-change.log" 2>&1;then
            echo 'FAIL: recovery seed did not change either overlay';exit 1
        fi
        rg -Fqx "$run/disk0.qcow2: FAILED" "$run/seed-overlay-change.log"
        rg -Fqx "$run/disk1.qcow2: FAILED" "$run/seed-overlay-change.log"
        echo "PASS: native recovery seed created; not recovery qualification: $run"
        exit 0
    fi
    if [[ $import_rewind_dryrun == 1 ]];then
        rg -q "^RESULT native recovery assessment PASS flag=n${import_rewind_flag} recoverable-report=1 pool-absent=1" "$run/serial.log"
    fi
    if [[ $import_rewind_refuse_normal == 1 ]];then
        rg -q '^RESULT native normal recovery boundary PASS Manager-ENXIO=1 CLI-refused=1 diagnostic=1 absent=2 no-extreme-fallback=1' "$run/serial.log"
    fi
    if [[ $import_assessment == 1 ]];then
        rg -q '^RESULT native assessment identity PASS mismatch-refused=1 pool-absent=1' "$run/serial.log"
        rg -q '^RESULT native Manager assessment PASS cycles=2 structured=2 pool-absent=2' "$run/serial.log"
    fi
    if [[ $import_assess_healthy == 1 ]];then
        rg -q '^RESULT native Manager healthy assessment PASS current=2 unloaded=2' "$run/serial.log"
    elif [[ $import_rewind_mode != cold ]];then
        rg -q "^RESULT native damaged import rewind PASS cycles=2 ordinary-refused=2 absent-after-refusal=2 readonly-${import_rewind_flag}=2 exact-DOS=2 write-refused=2" "$run/serial.log"
    fi
    rg -q '^RESULT native damaged rewind test PASS line=0' "$run/serial.log"
    if [[ $clear_gui == 1 ]];then
        rg -q '^RESULT native damaged clear GUI returned PASS' "$run/serial.log"
    fi
    if [[ $clear_rewind == 1 ]];then
        rg -q '^RESULT native verbatim privilege PASS nonroot-refused=1 absent=1 root-restored=1' "$run/serial.log"
        rg -q '^RESULT native verbatim admission PASS readonly-refused=1 absent=1 writable-admitted=1' "$run/serial.log"
        if [[ $clear_assessment_only == 1 ]];then
            rg -q '^RESULT native damaged clear assessment only PASS report=1 commit=0' "$run/serial.log"
            ! rg -q '^RESULT native damaged (clear rewind|rewind commit) PASS' "$run/serial.log"
        else
            rg -q '^RESULT native damaged clear rewind PASS assessment=1 recoverable-report=1 commit=1' "$run/serial.log"
        fi
    fi
    if [[ $clear_manager == 1 ]];then
        if [[ $clear_assessment_only == 1 ]];then
            rg -q '^RESULT native Manager damaged clear assessment PASS captured-report=1 joined=1 commit=0' "$run/serial.log"
        else
            rg -q '^RESULT native Manager damaged clear PASS assessment=1 captured-report=1 commit=1 joined=2' "$run/serial.log"
        fi
    fi
    if [[ $import_rewind_manager == 1 ]];then
        rg -q '^RESULT native recovery parser PASS modes=3 guid=1 malformed=5 unchanged=1 legacy-refused=1' "$run/serial.log"
        rg -q '^RESULT native Manager recovery authorization PASS nonroot-denied=1 root-restored=1' "$run/serial.log"
        rg -q '^RESULT native assessment parser PASS modes=2 malformed=5 unchanged=1' "$run/serial.log"
        if [[ $import_assess_healthy == 0 ]];then
            rg -q '^RESULT native Manager recovery API PASS real-admission=1 upstream-rewind=1 exact-DOS=1' "$run/serial.log"
        fi
    fi
    [[ -s "$run/overlays.sha256" ]]
    sha256sum -c "$run/backing.sha256"
    if [[ $import_rewind_mode == commit && $clear_assessment_only == 0 ]];then
        rg -q "^RESULT native damaged rewind commit PASS writable-${import_rewind_flag}=1 new-file=1 flushed=1 exported=1" "$run/serial.log"
        if sha256sum -c "$run/overlays.sha256" > "$run/rewind-overlay-change.log" 2>&1;then
            echo 'FAIL: committed recovery did not change either overlay';exit 1
        fi
        rg -Fqx "$run/disk0.qcow2: FAILED" "$run/rewind-overlay-change.log"
        rg -Fqx "$run/disk1.qcow2: FAILED" "$run/rewind-overlay-change.log"
    elif [[ $import_rewind_dryrun == 1 || $clear_assessment_only == 1 ]];then
        # A writable upstream vdev probe can rewrite identical label padding.
        # Container hashes therefore do not prove changed guest disk content.
        # Keep that diagnostic, but require exact equality of BOTH full disks;
        # this is unchanged-content proof, not a claim of zero issued writes.
        sha256sum -c "$run/overlays.sha256" > "$run/assessment-overlay-hashes.log" 2>&1 || true
        for unit in 0 1;do
            qemu-img compare -f qcow2 -F raw "$run/disk$unit.qcow2" "$fixture/disk$unit.raw" \
                > "$run/assessment-disk$unit-compare.log" 2>&1
            qemu-img map --output=json "$run/disk$unit.qcow2" > "$run/assessment-disk$unit-map.json"
        done
    else
        sha256sum -c "$run/overlays.sha256"
    fi
    if [[ $import_rewind_mode != preview && $clear_assessment_only == 0 ]];then
        rg -q '^RESULT native repaired pool readback PASS ordinary-import=1 original-file=1 post-recovery-file=1 readonly=1' "$run/serial.log"
    fi
    echo "PASS: native damaged-pool rewind mode=$import_rewind_mode clear_assessment_only=$clear_assessment_only, exact DOS and verified image policy: $run"
    exit 0
fi
if [[ $labelclear_only == 1 ]];then
    marker='RESULT native raw labelclear PASS active-refused=1 exported-force=1 labels=8 pads-preserved=8 regions-zero=8 import-absent=1'
    rg -Fq "$marker" "$run/serial.log"
    sha256sum -c "$run/backing.sha256"
    label_size=$((256*1024));skip_size=$((16*1024));clear_size=$((label_size-skip_size))
    : >"$run/labelclear-results.sha256"
    for unit in 0 1;do
        original="$fixture/disk$unit.raw"
        result="$run/labelclear-result-unit$unit.raw"
        verify="$run/labelclear-result-unit$unit-gpt.log"
        qemu-img convert -O raw "$run/disk$unit.qcow2" "$result"
        sgdisk --verify "$result" >"$verify" 2>&1
        rg -q 'No problems found' "$verify"
        sector_size=$(sgdisk -p "$result" | awk '/Sector size \(logical\):/ {print $4}')
        first_sector=$(sgdisk -i 1 "$result" | awk '/First sector:/ {print $3}')
        last_sector=$(sgdisk -i 1 "$result" | awk '/Last sector:/ {print $3}')
        [[ $sector_size =~ ^[0-9]+$ && $first_sector =~ ^[0-9]+$ && $last_sector =~ ^[0-9]+$ ]]
        partition_base=$((first_sector*sector_size))
        partition_size=$(((last_sector-first_sector+1)*sector_size))
        [[ $((partition_size%label_size)) == 0 && $partition_size -ge $((4*label_size)) ]]
        starts=(
            $((partition_base+skip_size))
            $((partition_base+label_size+skip_size))
            $((partition_base+partition_size-2*label_size+skip_size))
            $((partition_base+partition_size-label_size+skip_size))
        )
        for start in "${starts[@]}";do
            pad=$((start-skip_size))
            cmp -n "$skip_size" -i "$pad:$pad" "$original" "$result"
            cmp -n "$clear_size" -i "0:$start" /dev/zero "$result"
        done
        disk_size=$(stat -c %s "$original")
        [[ $(stat -c %s "$result") == "$disk_size" ]]
        cmp -n "$partition_base" "$original" "$result"
        partition_end=$((partition_base+partition_size))
        cmp -n "$((disk_size-partition_end))" -i "$partition_end:$partition_end" "$original" "$result"
        sha256sum "$result" >>"$run/labelclear-results.sha256"
    done
    echo "PASS: native raw labelclear and host-verified label boundaries: $run"
    exit 0
elif [[ $dataset_admin_only == 1 ]];then
    rg -Fq 'RESULT native labelclear active refusal PASS forced=1' "$run/serial.log"
    rg -Fq 'RESULT native labelclear bytes PASS labels=4 pads-preserved=1 label-regions-zero=1' "$run/serial.log"
    rg -Fq 'RESULT native Manager labelclear PASS active-denied=1 readonly-active-denied=1 exported-denied=1 force-cleared=1 import-absent=1 invalid=3 cleanup=1' "$run/serial.log"
    rg -Fq 'RESULT native dataset rewrite PASS CLI=1 Manager=2 packet=1 exact-data=3 physical-refused=1 physical=1 nonroot-denied=1 readonly-denied=1 malformed-packet=2 invalid-manager=6 cleanup=1' "$run/serial.log"
    rg -Fq 'RESULT native Manager dataset upgrade PASS legacy4=1 denied-unchanged=1 version5=1 exact-data=1 invalid=3 unconfirmed=1 cleanup=1' "$run/serial.log"
    rg -Fq 'RESULT native Manager pool upgrade PASS legacy26=1 denied-unchanged=1 version28=1 features=1 reimport=1 exact-data=1 invalid=2 unconfirmed=1 cleanup=1' "$run/serial.log"
    rg -Fq 'RESULT native import rejection recovery PASS unsupported=1 no-false-enomem=1 absent-after-reject=1 valid-reimport=1 exact-data=1' "$run/serial.log"
    sha256sum -c "$run/backing.sha256"
    echo "PASS: focused dataset administration and unchanged native fixture backings: $run"
    exit 0
elif [[ $live_create_only == 1 ]];then
    rg -Fq 'RESULT native live I/O rates PASS rows=8 generation=1 paired-samples=1 positive-write-leaves=2' "$run/serial.log"
    rg -Fq 'RESULT native post-reopen live create PASS reguid=1 clear=1 reopen=1 create-destroy=1' "$run/serial.log"
    sha256sum -c "$run/backing.sha256"
    echo "PASS: focused post-reopen live create and unchanged native fixture backings: $run"
    exit 0
fi
if [[ $usb_reinsert != 0 ]];then
    [[ $usb_reinserted == "$usb_rounds" && $usb_removed == "$usb_rounds" ]]
    if [[ $usb_suspend_guard == 0 && $usb_swap == 0 ]];then
        rg -q 'RESULT native USB reinsertion PASS no-auto-adopt=1 fresh-import=ONLINE' "$run/serial.log"
    fi
    if [[ $usb_reinsert == 2 ]];then
        rg -q 'RESULT native USB relocation PASS admitted-unit=2 identity=pool-label old-path-refused=1' "$run/serial.log"
    fi
fi
if [[ -n $usb_cold ]];then
    if [[ $usb_cold_kind == acknowledged ]];then
        [[ $(rg -c "^RESULT native USB cold acknowledged PASS chunks=$ack_chunks exact-prefix=1 sentinel=1 readonly=1" "$run/serial.log") == 2 ]]
    else
        [[ $(rg -c '^RESULT native USB cold survivor PASS exact-read=1 readonly=1' "$run/serial.log") == 2 ]]
    fi
fi
if [[ $usb_remove != 0 && $usb_all_lost == 0 ]];then
    [[ $usb_removed == 1 ]]
    rg -q 'RESULT native USB member removal PASS idle-health=DEGRADED' "$run/serial.log"
    if [[ $usb_remove == 2 ]];then
        rg -q 'RESULT native USB degraded write PASS flush=1 survivor-reimport=1 exact-read=1' "$run/serial.log"
    fi
fi
if [[ $usb_all_lost == 1 ]];then
    rg -q 'RESULT native USB total loss PASS health=UNAVAIL(-or-SUSPENDED)? responsive=1' "$run/serial.log"
fi
if [[ $usb_all_lost_writable == 1 && $usb_suspend_guard == 0 ]];then
    [[ $(rg -c '^RESULT native USB reinsertion readiness PASS partition-catalogs=2 readonly-probe=1' "$run/serial.log") == "$usb_rounds" ]]
    [[ $(rg -c '^RESULT native suspended clear invalid target PASS error=ENODEV suspended=1' "$run/serial.log") == "$usb_rounds" ]]
fi
if [[ $usb_swap == 1 ]];then
    if [[ $usb_cli_clear == 1 ]];then
        rg -q '^RESULT native CLI wrong-member clear PASS refused=1 suspended=1' "$run/serial.log"
    fi
    rg -q 'RESULT native wrong-member readmission PASS clear=ESTALE suspended=1' "$run/serial.log"
    sha256sum -c "$run/swapped-before.sha256"
elif [[ $usb_suspend_guard == 1 ]];then
    rg -q 'RESULT native suspended history PASS summary=EAGAIN raw=EAGAIN empty-errors=2 errno-detail=2 responsive=1' "$run/serial.log"
    rg -q 'RESULT native suspended export guard PASS export=EAGAIN destroy=EAGAIN responsive=1' "$run/serial.log"
elif [[ $usb_all_lost_writable == 1 ]];then
    if [[ $usb_cli_clear == 1 ]];then
        [[ $(rg -c '^RESULT native CLI suspended clear PASS returned=0' "$run/serial.log") == "$usb_rounds" ]]
    fi
    [[ $(rg -c '^RESULT native USB writable idle recovery PASS synced-before-loss=1 fresh-import=ONLINE' "$run/serial.log") == "$usb_rounds" ]]
fi
if [[ $usb_inflight == 1 ]];then
    [[ $(rg -Fc 'RESULT native USB in-flight write interruption PASS wrote-before-error=1 error-observed=1' "$run/serial.log") == 1 ]]
    [[ $(rg -Fc 'RESULT native suspended unmount guard PASS error=EAGAIN responsive=1 namespace-retained=1' "$run/serial.log") == 1 ]]
    [[ $(rg -Fc 'RESULT native USB in-flight recovery PASS committed-sentinel=1 fresh-import=ONLINE' "$run/serial.log") == 1 ]]
    rg -q '^RESULT native USB acknowledged writes PASS chunks=[1-9][0-9]* bytes=[1-9][0-9]* readonly-reimport=1 exact-prefix=1' "$run/serial.log"
fi
if [[ $usb_created == 1 ]];then
    rg -q '^RESULT native created pool recovery PASS first-export-after-clear=1 exact-read=1' "$run/serial.log"
    rg -q '^RESULT native created recovery test PASS line=0' "$run/serial.log"
    sha256sum -c "$run/backing.sha256"
    echo "PASS: native created pool loss before first export and exact DOS recovery: $run"
    exit 0
fi
if [[ ${AROS_TEST_BACKING_MEDIA_CHANGE:-0} == 1 ]];then
    rg -q 'RESULT native backing media change PASS read-before=1 eject=1 read-denied=1 write-denied=1 flush-denied=1 inquiry-denied=1 bound-reopen-denied=2 rebind-denied=1 generation-before-io=1 native-error=1 polling=1 joined=1 restart=1 notification=1' "$run/serial.log"
fi
if [[ -n ${AROS_TEST_ENGINE_LIBRARY_LIFETIME:-} ]];then
    rg -q 'RESULT shared engine library lifecycle PASS invalid-mode=2 nested-open=1 foreign-refused=1 reload-other-task=1' "$run/serial.log"
    rg -q 'RESULT shared engine denied return PASS status=20 repeated=10 caller-exit=7' "$run/serial.log"
fi
if [[ ${AROS_TEST_SERVICE_RELEASE:-0} == 1 ]];then
    rg -q 'RESULT native service release startup PASS diagnostic-modes-refused=3 malformed-refused=2 unauthorized-writable-refused=1 no-ports=1' "$run/serial.log"
fi
if [[ -n ${AROS_TEST_WHOLE_CLI_LINK:-} ]];then
  if [[ ${AROS_TEST_DOS_PUBLICATION_FAILURE:-0} == 1 ]];then
    rg -q 'DOS PUBLICATION FIXTURE registration errno=17' "$run/serial.log"
    rg -q 'DOS PUBLICATION FIXTURE cleanup errno=5' "$run/serial.log"
    rg -q 'RESULT native DOS publication recovery PASS failed-not-mounted=1 retry=1 unmount=1 destroy=1' "$run/serial.log"
    rg -q 'DOS RETIRED CLOSE FIXTURE cleanup errno=5' "$run/serial.log"
    rg -q 'RESULT native retired close recovery PASS detach-error=1 sentinel-held=1 remount-blocked=1 active-hidden=1 retry=1 distinct-remount=1 late-packet=1 cleanup=1' "$run/serial.log"
  fi
  if [[ ${AROS_TEST_MOUNT_REGISTRY_ALLOC:-0} == 1 ]];then
    rg -q 'MOUNT REGISTRY ALLOCATION FIXTURE calloc errno=12' "$run/serial.log"
    rg -q 'MOUNT REGISTRY ALLOCATION FIXTURE realloc errno=12' "$run/serial.log"
    rg -q 'RESULT native mount registry allocation recovery PASS calloc=1 realloc=1 unpublished=2 retry=2 cleanup=2' "$run/serial.log"
  fi
  if [[ ${AROS_TEST_ERROR_LOG:-0} == 1 ]];then
    if [[ ${AROS_TEST_ERROR_LOG_HEAD:-0} == 1 ]];then
      rg -q 'RESULT native head error log PASS active=1 birth-aware=1 scrub-cleared=1' "$run/serial.log"
      rg -q 'RESULT native head error attribution PASS snapshot=1 clone=1 rewritten-head-excluded=1' "$run/serial.log"
    fi
    rg -q 'RESULT native error log privilege PASS nonroot-denied=1' "$run/serial.log"
    rg -q "RESULT native Manager error details PASS seeded=${AROS_TEST_ERROR_LOG_SEEDED:-0} upstream-output=1 force-denied=1 options-denied=1" "$run/serial.log"
    if [[ ${AROS_TEST_ERROR_LOG_GUI:-0} == 1 ]];then
      rg -q "RESULT native Manager error GUI lifecycle PASS seeded=${AROS_TEST_ERROR_LOG_SEEDED:-0} actual-manager=1 unchanged-log=1 visual-verification=separate" "$run/serial.log"
    fi
    rg -q "RESULT native error log wire PASS seeded=${AROS_TEST_ERROR_LOG_SEEDED:-0} malformed=10 canaries=2 reused-path=1" "$run/serial.log"
    rg -q "RESULT native CLI error log PASS seeded=${AROS_TEST_ERROR_LOG_SEEDED:-0} status-path=1 cleanup=1" "$run/serial.log"
  fi
  rg -q 'RESULT native management API version PASS wire=[0-9]+ advertised=[0-9]+ old-version-rejected=1' "$run/serial.log"
  rg -q 'RESULT native management capability catalogue PASS supported=[0-9]+ unavailable=[0-9]+ exact=1' "$run/serial.log"
  if [[ ${AROS_TEST_RECEIVED_PROPERTIES:-0} == 1 ]];then
    rg -q 'RESULT native CLI received properties PASS stream-props=1 native=1 user=1 local-override=1 inherit-received=1' "$run/serial.log"
    if [[ ${AROS_TEST_RECEIVED_EXPANSION:-0} == 1 ]];then
      rg -q 'RESULT native CLI received expansion PASS properties=96 value-bytes=3072 initial-capacity=262144 exact-readbacks=2 cleanup=1' "$run/serial.log"
    fi
    rg -q 'RESULT native received wire PASS absent=1 tiny=1 malformed=12 guards=1 recovered=1' "$run/serial.log"
    rg -q 'RESULT native Manager received restore PASS native=1 user=1 unconfirmed=1 bad-option=1 force-denied=1 CLI-readback=1' "$run/serial.log"
    if [[ ${AROS_TEST_RECEIVED_GUI:-0} == 1 ]];then
      rg -q 'RESULT native Manager received GUI PASS actual-manager=1 native=1 user=1 CLI-readback=2' "$run/serial.log"
    fi
  fi
  if [[ ${AROS_TEST_POOL_PAGINATION:-0} == 1 ]];then
    rg -q 'RESULT native pool pagination PASS pages=2 pools=26 unique=26 exact-filter=1 end-cursor=1 cleanup=25' "$run/serial.log"
  fi
  rg -q 'RESULT native I/O rate arithmetic PASS delta=1 reset=1 generation=1 unavailable=1 eviction=1 overflow=1' "$run/serial.log"
  rg -q 'RESULT native I/O refresh scheduler PASS busy=1 rollback=1 no-catchup=1 stop=1' "$run/serial.log"
  if [[ $writable != 2 ]];then
    rg -q 'RESULT native live I/O rates PASS rows=8 generation=1 paired-samples=1 positive-write-leaves=2' "$run/serial.log"
  fi
  if [[ ${AROS_TEST_ZVOL_TRANSPORT:-0} == 1 ]];then
    rg -q 'RESULT native dataset rewrite PASS CLI=1 Manager=2 packet=1 exact-data=3 physical-refused=1 physical=1 nonroot-denied=1 readonly-denied=1 malformed-packet=2 invalid-manager=6 cleanup=1' "$run/serial.log"
    rg -q 'RESULT native Manager dataset upgrade PASS legacy4=1 denied-unchanged=1 version5=1 exact-data=1 invalid=3 unconfirmed=1 cleanup=1' "$run/serial.log"
    rg -q 'RESULT native Manager volume delegation PASS granted=1 created=1 revoked=1 refused-absent=1' "$run/serial.log"
    rg -q 'RESULT native Manager permission API PASS allow=1 unallow=1 invalid=3 self-grant-denied=1 real-create=1 revoked-create-denied=1' "$run/serial.log"
    rg -q 'RESULT native Manager permission scope matrix PASS principals=3 scopes=3 cases=9 revoked=9' "$run/serial.log"
    rg -q 'RESULT native Manager named permissions PASS nested-set=1 changed-set-denial=1 creator-snapshot=1 removed-creator=1 existing-grant-retained=1' "$run/serial.log"
    rg -q 'RESULT native permission revoke all PASS local-only=1 descendants-retained=1 both-denied=1 other-principal-retained=1 creator=1 named-set=1 invalid=3' "$run/serial.log"
    rg -q 'RESULT native recursive revoke PASS datasets=3 denied=3 other-principal-retained=1 outside-subtree-retained=1' "$run/serial.log"
    rg -q 'RESULT native recursive revoke failure PASS completed=1 precise-target=1 ancestors-retained=1 self-retry=3 explicit-permission-preserved-others=1' "$run/serial.log"
    rg -q 'RESULT native accounting PASS quota-rows=30 pages=2 empty=3 delegated=1 other-metric-denied=1 revoked=1 invalid=1' "$run/serial.log"
    rg -q 'RESULT native accounting metrics PASS quota-kinds=3 objectquota-kinds=3 usage-kinds=6 defaults=6 units=1' "$run/serial.log"
    rg -q 'RESULT native accounting setter PASS quota-kinds=6 defaults=6 delegated=1 denied=1 invalid=4 cli-readback=1' "$run/serial.log"
    rg -q 'RESULT native default quota enforcement PASS denied-create=4 project-zero-exempt=2 cleared-create=6' "$run/serial.log"
    rg -q 'RESULT native project packets PASS get=1 set=1 inherited=1 nonowner=1 malformed=1 stale=1 readonly=1' "$run/serial.log"
    rg -q 'RESULT native upstream project CLI PASS list=1 set=1 clear=1 recursive=1 packet-readback=1 nonowner=1 readonly=1' "$run/serial.log"
    rg -q 'RESULT native project worker PASS malformed=13 nonowner=1 owner-set=1 list=1 check=1 clear-keep=1 clear=1 query-unconfirmed=2' "$run/serial.log"
    rg -q 'RESULT native CLI encrypted create PASS upstream-create=1 key-available=1 DOS-roundtrip=1 cleanup=1' "$run/serial.log"
    rg -q 'RESULT native Manager encrypted create PASS raw=1 malformed=1 unauthorized=1 key-reload=1 exact-DOS=1' "$run/serial.log"
    rg -q 'RESULT native Manager crypto formats PASS hex=1 passphrase=1 volume=1 CLI-key-reload=3 exact-DOS=2' "$run/serial.log"
    rg -q 'RESULT native Manager PBKDF2 creation PASS custom=350001 invalid-refused=3' "$run/serial.log"
    rg -q 'RESULT native encryption invalid material PASS raw-length=2 hex=2 passphrase-length=2 absent=6 valid-retry=1' "$run/serial.log"
    rg -q 'RESULT native raw encrypted replication PASS source-locked=1 receive-locked=1 original-key=1 exact-DOS=1' "$run/serial.log"
    rg -q 'RESULT native raw send delegation PASS raw-grant=1 plaintext-denied=1' "$run/serial.log"
    rg -q 'RESULT native raw incremental PASS source-locked=1 receive-locked=1 original-key=1 changed-bytes=1' "$run/serial.log"
    rg -q 'RESULT native raw resume PASS truncated=1 token=1 delegated=1 locked=1 exact-MiB=1 cleared=1' "$run/serial.log"
    rg -q 'RESULT native CLI send estimate PASS ordinary=1 raw-locked=1 raw-resume=1 raw-incremental=1' "$run/serial.log"
    rg -q 'RESULT native CLI raw stream PASS locked-source=1 upstream-send=1 CLI-receive=1 original-key=1 exact-MiB=1' "$run/serial.log"
    rg -q 'RESULT native CLI plain stream PASS modern-receive=1 own-volume=1 encryption-off=1 exact-MiB=1' "$run/serial.log"
    rg -q 'RESULT native CLI diff PASS snapshots=2 added-path=1 cleanup=1' "$run/serial.log"
    rg -q 'RESULT native CLI diff changes PASS added=1 modified=1 renamed=1 removed=1' "$run/serial.log"
    rg -q 'RESULT native CLI diff live PASS implicit=1 explicit-filesystem=1 cleanup=1' "$run/serial.log"
    rg -q 'RESULT native Manager diff worker PASS snapshots=1 live=1 malformed=6 cleanup=1' "$run/serial.log"
    rg -q 'RESULT native Manager diff delegation PASS denied=1 diff-only=1 revoked=1 cleanup=1' "$run/serial.log"
    rg -q 'RESULT native Manager diff report PASS overflow=1 exact-paths=128 user-cleanup=1' "$run/serial.log"
    rg -q 'RESULT native Manager prefetch PASS ddt=1 brt=1 both=1 invalid=1 unconfirmed=1 nonroot-denied=1' "$run/serial.log"
    rg -q 'RESULT native Manager ddtprune PASS populated=1 entries-after=0 exact-KiB=512 days-empty=1 invalid=7 unconfirmed=1 nonroot-denied=1 cleanup=1' "$run/serial.log"
    rg -q 'RESULT native DDT prune recovery PASS populated-invalid=1 valid-after-error=1 empty-invalid=1 valid-after-empty-error=1' "$run/serial.log"
    rg -q 'RESULT native DDT prune shared PASS duplicate-written=1 shared-retained=1 duplicate-read=1 duplicate-deleted=1 unique-pruned=1 survivor-read=1' "$run/serial.log"
    rg -q 'RESULT native Manager pool upgrade PASS legacy26=1 denied-unchanged=1 version28=1 features=1 reimport=1 exact-data=1 invalid=2 unconfirmed=1 cleanup=1' "$run/serial.log"
    rg -q 'RESULT native import rejection recovery PASS unsupported=1 no-false-enomem=1 absent-after-reject=1 valid-reimport=1 exact-data=1' "$run/serial.log"
    rg -q 'RESULT native labelclear active refusal PASS forced=1' "$run/serial.log"
    rg -q 'RESULT native channel program PASS CLI=1 Manager=1 arguments=1 json=1 nonroot-denied=1 nosync-denied=1 snapshot=1 instruction-limit=1 recovery=1 invalid=7 cleanup=1' "$run/serial.log"
    rg -q 'RESULT native channel mounted rename PASS refused=1 original-retained=1 unmounted-rename=1' "$run/serial.log"
    rg -q 'RESULT native channel limits PASS instructions=1 memory=1 syntax=1 valid-after-errors=1' "$run/serial.log"
    rg -q 'RESULT native redaction PASS Manager=1 nonroot-denied=1 invalid=3 bookmark-guid=1 plain-secret=1 redacted-secret-absent=1 public-retained=1 receive-guid=1 cleanup=1' "$run/serial.log"
    rg -q 'RESULT native Manager mounted redacted receive PASS received=1 retained-read-denied=1 namespace-ended=1 unmounted=1 cleanup=1' "$run/serial.log"
    rg -q 'RESULT native CLI mounted redacted receive PASS received=1 retained-read-denied=1 namespace-ended=1 unmounted=1 cleanup=1' "$run/serial.log"
    rg -q 'RESULT native multi-redaction PASS selected=2 referenced-retained=1 absent-both-omitted=1 public-retained=1' "$run/serial.log"
    rg -q 'RESULT native redacted resume PASS truncated=1 CLI-token=1 secret-absent=1 receive-guid=1 token-cleared=1 incremental-origin=1 DOS-MiB=1 cleanup=1' "$run/serial.log"
    rg -q 'RESULT native Manager redacted resume PASS token=1 missing-bookmark-denied=1 output-absent=1 replacement-bookmark=1 unauthorized-denied=1 secret-absent=1 received=1 DOS-MiB=1 cleanup=1' "$run/serial.log"
    rg -q 'RESULT native Manager engine policy PASS properties=9 set=18 inherit=9 invalid=9 unchanged=9 unauthorized=1 DOS-KiB=256 cleanup=1' "$run/serial.log"
    rg -q 'RESULT native Manager dnodesize feature PASS disabled-refused=1 unchanged=1 enabled=1' "$run/serial.log"
    rg -q 'RESULT native Manager dnodesize values PASS values=7 snapshot-refused=2 volume-refused=2 cleanup=1' "$run/serial.log"
    rg -q 'RESULT native admission list PASS retained=1 completed=1 worker=1 foreign-hidden=1 released-absent=1' "$run/serial.log"
    rg -q 'RESULT native Manager engine policy delegation PASS set=1 inherit=1 other-denied=1 revoked-denied=1' "$run/serial.log"
    if [[ ${AROS_TEST_ENGINE_POLICY_GUI:-0} == 1 ]];then
        rg -q 'RESULT native Manager engine policy GUI PASS checksum=fletcher4 source=local CLI-verified=1' "$run/serial.log"
    fi
    rg -q 'RESULT native Manager encrypted copies PASS copies3-refused=1 unchanged=1 copies2=1 restored=1' "$run/serial.log"
    rg -q 'RESULT native Manager creation limits PASS quota=1 refquota=1 reservation=1 refreservation=1 recordsize=1 none=1 invalid-absent=1' "$run/serial.log"
    rg -q 'RESULT native Manager bootfs PASS set=1 readback=1 clear=1 cross-pool-refused=1 missing-preserves=1 unprivileged-refused=1' "$run/serial.log"
    rg -q 'RESULT native Manager creation cleanup PASS insufficient-space=1 absent=1 same-name-retry=1' "$run/serial.log"
    rg -q 'RESULT native redacted resume malformed wire PASS version=1 truncated=1 checksum=1 output-absent=3' "$run/serial.log"
    rg -q 'RESULT native redacted incremental resume PASS CLI=1 Manager=1 missing-base=1 intact-base-refused=1 wrong-redaction-set=1 output-absent=1 DOS-MiB=2 token-cleared=2 cleanup=1' "$run/serial.log"
    rg -q 'RESULT Manager redacted send PASS secret-absent=1 public-retained=1 receive-guid=1' "$run/serial.log"
    rg -q 'RESULT Manager redacted send refusal PASS malformed=6 nonroot-denied=1 output-absent=1' "$run/serial.log"
    rg -q 'RESULT native events PASS CLI=1 independent-readers=2 Manager=1 clear-confirmed=1 invalid=2 empty=1' "$run/serial.log"
    rg -q 'RESULT native events permissions PASS nonroot-list-denied=1 nonroot-clear-denied=1 queue-retained=1' "$run/serial.log"
    rg -q 'RESULT native events follow PASS concurrent-status=1 scrub-start=1 scrub-finish=1 break-completed=1' "$run/serial.log"
    rg -q 'RESULT native events filter PASS excluded-empty=1 included-start-finish=1' "$run/serial.log"
    rg -q 'RESULT native redacted incremental PASS received=1 redacted-origin=1 unredacted=1 DOS-public-KiB=128 DOS-replacement-KiB=128' "$run/serial.log"
    rg -q 'RESULT native labelclear bytes PASS labels=4 pads-preserved=1 label-regions-zero=1' "$run/serial.log"
    rg -q 'RESULT native Manager labelclear PASS active-denied=1 readonly-active-denied=1 exported-denied=1 force-cleared=1 import-absent=1 invalid=3 cleanup=1' "$run/serial.log"
    rg -q 'RESULT native CLI pool wait PASS idle=1 scrub=1 all=1' "$run/serial.log"
    rg -q 'RESULT native Manager pool capacity PASS upstream-size=1 allocated=1 free=1' "$run/serial.log"
    rg -q 'RESULT native Manager mounted promotion PASS collision-refused=1 retained-handles=2 ancestry=1 independent-write=1' "$run/serial.log"
    rg -q 'RESULT native published snapshot promotion PASS busy=2 Manager-detach=1 CLI-detach=2 remount-read=2' "$run/serial.log"
    rg -q 'RESULT native Manager promotion authority PASS ungranted-denied=1 clone-only-denied=1 both-granted=1 revoked=2 snapshot-busy-preserved=1' "$run/serial.log"
    if [[ ${AROS_TEST_PROMOTION_GUI:-0} == 1 ]];then
        rg -q 'RESULT native published snapshot promotion GUI PASS mounted-before=1 detached=1 remount-read=1' "$run/serial.log"
        rg -q 'RESULT native Manager mounted promotion GUI PASS actual-manager=1 ancestry=1 preserved-files=2' "$run/serial.log"
    fi
    rg -q 'RESULT native mounted incremental receive PASS cycles=2 exact-DOS=1 resumed-wait=1' "$run/serial.log"
    rg -q 'RESULT native Manager mounted receive PASS incremental=1 exact-files=2 resumed-wait=1 rollback=1' "$run/serial.log"
    rg -q 'RESULT native Manager mounted receive recovery PASS truncated=1 baseline-preserved=1 retry=1' "$run/serial.log"
    rg -q 'RESULT native Manager mounted receive noforce PASS modified-refused=1 local-preserved=1 baseline-preserved=1' "$run/serial.log"
    rg -q 'RESULT native Manager mounted receive force PASS explicit=1 invalid-flags-refused=2 local-discarded=1 incoming-exact=1 rollback=1' "$run/serial.log"
    rg -q 'RESULT native Manager forced receive delegation PASS append-denied=1 local-preserved=1 receive-granted=1 revoked-denied=1' "$run/serial.log"
    if [[ ${AROS_TEST_RECEIVE_GUI:-0} == 1 ]];then
        rg -q 'RESULT native Manager mounted receive GUI PASS actual-manager=1 exact-files=2 resumed-wait=1 rollback=1' "$run/serial.log"
        if [[ ${AROS_TEST_FORCE_RECEIVE_GUI:-0} == 1 ]];then
            rg -q 'RESULT native Manager forced receive GUI PASS actual-manager=1 local-discarded=1' "$run/serial.log"
        fi
    else
        rg -q 'RESULT native Manager mounted receive delegation PASS ungranted-denied=1 partial-denied=1 granted=1 revoked-denied=1' "$run/serial.log"
    fi
    rg -q 'RESULT native mounted receive recovery PASS truncated=2 preserved=2 retried=2' "$run/serial.log"
    rg -q 'RESULT native Manager mounted rollback PASS cycles=2 newer-refused=2 missing-refused=2 DOS-restored=2' "$run/serial.log"
    rg -q 'RESULT native Manager rollback delegation PASS denied=2 granted=2 revoked=2' "$run/serial.log"
    if [[ ${AROS_TEST_ROLLBACK_GUI:-0} == 1 ]];then
        rg -q 'RESULT native Manager mounted rollback GUI PASS actual-manager=1 discarded=1 baseline-preserved=1' "$run/serial.log"
    fi
    rg -q 'RESULT native CLI filesystem wait PASS deleteq=1 all=1 absent-refused=1' "$run/serial.log"
    rg -q 'RESULT native Manager wait PASS selected=1 all=1 malformed=6' "$run/serial.log"
    rg -q 'RESULT native Manager filesystem wait PASS selected=1 all=1 malformed=7' "$run/serial.log"
    if [[ ${AROS_TEST_WAIT_PENDING:-0} == 1 ]];then
        rg -q 'RESULT native live request list PASS wait-visible=1 in-flight=1 drained-absent=1' "$run/serial.log"
        rg -q 'RESULT native live request identity PASS foreign-hidden=1' "$run/serial.log"
        rg -q 'RESULT native pending wait PASS pending=1 concurrent-status=1 stop=1 drained=1' "$run/serial.log"
        rg -q 'WAIT TEST upstream result errno=0 waited=1' "$run/serial.log"
        rg -q 'FS WAIT TEST upstream result errno=0 waited=1' "$run/serial.log"
        rg -q 'RESULT native pending filesystem wait PASS pending=1 concurrent-list=1 released=1 drained=1' "$run/serial.log"
        rg -q 'FS WAIT TEST upstream result errno=4 waited=1' "$run/serial.log"
        rg -q 'RESULT native pending filesystem cancel PASS unmount=1 interrupted=1 remount=1' "$run/serial.log"
        rg -q 'RESULT native pending filesystem rollback PASS pending=1 interrupted=1 rollback=1 resumed-wait=1' "$run/serial.log"
        rg -q 'RESULT native pending filesystem receive PASS pending=1 interrupted=1 received=1 resumed-wait=1 exact-DOS=1' "$run/serial.log"
    fi
    rg -q 'RESULT native CLI incremental stream PASS own-volume=1 exact-128KiB=1' "$run/serial.log"
    rg -q 'RESULT native CLI resumed stream PASS truncated=1 token=1 own-volume=1 exact-MiB=1' "$run/serial.log"
    rg -q 'RESULT native CLI raw resume PASS truncated=1 source-locked=1 receiver-locked=1 original-key=1 exact-MiB=1 cleared=1' "$run/serial.log"
    rg -q 'RESULT native CLI pipeline PASS concurrent=1 exact-MiB=1' "$run/serial.log"
    rg -q 'RESULT native CLI pipeline repeat PASS runs=8 exact-MiB=1 reuse-pipe=1' "$run/serial.log"
    if [[ ${AROS_TEST_PIPE_POLICY_LEGACY:-0} == 1 ]];then
        rg -q 'RESULT native CLI pipeline legacy PASS reader-reopen=1 sender-success=1' "$run/serial.log"
    else
        rg -q 'RESULT native CLI pipeline reader-close PASS sender-failed=1' "$run/serial.log"
        rg -q 'RESULT native pipe partial close PASS short-write=1 next-write-failed=1' "$run/serial.log"
    fi
    rg -q 'RESULT native raw incremental resume PASS truncated=1 from-required=1 locked=1 exact-MiB=1 cleared=1' "$run/serial.log"
    rg -q 'RESULT native Manager mounted raw receive PASS locked-source=1 mounted-target=1 exact-DOS=1 key-retained=1 wait=1' "$run/serial.log"
    if [[ ${AROS_TEST_RAW_MOUNT_FORCE:-0} == 1 ]];then
        rg -q 'RESULT native Manager writable mounted raw receive PASS ordinary-refused=1 explicit-force=1 prior-bytes=1' "$run/serial.log"
    fi
    rg -q 'RESULT native Manager encryption alias PASS on=aes-256-gcm off-with-key-refused=1' "$run/serial.log"
    rg -q 'RESULT native encrypted volume I/O PASS write=1 flush=1 key-reload=1 exact-block-read=1' "$run/serial.log"
    if [[ ${AROS_TEST_ENCRYPTED_CREATE_GUI:-0} == 1 ]];then
        rg -q 'RESULT native encrypted create GUI PASS actual-manager=1 CLI-properties=1 key-reload=1 exact-DOS=1' "$run/serial.log"
    fi
    rg -q 'RESULT native CLI key lifecycle PASS mounted-busy=1 nonowner-denied=1 wrong-key=1 reload=1 replacement=1 old-key-denied=1 exact-read=1' "$run/serial.log"
    if [[ ${AROS_TEST_PROJECT_GUI:-0} == 1 ]];then
        rg -q 'RESULT native project GUI PASS actual-manager=1 recursive-set=1 independent-cli=2' "$run/serial.log"
    fi
    rg -q 'RESULT native accounting default reads PASS properties=6 exact=1' "$run/serial.log"
    if [[ ${AROS_TEST_ACCOUNTING_SET_GUI:-0} == 1 ]];then
      rg -q 'RESULT native accounting setter GUI PASS actual-manager=1 independent-cli=2' "$run/serial.log"
    fi
    rg -q 'RESULT native delegated administration PASS allow-required=1 held-right-granted=1 escalation-denied=1 allow-redelegation-denied=1 other-revoke-denied=1 self-revoke=1 inherited-authority=1 self-all=1 revoked-denial=1' "$run/serial.log"
    if [[ ${AROS_TEST_PERMISSION_GUI:-0} == 1 ]];then
      rg -q 'RESULT native Manager permission GUI PASS actual-grant=1 actual-revoke=1 independent-create=1 revoked-denial=1' "$run/serial.log"
    fi
    rg -q 'RESULT native zvol discard PASS freed-block-zero=1 partial-edges-preserved=1 subblock-noop=1 bounds=1 overflow=1' "$run/serial.log"
    rg -q 'RESULT native zvol transport PASS rw=1 flush=1 geometry=1 bounds=1 readonly=1 wrong-type=1 unauthorized=1 stale=1 session-cleanup=1' "$run/serial.log"
  fi
  if [[ ${AROS_TEST_ZVOL_TRANSPORT:-0} == 1 ]];then
    rg -q 'RESULT native Manager volume authorization PASS unprivileged-denied=1 privileged-created=1 geometry=1' "$run/serial.log"
    if [[ ${AROS_TEST_ZVOL_CREATE_GUI:-0} == 1 ]];then
      rg -q 'RESULT native Manager volume GUI PASS actual-manager=1 independently-opened=1 exact-geometry=1' "$run/serial.log"
    fi
  fi
  if [[ ${AROS_TEST_ZVOL_DISCARD_PERSIST:-none} == write ]];then
    rg -q 'RESULT native zvol discard retained PASS bytes=24576 zero-offset=8192 zero-length=8192' "$run/serial.log"
  elif [[ ${AROS_TEST_ZVOL_DISCARD_PERSIST:-none} == read ]];then
    rg -q 'RESULT native Manager volume readonly creation PASS refused=1 absent=1' "$run/serial.log"
    rg -q 'RESULT native Manager permission readonly PASS allow-denied=1 unallow-denied=1' "$run/serial.log"
    rg -q 'RESULT native permission revoke all readonly PASS refused=1' "$run/serial.log"
    rg -q 'RESULT native zvol discard cold PASS zero-block=1 exact-neighbors=1 readonly-denied=1' "$run/serial.log"
  fi
  if [[ ${AROS_TEST_PERMISSION_PERSIST:-none} == write ]];then
    rg -q 'RESULT native permissions retained PASS synced=1' "$run/serial.log"
  elif [[ ${AROS_TEST_PERMISSION_PERSIST:-none} == read || ${AROS_TEST_PERMISSION_PERSIST:-none} == authorize ]];then
    rg -q 'RESULT native permission list boundaries PASS cases=15 empty=1 end=1 invalid-cursor=2 missing=1 snapshot=1 invalid-fields=9' "$run/serial.log"
    rg -q 'RESULT native permission list PASS structured=1 ancestor-source=1 sets=1 scopes=1 acquired-grant=1' "$run/serial.log"
    rg -q 'RESULT native permissions cold PASS sets=1 acquired-grant=1 removed-creator=1 refused-objects-absent=1' "$run/serial.log"
    if [[ ${AROS_TEST_PERMISSION_PERSIST} == authorize ]];then
      rg -q 'RESULT native permission list pagination PASS ordinary-user=1 pages=2 rows=35 unique-extra-grants=30' "$run/serial.log"
      rg -q 'RESULT native permissions cold authorization PASS retained-allowed=1 removed-denied=1 changed-set-denied=1 regrant-allowed=1' "$run/serial.log"
      if sha256sum -c "$run/overlays-before.sha256" >"$run/overlay-change.log" 2>&1;then
        echo 'FAIL: cold authorization produced no persistent overlay changes';exit 1
      fi
      rg -Fxq "$run/disk0.qcow2: FAILED" "$run/overlay-change.log"
      rg -Fxq "$run/disk1.qcow2: FAILED" "$run/overlay-change.log"
    fi
  fi
  if [[ ${AROS_TEST_ENCRYPTION_PERSIST:-none} == write ]];then
    rg -q 'RESULT native raw resume retained PASS pending=1 source-locked=1 synced=1 CLI=1' "$run/serial.log"
    rg -q 'RESULT native CLI encryption retained PASS synced=1 replacement-key=1' "$run/serial.log"
    rg -q 'RESULT native API encryption retained PASS hex=1 passphrase=1 volume=1 synced=1' "$run/serial.log"
    rg -q 'RESULT native raw receive retained PASS incremental=1 synced=1' "$run/serial.log"
  elif [[ ${AROS_TEST_ENCRYPTION_PERSIST:-none} == read || ${AROS_TEST_ENCRYPTION_PERSIST:-none} == resume ]];then
    rg -q 'RESULT native CLI encryption cold PASS locked=1 old-key-denied=1 verify-only=1 new-key=1 exact-read=1 readonly=1 unload=1' "$run/serial.log"
    rg -q 'RESULT native API encryption cold PASS hex=1 passphrase=1 volume=1 verify-only=3 exact-read=3 readonly=3' "$run/serial.log"
    rg -q 'RESULT native raw receive cold PASS locked=1 original-key=1 incremental-bytes=1 snapshots=2 readonly=1' "$run/serial.log"
    if [[ ${AROS_TEST_ENCRYPTION_PERSIST} == resume ]];then
      rg -q 'RESULT native raw resume cold PASS persisted-token=1 locked=1 original-key=1 exact-MiB=1 cleared=1 CLI=1' "$run/serial.log"
      if sha256sum -c "$run/overlays-before.sha256" >"$run/overlay-change.log" 2>&1;then
        echo 'FAIL: cold raw resume produced no persistent overlay changes';exit 1
      fi
      rg -Fxq "$run/disk0.qcow2: FAILED" "$run/overlay-change.log"
      rg -Fxq "$run/disk1.qcow2: FAILED" "$run/overlay-change.log"
    fi
  fi
  if [[ ${AROS_TEST_PROJECT_PERSIST:-none} == write ]];then
    rg -q 'RESULT native project retained PASS synced=1' "$run/serial.log"
  elif [[ ${AROS_TEST_PROJECT_PERSIST:-none} == read ]];then
    rg -q 'RESULT native project cold PASS root=1 child=1 inheritance=1 packet-readback=1 readonly=1' "$run/serial.log"
  fi
  if [[ ${AROS_TEST_ACCOUNTING_PERSIST:-none} == write ]];then
    rg -q 'RESULT native accounting retained PASS synced=1' "$run/serial.log"
  elif [[ ${AROS_TEST_ACCOUNTING_PERSIST:-none} == read ]];then
    rg -q 'RESULT native accounting cold PASS metrics=12 defaults=6 cli-readback=3 readonly-query=1' "$run/serial.log"
    rg -q 'RESULT native accounting CLI PASS single=1 user-rows=30 group=1 project=1 objectquotas=3' "$run/serial.log"
  fi
  if [[ ${AROS_TEST_ZVOL_DEVICE:-0} == 1 ]];then
    if [[ ${AROS_TEST_ZVOL_UNMAP:-0} == 1 ]];then
      rg -q 'RESULT native zvol UNMAP PASS VPD=1 full-block-zero=1 edges-preserved=1 invalid-list-no-write=1 malformed=3' "$run/serial.log"
      rg -q 'RESULT native zvol UNMAP readonly PASS sense-write-protected=1 no-transfer=1' "$run/serial.log"
    elif [[ ${AROS_TEST_ZVOL_UNMAP:-0} == 2 ]];then
      rg -q 'RESULT native zvol UNMAP compatibility PASS old-service=1 unadvertised=1 refused=1' "$run/serial.log"
    elif [[ ${AROS_TEST_ZVOL_UNMAP:-0} == 3 ]];then
      rg -q 'RESULT native zvol UNMAP cold PASS exact-hole=1 exact-neighbors=1 readonly-denied=1 capabilities=1' "$run/serial.log"
    fi
    if [[ ${AROS_TEST_ZVOL_DEVICE_EMPTY:-0} == 1 ]];then
      rg -q 'RESULT native zvol empty startup PASS absent=32 boundary-slot=31 readonly=1 first-writable=1 duplicate-refused=1' "$run/serial.log"
      rg -q 'RESULT native zvol preservice startup PASS absent=32 no-service=1 remap-refused=1' "$run/serial.log"
    fi
    if [[ ${AROS_TEST_ZVOL_DEVICE_MULTI:-0} == 1 ]];then
      rg -q 'RESULT native zvol multiple units PASS units=2 distinct-geometry=1 independent-data=1 readonly-isolated=1 eject-isolated=1 cleanup=1' "$run/serial.log"
    fi
    if [[ ${AROS_TEST_ZVOL_DEVICE_CAPACITY:-0} == 0 && ${AROS_TEST_ZVOL_DEVICE_READONLY:-0} == 0 ]];then
      rg -q 'RESULT native zvol change notification PASS retained=1 remove=1 abort=1 eject-callback=1 drained=1' "$run/serial.log"
    fi
    if [[ ${AROS_TEST_ZVOL_MAPPING_GUI:-0} == 1 ]];then
      rg -q 'RESULT native live zvol GUI continuation PASS mappings-unchanged=1' "$run/serial.log"
    fi
    if [[ ${AROS_TEST_ZVOL_REMAP_GUI:-0} == 1 ]];then
      rg -q 'RESULT native zvol GUI remap PASS readonly=1 exact-read=1 eject=1' "$run/serial.log"
    elif [[ ${AROS_TEST_ZVOL_REMAP_GUI:-0} == 2 ]];then
      rg -q 'RESULT native zvol GUI writable remap PASS write=1 flush=1 exact-read=1 eject=1' "$run/serial.log"
    fi
    if [[ ${AROS_TEST_ZVOL_MOUNT_GUI:-0} == 1 ]];then
      rg -q 'RESULT native zvol GUI mountfile PASS saved=1 exact-cli-match=1 transaction-clean=1' "$run/serial.log"
    fi
    if [[ ${AROS_TEST_ZVOL_EJECT_GUI:-0} == 1 ]];then
      rg -q 'RESULT native zvol GUI eject PASS absent=1 generation=3 io-refused=1 remappable=1' "$run/serial.log"
    fi
    if [[ ${AROS_TEST_ZVOL_RESIZE_API:-0} == 1 ]];then
      if [[ $writable == 2 ]];then
      rg -q 'RESULT native Manager zvol resize cold PASS exact-get=1 readonly-service-denied=1 unchanged=1' "$run/serial.log"
      rg -q 'RESULT native Manager zvol readonly cold PASS set-denied=1 inherit-denied=1 property-unchanged=1' "$run/serial.log"
      else
        rg -q 'RESULT native Manager zvol resize PASS malformed=4 inherit-denied=1 filesystem-denied=1 readonly-denied=1 unauthorized-denied=1 changed=1 exact-get=1' "$run/serial.log"
      fi
    fi
    if [[ ${AROS_TEST_ZVOL_READONLY_POLICY:-0} == 1 ]];then
      rg -q 'RESULT native zvol inherited readonly PASS property=on device-protected=1 write-denied=1 restored=1' "$run/serial.log"
      rg -q 'RESULT native zvol readonly boundaries PASS direct-write-denied=1 writable-open-denied=1 inherited-off-write=1 readback=1' "$run/serial.log"
      rg -q 'RESULT native Manager zvol readonly PASS set=1 get=1 inherit=1 write-policy=1 malformed-denied=1 filesystem-only-denied=1 unauthorized-denied=1' "$run/serial.log"
      if [[ ${AROS_TEST_ZVOL_READONLY_GUI:-0} == 1 ]];then
        rg -q 'RESULT native Manager zvol readonly GUI PASS actual-manager=1 independent-property=1 independent-write-refusal=1' "$run/serial.log"
      fi
    fi
    if [[ ${AROS_TEST_ZVOL_DEVICE_RACE:-0} == 1 ]];then
      rg -q 'RESULT native zvol startup race PASS launches=4 barrier=4 rejected=3 registered=1' "$run/serial.log"
    fi
    if [[ ${AROS_TEST_ZVOL_DEVICE_CAPACITY:-0} == 0 ]];then
      rg -q 'RESULT native zvol SCSI capacity PASS capacity10=1 capacity16=1 short-replies=1 invalid-rejected=1' "$run/serial.log"
      rg -q 'RESULT native zvol inquiry PASS standard36=1 inspector96=1 short-reply=1 unknown-VPD-rejected=1' "$run/serial.log"
      if [[ ${AROS_TEST_ZVOL_DEVICE_READONLY:-0} == 0 ]];then
        rg -q 'RESULT native zvol mapping PASS live=1 readonly=1 absent=1 short-buffer=1 list=1' "$run/serial.log"
        rg -q 'RESULT native zvol remap PASS stale=1 busy=1 failed-open=1 readonly=1 exact-read=1 session-reopen=1 generation=1' "$run/serial.log"
        rg -q 'RESULT native zvol remap CLI PASS default-readonly=1 mapping-query=1 cleanup=1' "$run/serial.log"
        rg -q 'RESULT native zvol guarded eject PASS malformed=1 stale=1 busy=1 preserved=1 released=1 generation=1 absent-refused=1' "$run/serial.log"
        rg -q 'RESULT native zvol eject CLI PASS busy-refused=1 released=1 absent-refused=1' "$run/serial.log"
        if [[ ${AROS_TEST_ZVOL_DEVICE_COLD:-0} != 1 ]];then
          rg -q 'RESULT native zvol writable eject PASS write=1 eject=1 readonly-remap=1 exact-read=1' "$run/serial.log"
          rg -q 'RESULT native zvol live resize PASS growth=1 capacity=1 geometry=1 new-range-io=1 shrink-bounds=1 prefix-preserved=1 regrown-zero=1' "$run/serial.log"
          rg -q 'RESULT native zvol new mapping PASS changed-dataset=1 geometry=1 write-flush-read=1 cleanup=1' "$run/serial.log"
        fi
      fi
    fi
    if [[ ${AROS_TEST_ZVOL_DEVICE_MULTI:-0} == 1 ]];then
      rg -q 'RESULT native zvol large capacity PASS bytes=3298534883328 capacity16=1 capacity10-saturated=1 last-sector-io=1' "$run/serial.log"
    fi
    if [[ ${AROS_TEST_ZVOL_DEVICE_CAPACITY:-0} == 1 ]];then
      rg -q 'RESULT native zvol capacity PASS units=32 geometry=32 distinct-data=32 boundary=1 partial-startup=1 readonly=1 independent-eject=32 cleanup=1' "$run/serial.log"
      rg -q 'RESULT native zvol queued capacity PASS units=32 rounds=8 write-bytes=33554432 read-bytes=33554432 flushes=256 drained=1' "$run/serial.log"
      rg -q 'RESULT native zvol clients PASS processes=4 units=32 rounds=8 flushes=1024 drained=1' "$run/serial.log"
    elif [[ ${AROS_TEST_ZVOL_DEVICE_COLD:-0} == 1 ]];then
      rg -q 'RESULT native cold zvol device PASS readonly=1 eject=1' "$run/serial.log"
    elif [[ ${AROS_TEST_ZVOL_DEVICE_READONLY:-0} == 1 ]];then
      rg -q 'RESULT native zvol device default readonly PASS read=1 write-denied=1 eject=1' "$run/serial.log"
    else
      rg -q 'RESULT native zvol device PASS exec=1 geometry=1 query=1 chunked=1 offset64=1 bounds=1 readonly=1 eject=1 unauthorized=1 abort=1' "$run/serial.log"
    fi
    if [[ ${AROS_TEST_ZVOL_DEVICE_FILESYSTEM:-0} == 1 ]];then
      rg -q 'RESULT native zvol mounted eject refusal PASS handler-only=1 data-preserved=1' "$run/serial.log"
      if [[ ${AROS_TEST_ZVOL_DEVICE_COLD:-0} == 1 ]];then
        rg -q 'RESULT native zvol generated mountfile PASS actual-mount=1 invalid-refused=1 blocks=131072' "$run/serial.log"
        rg -q 'RESULT native cold filesystem on zvol PASS exact-read=1 write-denied=1 shutdown=1' "$run/serial.log"
        if [[ ${AROS_TEST_ZVOL_DEVICE_AFS:-0} != 1 ]];then
          rg -q 'RESULT native cold FAT protection PASS info=1 mutations-denied=6 exact-reread=1' "$run/serial.log"
        fi
      else
        rg -q 'RESULT native zvol generated mountfile PASS actual-mount=1 invalid-refused=1 blocks=131072' "$run/serial.log"
        rg -q 'RESULT native filesystem on zvol PASS format=1 write=1 flush=1 reopen=1 exact-read=1' "$run/serial.log"
      fi
    fi
    if [[ ${AROS_TEST_ZVOL_DEVICE_KEEP:-0} == 1 ]];then
      rg -q 'RESULT native zvol retained PASS dataset=qemu_mirror/device-volume' "$run/serial.log"
    fi
    if [[ ${AROS_TEST_ZVOL_RESIZE_PERSIST:-none} == write ]];then
      rg -q 'RESULT native zvol resize retained PASS bytes=33554432 prefix=512 truncated-offset=25165824' "$run/serial.log"
    elif [[ ${AROS_TEST_ZVOL_RESIZE_PERSIST:-none} == read ]];then
      rg -q 'RESULT native zvol resize cold PASS capacity=33554432 prefix=512 truncated-zero=1 write-denied=1' "$run/serial.log"
    fi
  fi
  if [[ $writable == 2 ]];then
    if [[ $create_cold == 1 ]];then
        rg -q 'RESULT native CLI create cold PASS partition-only=1 normalized-read=1 readonly=1 exported=1 payload-bytes=1048576' "$run/serial.log"
        if rg -q 'OpenZFS native I/O failure' "$run/serial.log";then exit 1;fi
    else
        rg -q 'RESULT native CLI cold PASS import=1 properties=2 normalized-read=1 readonly=1 export=1' "$run/serial.log"
        if [[ ${AROS_TEST_COMMAND_HISTORY:-0} == 1 ]];then
            rg -q 'RESULT native CLI cold history PASS persisted=1 isolated=1' "$run/serial.log"
        fi
    fi
    if [[ $split_cold == 1 ]];then
        rg -q 'RESULT native CLI split cold PASS partition-only=1 normalized-read=1 readonly=1 exported=1 payload-bytes=1048576' "$run/serial.log"
        ! rg -q 'OpenZFS native I/O failure' "$run/serial.log"
    fi
    if [[ ${AROS_TEST_CLI_TINY_REPLY:-0} == 1 ]];then
        [[ $(rg -c 'TEST native import tiny reply EOVERFLOW' "$run/serial.log") == 1 ]]
        rg -q 'RESULT native CLI tiny reply PASS imported=1 readable=1 exported=1' "$run/serial.log"
    fi
  else
    if [[ ${AROS_TEST_CLI_POOL_CREATE:-0} == 1 ]];then
        if [[ ${AROS_TEST_CLI_DRAID:-0} == 1 ]];then
          if [[ ${AROS_TEST_TYPED_DRAID:-0} == 1 ]];then
            if [[ $draid_persist != read ]];then rg -q 'RESULT native typed dRAID create PASS geometry=1 API-create=1' "$run/serial.log";fi
            if [[ $draid_persist == none ]];then
              for parity in 2 3;do
                rg -q "RESULT native typed dRAID parity PASS parity=$parity children=$((parity+2)) degraded-write=1 resilver=1 readonly-reimport=1 payload-bytes=1048576" "$run/serial.log"
              done
              for parity in 1 2 3;do
                rg -q "RESULT native typed dRAID add PASS parity=$parity groups=2 members=$(((parity+2)*2)) capacity-increased=1 scrub=1 readonly-reimport=1 payload-bytes=1048576" "$run/serial.log"
                rg -q "RESULT native typed dRAID allocation PASS parity=$parity new-group-growth=1 readonly-retained=1" "$run/serial.log"
              done
            fi
          fi
          if [[ $draid_expand != 0 ]];then
            if [[ $draid_persist == write ]];then
                rg -q "RESULT native typed dRAID expansion retained PASS parity=$draid_expand members=${#draid_members[@]} groups=2 new-allocation=1 scrub=1 exact-payload=1 exported=1" "$run/serial.log"
            else
                rg -q "RESULT native typed dRAID expansion cold PASS parity=$draid_expand members=${#draid_members[@]} groups=2 allocated=1 readonly=1 exact-payload=1 exported=1" "$run/serial.log"
            fi
          elif [[ $draid_persist == write ]];then
            rg -q 'RESULT native CLI dRAID retained PASS active-spare=1 original-offline=1 payload-bytes=1048576 exported=1' "$run/serial.log"
          elif [[ $draid_persist == read ]];then
            rg -q 'RESULT native CLI dRAID cold PASS active-spare=1 original-offline=1 payload-bytes=1048576 readonly=1 exported=1' "$run/serial.log"
            rg -q 'RESULT native Manager dRAID cold spare PASS inuse=1' "$run/serial.log"
          else
            rg -q 'RESULT native CLI dRAID PASS parity=1 data=1 children=3 spares=1 payload-bytes=1048576 readonly-reimport=1' "$run/serial.log"
            rg -q 'RESULT native CLI dRAID resilver PASS degraded-write=1 completed=1 readonly-reimport-read=1' "$run/serial.log"
            rg -q 'RESULT native CLI dRAID spare PASS inuse=1 resilver-complete=1 detached=1 available=1 unauthorized-denied=1 readonly-denied=1' "$run/serial.log"
            rg -q 'RESULT native CLI dRAID active spare PASS original-offline=1 DOS-write-read=1 active-reimport=1 restored=1' "$run/serial.log"
          fi
        fi
        if [[ ${AROS_TEST_NATIVE_CREATE:-0} == 1 ]];then
            rg -q 'RESULT native CLI partition create PASS native-only=1 normalized-read=1 readonly=1 exported=1 payload-bytes=1048576' "$run/serial.log"
            if rg -q 'OpenZFS native I/O failure' "$run/serial.log";then exit 1;fi
            if [[ ${AROS_TEST_TYPED_NATIVE_CREATE:-0} == 1 ]];then
                rg -q 'RESULT native typed partition create PASS API-create=1 CLI-import=1 DOS-read=1' "$run/serial.log"
                rg -q 'RESULT native typed create policy PASS unconfirmed=denied unauthorized=denied nonblank=denied' "$run/serial.log"
                if [[ ${AROS_TEST_TYPED_NATIVE_MIRROR:-0} == 1 ]];then
                    rg -q 'RESULT native typed mixed mirror PASS file-member-deleted=1 native-survivor-read=1' "$run/serial.log"
                fi
            fi
        fi
        rg -q 'RESULT native CLI split PASS mounted-denied=1 readonly-denied=1 unauthorized=1 dryrun-unchanged=1 destination-exported=1 separate-imports=2 payload-bytes=1048576' "$run/serial.log"
        if [[ ${AROS_TEST_SPLIT_RETAINED:-0} == 1 ]];then
            rg -q 'RESULT native CLI split retention PASS injected-error=1 destination-readable=1 source-export-denied=1 source-destroy-denied=1 destination-exported=1' "$run/serial.log"
        fi
        rg -q 'RESULT native CLI attach replace PASS readonly-denied=1 unauthorized=1 resilver-complete=2 old-members-deleted=1 replacement-only-read=1 payload-bytes=1048576' "$run/serial.log"
        rg -q 'RESULT native CLI attach replace blankness PASS force-denied=2 marker-preserved=2' "$run/serial.log"
        if [[ ${AROS_TEST_NATIVE_REPLACE:-0} == 1 ]];then
            rg -q 'RESULT native CLI partition-attach replace PASS readonly-denied=1 unauthorized=1 resilver-complete=2 old-members-deleted=1 replacement-only-read=1 payload-bytes=1048576' "$run/serial.log"
            ! rg -q 'OpenZFS native I/O failure' "$run/serial.log"
            if [[ ${AROS_TEST_TYPED_NATIVE_REPLACE:-0} == 1 ]];then
                rg -q 'RESULT native typed replacement PASS readonly-denied=1 unauthorized=1 resilver-complete=1 replacement-only-read=1' "$run/serial.log"
            fi
        fi
        if [[ ${AROS_TEST_NATIVE_SPLIT:-0} == 1 ]];then
            rg -q 'RESULT native CLI partition-attach-split PASS mounted-denied=1 readonly-denied=1 unauthorized=1 dryrun-unchanged=1 destination-exported=1 separate-imports=2 payload-bytes=1048576' "$run/serial.log"
            ! rg -q 'OpenZFS native I/O failure' "$run/serial.log"
            if [[ ${AROS_TEST_TYPED_NATIVE_ATTACH:-0} == 1 ]];then
                rg -q 'RESULT native typed attach PASS readonly-denied=1 unauthorized=1 resilver-complete=1 payload-bytes=1048576' "$run/serial.log"
            fi
            if [[ $split_keep == 1 ]];then
                rg -q 'RESULT native CLI split retained PASS pool=cli_split_new member=/AROS/ata.device/3/partition/0 payload-bytes=1048576' "$run/serial.log"
            fi
        fi
        if [[ ${AROS_TEST_NATIVE_ADD:-0} == 1 ]];then
            rg -q 'RESULT native CLI native-partition growth PASS named-policy=1 added-member=1 reopen=1 DOS-write=1 readonly-reimport-read=1' "$run/serial.log"
            if [[ ${AROS_TEST_TYPED_NATIVE_ADD:-0} == 1 ]];then
                rg -q 'RESULT native typed partition add PASS readonly-denied=1 unauthorized=1 native-survivor-read=1' "$run/serial.log"
                if rg -q 'OpenZFS native I/O failure' "$run/serial.log";then exit 1;fi
            fi
        fi
        rg -q 'RESULT native CLI pool create PASS mirror=1 DOS-write=1 export=1 import=1 readonly-read=1' "$run/serial.log"
        rg -q 'RESULT native CLI create alias PASS bound-assign=1 canonical-lifetime=1' "$run/serial.log"
        rg -q 'RESULT native CLI cachefile PASS written=1 parsed-reimport=1 export-removed=1 cleanup=1' "$run/serial.log"
        rg -q 'RESULT native CLI create cachefile PASS explicit=1 written=1 disabled-removed=1' "$run/serial.log"
        rg -q 'RESULT native CLI import cachefile PASS explicit=1 regenerated=1 disabled-removed=1' "$run/serial.log"
        rg -q 'RESULT native CLI invalid cache PASS metadata-refused=1 tree-refused=1 pool-absent=2 cleanup=1' "$run/serial.log"
        rg -q 'RESULT native Manager cachefile PASS invalid=2 unchanged=1 disabled=1 enabled=1 serialized=1' "$run/serial.log"
        rg -q 'RESULT native Manager cachefile readonly PASS refused=1' "$run/serial.log"
        rg -q 'RESULT native CLI temporary create PASS collision=1 live-name=1 persistent-name=1 reimport=1 release=1' "$run/serial.log"
        rg -q 'RESULT native Manager temporary create PASS live-name=1 persistent-name=1 reimport=1 release=1' "$run/serial.log"
        rg -q 'RESULT native Manager temporary create validation PASS malformed=4 valid-after-refusals=1' "$run/serial.log"
        if [[ ${AROS_TEST_TEMPORARY_CREATE_GUI:-0} == 1 ]];then
            rg -q 'RESULT native temporary create GUI PASS actual-manager=1 permanent-reimport=1 child-retained=1' "$run/serial.log"
        fi
        if [[ ${AROS_TEST_POOL_WIZARD_GUI:-0} == 1 ]];then
            rg -q 'RESULT native pool wizard GUI PASS actual-manager=1 mirror=1 DOS-persistence=1 cleanup=1' "$run/serial.log"
        fi
        rg -q 'RESULT native CLI create blankness PASS force-rejected=1 first-member-unchanged=1 marker-retained=1' "$run/serial.log"
        rg -q 'RESULT native CLI pool destroy PASS busy=1 unauthorized=1 removed=1 normal-import-denied=1 bindings-released=1 readonly-denied=1 forced-busy-denied=1' "$run/serial.log"
        rg -q 'RESULT native CLI maintenance PASS scrub-complete=1 reopen=1 readonly-scrub-denied=1 readonly-reopen-read=1 unauthorized=1' "$run/serial.log"
        rg -q 'RESULT native CLI checkpoint rewind PASS preview=1 current-retained=1 writable=1 no-checkpoint-denied=1 reimport=1 DOS-readonly=1' "$run/serial.log"
        rg -q 'RESULT native CLI checkpoint PASS create=1 duplicate-denied=1 reimport=1 readonly-discard-denied=1 discard-complete=1 absent-discard-denied=1 unauthorized=1' "$run/serial.log"
        rg -q 'RESULT native CLI vdev PASS offline=1 last-replica-denied=1 degraded-write=1 online=1 resilver-complete=1 readonly-denied=1 unauthorized=1 repaired-member-read=1' "$run/serial.log"
        rg -q 'RESULT native Manager vdev flags PASS malformed=2 temporary-offline=1 reimport-online=1 explicit-expand-accepted=1 DOS-read=1' "$run/serial.log"
        rg -q 'RESULT native Manager bootfs reimport PASS persisted=1 readonly-refused=1 unchanged=1' "$run/serial.log"
        rg -q 'RESULT native Manager online expansion PASS before=[0-9]* after=[0-9]* reimport-capacity=1 payload-bytes=1048576' "$run/serial.log"
        rg -q 'RESULT native CLI online expansion PASS before=[0-9]* after=[0-9]* reimport-capacity=1 payload-bytes=1048576' "$run/serial.log"
        rg -q 'RESULT native CLI online expansion policy PASS readonly-denied=1 unauthorized-denied=1' "$run/serial.log"
        rg -q 'RESULT native CLI initialize PASS wait-complete=1 status=1 uninitialize=1 payload-bytes=1048576' "$run/serial.log"
        rg -q 'RESULT native CLI initialize policy PASS readonly-denied=1 unauthorized-denied=1' "$run/serial.log"
        rg -q 'RESULT native CLI history PASS internal-dataset-record=1' "$run/serial.log"
        if [[ ${AROS_TEST_VDEV_PROPERTIES:-0} == 1 ]];then
            rg -q 'RESULT native CLI vdev properties PASS guid=1 comment-set-get=1' "$run/serial.log"
            rg -q 'RESULT native CLI vdev properties persistence PASS reimport=1' "$run/serial.log"
            if [[ ${AROS_TEST_MANAGER_VDEV_PROPERTIES:-0} == 1 ]];then
                rg -q 'RESULT native Manager vdev property PASS guid=1 comment=1 structured=1' "$run/serial.log"
                rg -q 'RESULT native Manager vdev property long PASS bytes=600 exact=1' "$run/serial.log"
                rg -q 'RESULT native Manager vdev property all PASS .* cursor-end=1 invalid-cursor=2 errno-detail=2' "$run/serial.log"
                rg -q 'RESULT native Manager vdev setter PASS write-readback=1 invalid=5 unchanged=1' "$run/serial.log"
                rg -q 'RESULT native Manager vdev setter policy PASS readonly=1 unauthorized=1' "$run/serial.log"
                rg -q 'RESULT native Manager vdev types PASS numeric=1 boolean=1 user-property=1 empty=1' "$run/serial.log"
                if [[ ${AROS_TEST_MANAGER_VDEV_GUI:-0} == 1 ]];then
                    rg -q 'RESULT native Manager vdev GUI PASS actual-manager=1 independent-readback=1' "$run/serial.log"
                fi
            fi
            rg -q 'RESULT native CLI vdev properties policy PASS readonly-read=1 readonly-write-denied=1 unauthorized-write-denied=1 path-change-denied=1' "$run/serial.log"
        fi
        if [[ ${AROS_TEST_COMMAND_HISTORY:-0} == 1 ]];then
            rg -q 'RESULT native CLI command history PASS plain-user-command=1' "$run/serial.log"
            if [[ -n ${AROS_TEST_COMMAND_HISTORY_BINARY:-} ]];then
                rg -q 'RESULT native command history isolation PASS threads=2 recycled=72 single-use=1 reconnect=1' "$run/serial.log"
                rg -q 'RESULT native command history pools PASS separate=2 forbidden-absent=1' "$run/serial.log"
                rg -q 'RESULT native command history wire PASS line=0 malformed=7 contexts=2 stale=1 recovered=1' "$run/serial.log"
                rg -q 'RESULT native command history long PASS bytes=6000 exact-record=1' "$run/serial.log"
                if [[ ${AROS_TEST_HISTORY_MEGARECORD:-0} == 1 ]];then
                    rg -q 'RESULT native command history megarecord PASS bytes=1052672 exact-record=1 libzfs-resize=1 batches=[0-9]*' "$run/serial.log"
                    rg -q 'RESULT native Manager history megarecord PASS bytes=1052672 summary-eoverflow=1 raw-exact=1 summary-pages=[0-9]* raw-pages=[0-9]*' "$run/serial.log"
                fi
            fi
        fi
        rg -q 'RESULT native CLI history policy PASS readonly-read=1 unauthorized-denied=1' "$run/serial.log"
        if [[ ${AROS_TEST_HISTORY_RACE:-0} == 1 ]];then
            rg -q 'RESULT native history concurrent overwrite PASS writes=240 pages=[0-9]* bytes=[0-9]* jumps=[1-9][0-9]* overlap-jumps=[1-9][0-9]* discarded-partials=[0-9]* records=[1-9][0-9]* eof=1 line=0' "$run/serial.log"
            rg -q 'RESULT native history wire PASS bytes=[0-9]* pages=[0-9]* overwrite-jumps=1 malformed=6 guards=1 eof=1 recovered=1' "$run/serial.log"
            rg -q 'RESULT native concurrent CLI history PASS records=240 newest=20 oldest-overwritten=1 raw-over-384KiB=1 overwrite-jumps=1' "$run/serial.log"
        elif [[ ${AROS_TEST_LARGE_HISTORY:-0} == 1 ]];then
            rg -q 'RESULT native history wire PASS bytes=[0-9]* pages=[0-9]* overwrite-jumps=1 malformed=6 guards=1 eof=1 recovered=1' "$run/serial.log"
            rg -q 'RESULT native large CLI history PASS records=180 newest=20 oldest-overwritten=1 raw-over-384KiB=1 overwrite-jumps=1' "$run/serial.log"
        else
            rg -q 'RESULT native history wire PASS bytes=[0-9]* pages=[0-9]* overwrite-jumps=0 malformed=6 guards=1 eof=1 recovered=1' "$run/serial.log"
        fi
        rg -q 'RESULT native Manager vdev flag policy PASS readonly-denied=2 unauthorized-denied=2' "$run/serial.log"
        rg -q 'RESULT native CLI detach PASS readonly-denied=1 unauthorized=1 topology=1 last-member-denied=1 removed-file-deleted=1 survivor-import-read=1' "$run/serial.log"
        rg -q 'RESULT native CLI rename PASS busy=1 readonly-denied=1 unauthorized=1 renamed=1 reimport-read=1' "$run/serial.log"
        rg -q 'RESULT native CLI snapshot rename PASS mounted-denied=1 descendant-mounted-denied=1 recursive=1 single=1 reimport-read=1' "$run/serial.log"
        rg -q 'RESULT native mounted snapshot rename PASS busy-lock=1 nounmount-refused=1 CLI-detach=1 Manager-detach=1 remount-read=1' "$run/serial.log"
        rg -q 'RESULT native recursive mounted snapshot rename PASS busy-child=1 parent-retained=1 idle-detach=2 remount=2' "$run/serial.log"
        rg -q 'RESULT native Manager recursive snapshot rename PASS busy-child=1 parent-retained=1 idle-detach=2 remount=2 filesystem-refused=1' "$run/serial.log"
        rg -q 'RESULT native manager snapshot rename PASS mounted-denied=1 rename=1 CLI-enumeration=1 reimport-read=1 readonly-denied=1' "$run/serial.log"
    fi
    rg -q 'RESULT native service CLI PASS list=1 create=1 property=1 snapshot=1 destroy=1' "$run/serial.log"
    rg -q 'RESULT native deferred snapshot destroy PASS held=1 marked=1 retained=1 release-destroyed=1 invalid=3 cleanup=1' "$run/serial.log"
    rg -q 'RESULT native recursive snapshot create PASS snapshots=3 atomic-collision=1 retained=1 absent=2 cleanup=1' "$run/serial.log"
    rg -q 'RESULT native recursive snapshot holds PASS snapshots=3 hold-atomic=1 queries=27 pages=2 cursor-end=2 invalid-cursor=4 errno-detail=4 release-preflight=1 release-all=1 cleanup=1' "$run/serial.log"
    rg -q 'RESULT native bookmark pagination PASS bookmarks=27 pages=2 unique=27 cursor-end=1 invalid-cursor=2 errno-detail=2 cleanup=29' "$run/serial.log"
    rg -q 'RESULT native status cursor boundaries PASS views=4 paginated=2 cursor-end=4 invalid-cursor=8 errno-detail=8' "$run/serial.log"
    rg -q 'RESULT native history cursor boundaries PASS views=2 raw-bytes=[0-9]* raw-rows=[0-9]* raw-pages=[0-9]* cursor-end=2 invalid-cursor=4 errno-detail=4' "$run/serial.log"
    if [[ ${AROS_TEST_CLI_POOL_CREATE:-0} == 1 ]];then
        rg -q 'RESULT native legacy pool history PASS version=1 summary-unsupported=1 raw-unsupported=1 empty-errors=2 errno-detail=2 cleanup=1' "$run/serial.log"
    fi
    rg -q 'RESULT native recursive snapshot destroy PASS snapshots=3 atomic-busy=1 retained=3 keep=3 invalid=1 cleanup=1' "$run/serial.log"
    rg -q 'RESULT native recursive dataset destroy PASS datasets=3 mounted-preflight=1 snapshot-preflight=1 retained=6 destroyed=3 keep=1 invalid=1 progress-detail=1 cleanup=1' "$run/serial.log"
    rg -q 'RESULT native recursive property query PASS datasets=27 pages=2 unique=27 inherited=26 long-placeholders=27 cursor-end=1 invalid-cursor=2 errno-detail=2 cleanup=27' "$run/serial.log"
    rg -q 'RESULT native recursive property inherit PASS datasets=27 pages=2 inherited=27 preflight=1 cleanup=27' "$run/serial.log"
    rg -q 'RESULT native property all PASS .* cursor-end=2 invalid-cursor=4 errno-detail=4 cleanup=2' "$run/serial.log"
    rg -q 'RESULT native recursive property all PASS datasets=3 .* cursor-end=1 invalid-cursor=3 errno-detail=3 cleanup=3' "$run/serial.log"
    if [[ ${AROS_TEST_CLI_POOL_CREATE:-0} == 1 ]];then
        rg -q 'RESULT native Manager removal PASS submitted=1 status-finished=1 copied-positive=1 topology-removed=1 survivor=1' "$run/serial.log"
    fi
    rg -q 'RESULT native CLI integration PASS GUI-property=1 DOS-file=1 normalized-lookup=1' "$run/serial.log"
    rg -q 'RESULT native CLI export PASS busy=1 force-rejected=1 export=1 API-reimport=1' "$run/serial.log"
    rg -q 'RESULT native CLI export permission PASS nonroot-denied=1 pool-retained=1' "$run/serial.log"
    rg -q 'RESULT native CLI identity PASS root=0 reader=77 discovery-denied=1' "$run/serial.log"
    rg -q 'RESULT native CLI import PASS writable=1 readonly=1 persisted=1 repeat=1' "$run/serial.log"
    if [[ ${AROS_TEST_NATIVE_LABELCLEAR:-0} == 1 ]];then
        rg -q 'RESULT native raw labelclear PASS active-refused=1 exported-force=1 labels=8 pads-preserved=8 regions-zero=8 import-absent=1' "$run/serial.log"
    fi
    if [[ $pressure == 1 ]];then
        rg -q 'RESULT native DOS cache pressure PASS files=320 passes=3 writable=1 readonly-reimport=1' "$run/serial.log"
    fi
    rg -q 'RESULT native CLI import admission PASS nonroot=1 implicit-cache-rejected=1 recovery=1' "$run/serial.log"
    if [[ ${AROS_TEST_CLI_DISCOVERY:-1} == 1 ]];then
        rg -q 'RESULT native CLI discovery PASS mirror=1 repeat=1 excluded-path=1 not-imported=1' "$run/serial.log"
    fi
    if [[ $keep_cli == 1 ]];then
        rg -q 'RESULT native CLI retained PASS dataset=qemu_mirror/cli-import' "$run/serial.log"
    fi
  fi
fi
sha256sum -c "$run/backing.sha256"
if [[ ${AROS_TEST_NATIVE_REPATH:-0} == 1 ]];then
    rg -q 'RESULT native CLI repath PASS online-denied=1 blank-denied=1 wrong-member-denied=1 moved=1 offline-preserved=1 reimport=3 DOS-read=2 restored=1' "$run/serial.log"
    rg -q 'RESULT native CLI repath guards PASS unauthorized=1 readonly=1 post-move-write=1 fresh-return-copy=1' "$run/serial.log"
    qemu-img convert -O raw "$run/repath.qcow2" "$run/repath-result.raw"
    cmp -n $((2048*512)) "$run/repath.raw" "$run/repath-result.raw"
    cmp -i $((2048*512+256*1024*1024)) "$run/repath.raw" "$run/repath-result.raw"
    sgdisk --verify "$run/repath-result.raw" >"$run/repath-result-verify.log" 2>&1
    rg -q 'No problems found' "$run/repath-result-verify.log"
fi
if [[ ${AROS_TEST_NATIVE_ADD:-0} == 1 || ${AROS_TEST_NATIVE_REPLACE:-0} == 1 || ${AROS_TEST_NATIVE_SPLIT:-0} == 1 || ${AROS_TEST_NATIVE_CREATE:-0} == 1 ]];then
    # The test may write only the new GPT data extent, never either GPT copy
    # or the gap preceding it. Preserve a raw result for independent inspection.
    qemu-img convert -O raw "$run/add-partition.qcow2" "$run/add-partition-result.raw"
    cmp -n $((2048*512)) "$run/add-partition.raw" "$run/add-partition-result.raw"
    cmp -i $((256*1024*1024-33*512)) "$run/add-partition.raw" "$run/add-partition-result.raw"
    sgdisk --verify "$run/add-partition-result.raw" >"$run/add-partition-result-verify.log" 2>&1
    rg -q 'No problems found' "$run/add-partition-result-verify.log"
    if [[ ${AROS_TEST_NATIVE_CREATE:-0} == 1 ]];then
        dd if="$run/add-partition-result.raw" of="$run/create-zdb-member.raw" \
            bs=512 skip=2048 count=522207 conv=excl status=none
        LD_LIBRARY_PATH="$repo/build-control/.libs:$repo/build-control/deps/usr/lib/x86_64-linux-gnu" \
            build-control/zdb -e -p "$run/create-zdb-member.raw" -bcc cli_native_created >"$run/create-zdb.log" 2>&1
        rg -q 'No leaks' "$run/create-zdb.log"
    fi
fi
if [[ -s "$run/overlays.sha256" ]];then sha256sum -c "$run/overlays.sha256";fi
if [[ $split_cold == 1 ]];then
    # Fixed 256 MiB GPT fixture above: sector 2048 through 524254. zdb is
    # read-only and independent of the AROS runtime. Its exit status matters:
    # "No leaks" alone can coexist with a space-map refcount failure.
    dd if="$verify_run/add-partition-result.raw" of="$run/split-zdb-member.raw" \
        bs=512 skip=2048 count=522207 conv=excl status=none
    LD_LIBRARY_PATH="$repo/build-control/.libs:$repo/build-control/deps/usr/lib/x86_64-linux-gnu" \
        build-control/zdb -e -p "$run/split-zdb-member.raw" -bcc cli_split_new >"$run/split-zdb.log" 2>&1
    rg -q 'No leaks' "$run/split-zdb.log"
fi
if [[ $writable == 1 ]];then
    if [[ $gui == 2 ]];then rg -q '^RESULT native GUI create PASS normalized-case-read=1 destroy=1' "$run/serial.log";fi
    if [[ $gui == 3 ]];then
        rg -q '^RESULT native GUI snapshot rename PASS old-absent=1 readonly-reimport-read=1' "$run/serial.log"
        rg -q '^RESULT native GUI mounted snapshot rename PASS mounted-before=1 detached-after=1' "$run/serial.log"
    fi
    if [[ $keep_names == 1 ]];then rg -q '^RESULT native reopened namespace PASS properties=3 normalized-case-read=1 readonly=1' "$run/serial.log";fi
    rg -q 'RESULT native writable import PASS' "$run/serial.log"
    echo "PASS: native writable $layout import/DOS write/export/reimport: $run"
fi
if [[ $writable == 2 ]];then
    if [[ $keep_names == 1 ]];then rg -q '^RESULT native cold namespace PASS properties=3 normalized-case-read=1 readonly=1' "$run/serial.log";fi
    rg -q 'RESULT native cold write verification PASS' "$run/serial.log"
    if [[ ${AROS_TEST_ENCRYPTION_PERSIST:-none} == resume ]];then
        echo "PASS: native cold raw resume and prior DOS write verification: $run"
    elif [[ ${AROS_TEST_PERMISSION_PERSIST:-none} == authorize ]];then
        echo "PASS: native cold permission authorization and prior DOS write verification: $run"
    else
        echo "PASS: native cold readonly verification of prior DOS write: $run"
    fi
fi
if [[ $gpt == 2 ]];then rg -q 'RESULT native GPT readonly API PASS' "$run/serial.log";fi
if [[ $gui_gpt == 1 ]];then
    # An actual GUI Apply must have replaced the original mirror GPT layout.
    qemu-img convert -O raw "$run/disk0.qcow2" "$run/gui-labeled.raw"
    sgdisk --verify --print "$run/gui-labeled.raw" >"$run/gui-labeled-verify.log" 2>&1
    rg -q 'No problems found' "$run/gui-labeled-verify.log"
    rg -q 'AROS OpenZFS' "$run/gui-labeled-verify.log"
    sgdisk --info=1 "$run/gui-labeled.raw" >"$run/gui-data-partition.log" 2>&1
    rg -q 'First sector: 2048 ' "$run/gui-data-partition.log"
    echo "PASS: interactive GPT Apply independently verified; only overlay0 writable: $run"
fi
if [[ $gpt == 1 || $gpt == 3 ]];then
    rg -q 'RESULT native GPT API PASS' "$run/serial.log"
    qemu-img convert -O raw "$run/disk0.qcow2" "$run/labeled.raw"
    sgdisk --verify --print "$run/labeled.raw" >"$run/labeled-verify.log" 2>&1
    rg -q 'No problems found' "$run/labeled-verify.log"
    if [[ $gpt == 3 ]];then
        rg -q 'RESULT upstream GPT CLI PASS' "$run/serial.log"
        rg -q 'zfs-' "$run/labeled-verify.log"
    else
        rg -q 'AROS OpenZFS API' "$run/labeled-verify.log"
    fi
fi
if [[ $draid_persist != none ]];then
    zdb_paths=()
    for member in "${draid_members[@]}";do
        mcopy -i "$run/draid-backing.raw" "::cli-draid-$member.vdev" "$run/draid-$member.raw"
        zdb_paths+=(-p "$run/draid-$member.raw")
        if [[ $draid_persist == read ]];then cmp "$draid_source/draid-$member.raw" "$run/draid-$member.raw";fi
    done
    build-control/zdb -e "${zdb_paths[@]}" -bcc cli_draid >"$run/draid-zdb.log" 2>&1
    rg -q 'No leaks' "$run/draid-zdb.log"
    sha256sum "$run/draid-backing.raw" "$run"/draid-?.raw >"$run/draid-retained.sha256"
    if [[ $draid_persist == read ]];then sha256sum -c "$run/draid-source.sha256";fi
    if [[ $draid_expand != 0 ]];then
        echo "PASS: persistent dRAID $draid_persist expanded parity=$draid_expand with independent zdb: $run"
    else
        echo "PASS: persistent dRAID $draid_persist with active spare and independent zdb: $run"
    fi
fi
if [[ $gpt == 2 ]];then
    echo "PASS: native GPT readonly-service refusal only: $run"
else
if [[ ${AROS_TEST_ZVOL_DEVICE_COLD:-0} == 1 ]];then
    cp aros/device/verify-cold-filesystem.sh "$run/verify-cold-filesystem.sh"
    bash aros/device/verify-cold-filesystem.sh "$run"
fi
if [[ -n ${AROS_TEST_DEVICE_INVENTORY:-} ]];then
        rg -q 'RESULT HIDD inventory PASS .*version=1 ata-mask=3' "$run/serial.log"
        rg -q 'RESULT native inventory worker PASS delayed=1 busy-free=1 success=1 denied=1 malformed=10 absent=1 unchanged=1' "$run/serial.log"
fi
if [[ $usb_swap == 1 ]];then
    echo "PASS: native swapped-member readmission refused without disk changes: $run"
elif [[ $usb_suspend_guard == 1 ]];then
    echo "PASS: native suspended export safety only; pool recovery not established: $run"
else
    echo "PASS: native readonly $layout import and DOS read; explicit GPT test=$gpt: $run"
fi
fi
