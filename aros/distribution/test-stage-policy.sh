#!/usr/bin/env bash
# Negative packaging fixtures only; never edit selected inputs or installations.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
service=$(realpath -e "${1:?qualified native service build required}")
cli=$(realpath -e "${2:?whole CLI build required}")
handler=${3:-}
manager=${4:-$service/OpenZFSManagerNative}
debugger_run=${5:-}
rg -Fxq 'service_test_modes=0' "$service/service-build-options.txt"
rg -Fxq 'full_dispatcher=1' "$service/service-build-options.txt"
source "$repo/aros/engine/storage-preflight.sh"
artifact_root=$(oz_artifact_root "$repo/build-aros" "${AROS_ARTIFACT_ROOT:-}")
run=$(mktemp -d "$artifact_root/stage-policy.XXXXXXXX")
echo "Evidence: $run"
exec > >(tee "$run/test.log") 2>&1
cp "$0" aros/distribution/stage.sh "$run/"
git rev-parse HEAD >"$run/source-head.txt"
git diff --binary >"$run/source.patch"
sha256sum "$service/OpenZFSServiceNative" "$service/service-build-options.txt" \
    "$service/service-build.sha256" >"$run/inputs.sha256"
scenarios=(diagnostic incomplete status-trace missing-provenance stale-hash missing-hash empty-hash missing-binary)
if rg -Fxq 'shared_engine=1' "$service/service-build-options.txt";then
    scenarios+=(shared-missing-hash shared-empty-hash shared-missing-library shared-stale-hash)
    sha256sum "$service/openzfs.library" "$service/engine-library.sha256" >>"$run/inputs.sha256"
fi
for scenario in "${scenarios[@]}";do
    candidate="$run/$scenario"
    mkdir "$candidate"
    cp "$service/service-build.sha256" "$candidate/"
    case $scenario in
        status-trace) printf 'service_test_modes=0\nfull_dispatcher=1\nstatus_trace=1\n' >"$candidate/service-build-options.txt"
            expected='Refusing status-trace diagnostic build.' ;;
        diagnostic) printf 'service_test_modes=1\nfull_dispatcher=1\n' >"$candidate/service-build-options.txt"
            expected='Refusing service without diagnostic-modes-off provenance.' ;;
        incomplete) printf 'service_test_modes=0\nfull_dispatcher=0\n' >"$candidate/service-build-options.txt"
            expected='Refusing service without full dispatcher provenance.' ;;
        missing-provenance) expected='Refusing service without diagnostic-modes-off provenance.' ;;
        stale-hash) cp "$service/service-build-options.txt" "$candidate/"
            # The original hash record remains valid for its original file.
            # Staging must compare the selected artifact, not just check that
            # old absolute pathname with sha256sum -c.
            printf 'deliberately different selected binary\n' >"$candidate/OpenZFSServiceNative"
            expected='Refusing service binary/build hash mismatch.' ;;
        missing-hash|empty-hash) cp "$service/service-build-options.txt" "$candidate/"
            if [[ $scenario == missing-hash ]];then
                mv "$candidate/service-build.sha256" "$candidate/saved-build.sha256"
            else
                : >"$candidate/service-build.sha256"
            fi
            expected='Refusing service without readable build hash.' ;;
        missing-binary) cp "$service/service-build-options.txt" "$candidate/"
            expected='Refusing missing service binary.' ;;
        shared-*)
            cp "$service/service-build-options.txt" "$service/OpenZFSServiceNative" "$candidate/"
            case $scenario in
                shared-missing-hash) expected='Refusing shared engine without readable library hash.' ;;
                shared-empty-hash) : >"$candidate/engine-library.sha256"
                    expected='Refusing shared engine without readable library hash.' ;;
                shared-missing-library) cp "$service/engine-library.sha256" "$candidate/"
                    expected='Refusing missing shared engine library.' ;;
                shared-stale-hash) cp "$service/engine-library.sha256" "$candidate/"
                    printf 'different selected library\n' >"$candidate/openzfs.library"
                    expected='Refusing shared engine library/build hash mismatch.' ;;
            esac ;;
    esac
    status=0
    bash aros/distribution/stage.sh "$candidate" "$cli" >"$candidate/result.log" 2>&1 || status=$?
    [[ $status == 2 ]]
    rg -Fxq "$expected" "$candidate/result.log"
    ! rg -q '^Bundle:' "$candidate/result.log"
    echo "PASS $scenario: $expected"
done
if [[ -n $handler ]];then
    handler=$(realpath -e "$handler")
    sha256sum "$handler/handler.sha256" "$handler/OpenZFS-Handler" >>"$run/inputs.sha256"
    for scenario in missing-hash empty-hash missing-binary stale-hash;do
        candidate="$run/handler-$scenario";mkdir "$candidate"
        case $scenario in
            missing-hash) expected='Refusing handler without readable build hash.' ;;
            empty-hash) : >"$candidate/handler.sha256"
                expected='Refusing handler without readable build hash.' ;;
            missing-binary) cp "$handler/handler.sha256" "$candidate/"
                expected='Refusing missing handler binary.' ;;
            stale-hash) cp "$handler/handler.sha256" "$candidate/"
                printf 'different selected handler\n' >"$candidate/OpenZFS-Handler"
                expected='Refusing handler binary/build hash mismatch.' ;;
        esac
        status=0
        bash aros/distribution/stage.sh "$service" "$cli" "$manager" '' "$candidate" >"$candidate/result.log" 2>&1 || status=$?
        [[ $status == 2 ]]
        rg -Fxq "$expected" "$candidate/result.log"
        ! rg -q '^Bundle:' "$candidate/result.log"
        echo "PASS handler-$scenario: $expected"
    done
fi
sha256sum "$manager" "$manager.inputs.sha256" >>"$run/inputs.sha256"
for scenario in missing-manifest empty-manifest stale-hash;do
    candidate="$run/manager-$scenario";mkdir "$candidate"
    cp "$manager" "$candidate/OpenZFSManagerNative"
    expected='Refusing Manager without readable build manifest.'
    case $scenario in
        empty-manifest) : >"$candidate/OpenZFSManagerNative.inputs.sha256" ;;
        stale-hash)
            cp "$manager.inputs.sha256" "$candidate/OpenZFSManagerNative.inputs.sha256"
            printf 'different selected Manager\n' >"$candidate/OpenZFSManagerNative"
            expected='Refusing Manager binary/build hash mismatch.' ;;
    esac
    status=0
    bash aros/distribution/stage.sh "$service" "$cli" "$candidate/OpenZFSManagerNative" >"$candidate/result.log" 2>&1 || status=$?
    [[ $status == 2 ]]
    rg -Fxq "$expected" "$candidate/result.log"
    ! rg -q '^Bundle:' "$candidate/result.log"
    echo "PASS manager-$scenario: $expected"
done
if [[ -n $debugger_run ]];then
    debugger_run=$(realpath -e "$debugger_run")
    sha256sum "$service/zdb" "$debugger_run/native-debugger.sha256" >>"$run/inputs.sha256"
    for scenario in missing-run missing-binary stale-launcher incomplete-evidence;do
        candidate="$run/debugger-$scenario";mkdir "$candidate"
        cp "$service/OpenZFSServiceNative" "$service/openzfs.library" \
            "$service/service-build.sha256" "$service/engine-library.sha256" \
            "$service/service-build-options.txt" "$candidate/"
        selected_run="$debugger_run"
        expected='Refusing unqualified native debugger.'
        case $scenario in
            missing-run) cp "$service/zdb" "$candidate/"
                selected_run=''
                expected='Refusing bundle without native debugger and runtime evidence.' ;;
            missing-binary) expected='Refusing bundle without native debugger and runtime evidence.' ;;
            stale-launcher) cp "$service/OpenZFSServiceNative" "$candidate/zdb" ;;
            incomplete-evidence) cp "$service/zdb" "$candidate/"
                cp -a "$debugger_run" "$candidate/evidence"
                mv "$candidate/evidence/native-debugger.sha256" "$candidate/saved-native-debugger.sha256"
                selected_run="$candidate/evidence" ;;
        esac
        status=0
        bash aros/distribution/stage.sh "$candidate" "$cli" "$manager" '' '' "$selected_run" \
            >"$candidate/result.log" 2>&1 || status=$?
        [[ $status == 2 ]]
        rg -Fxq "$expected" "$candidate/result.log"
        ! rg -q '^Bundle:' "$candidate/result.log"
        echo "PASS debugger-$scenario: $expected"
    done
fi
sha256sum -c "$run/inputs.sha256"
echo 'PASS: staging policy diagnostic=1 status-trace=1 incomplete=1 missing-provenance=1 selected-hash=1 missing-hash=1 empty-hash=1 missing-binary=1 inputs-unchanged=1'
if rg -Fxq 'shared_engine=1' "$service/service-build-options.txt";then echo 'PASS: shared engine staging policy missing-hash=1 empty-hash=1 missing-library=1 selected-hash=1';fi
