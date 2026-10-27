#!/usr/bin/env bash
set -euo pipefail
cd /mnt/short-term/openzfs-bootenv-candidate-20261025
source build-audit/dependencies.YF6P1Tca/dependencies.env
build="$PWD/build-audit/mmake-debugger.JLOOcjrH"
engine="$PWD/build-audit/mmake-engine.hZ1s42So/aros/engine/full"
package=$(mktemp -d "$PWD/build-audit/debugger-service.XXXXXXXX")
echo "Service package: $package"
cp "$0" "$package/recipe.sh"
cp "$build/stage/C/OpenZFSServiceNative" "$build/stage/C/zdb" "$build/stage/Libs/openzfs.library" "$package/"
cp "$engine/engine-object.sha256" "$engine/engine-metadata.h" "$engine/engine-metadata.sha256" "$package/"
rg -Fx 'service_test_modes=0' "$engine/service-build-options.txt" > "$package/service-build-options.txt"
rg -Fx 'full_dispatcher=1' "$engine/service-build-options.txt" >> "$package/service-build-options.txt"
rg -Fx 'status_trace=0' "$engine/service-build-options.txt" >> "$package/service-build-options.txt"
printf 'shared_engine=1\n' >> "$package/service-build-options.txt"
"$AROS_CC" --sysroot="$AROS_SDK" -B"$AROS_SDK/lib/" -O2 -Wall -Wextra -Werror \
    -I"$build/gen/openzfs/aros/library/openzfs/include" aros/library/lifetime-test.c \
    -o "$package/OpenZFSLibraryTest" > "$package/lifetime-build.log" 2>&1
sha256sum "$package/openzfs.library" "$package/OpenZFSServiceNative" "$package/zdb" \
    "$package/OpenZFSLibraryTest" > "$package/binaries.sha256"
sha256sum "$package/openzfs.library" > "$package/engine-library.sha256"
sha256sum "$package/OpenZFSServiceNative" > "$package/service-build.sha256"
export AROS_TEST_SHARED_BUILD="$package"
export AROS_TEST_SERVICE_BINARY="$package/OpenZFSServiceNative"
export AROS_TEST_CLIENT_BINARY="$PWD/build-audit/public-service-run.uL30YO9M/OpenZFSPublicServiceTest"
export AROS_TEST_CLI_DIR="$PWD/build-audit/public-service-run.uL30YO9M"
export AROS_TEST_HANDLER_BINARY="$PWD/build-audit/public-service-run.uL30YO9M/OpenZFS-Handler"
export AROS_TEST_PERSISTENT=1 AROS_TEST_COLD_RELEASE=1
export AROS_TEST_FAT_HANDLER="$PWD/build-audit/public-service-run.uL30YO9M/fat-handler"
export AROS_TEST_HOST_ZDB=/home/terminills/Documents/GitHub/openzfs-aros/build-control/zdb
bash build-audit/run-accounting-replay.sh
