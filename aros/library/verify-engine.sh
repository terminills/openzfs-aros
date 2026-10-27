#!/usr/bin/env bash
# Reject stale provenance and diagnostic engine objects before packaging.
set -euo pipefail
engine=${1:?engine object required}
normal=${2:?normal service build required}
nm_tool=${3:-nm}
[[ -n $nm_tool ]] || nm_tool=nm
grep -Fxq 'service_test_modes=0' "$normal/service-build-options.txt"
grep -Fxq 'full_dispatcher=1' "$normal/service-build-options.txt"
read -r expected _ <"$normal/engine-object.sha256"
actual=$(sha256sum "$engine")
[[ $expected =~ ^[0-9a-f]{64}$ && ${actual%% *} == "$expected" ]]
# Older retained engines predate compiler-input manifests. When present, the
# manifest must describe this selected engine, not another still-valid build.
if [[ -e $normal/compiler-inputs.json ]];then
    helper=$(cd -- "$(dirname -- "$0")/../engine" && pwd)/record-build-inputs.py
    "${OPENZFS_PYTHON:-python3}" - "$helper" "$normal/compiler-inputs.json" "${actual%% *}" <<'PY'
import importlib.util
import json
import pathlib
import sys
spec = importlib.util.spec_from_file_location('engine_inputs', sys.argv[1])
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
manifest = json.loads(pathlib.Path(sys.argv[2]).read_text())
module.verify(manifest)
if manifest['linked_object']['sha256'] != sys.argv[3]:
    raise ValueError('Compiler manifest does not describe selected engine')
print('PASS selected engine compiler-input manifest')
PY
fi
symbols=$("$nm_tool" "$engine")
[[ $symbols == *' T main'* && $symbols == *' T zfsdev_ioctl_common'* ]]
if [[ $symbols == *'__wrap_'* || $symbols == *'oz_test_admission_hold'* ||
      $symbols == *'oz_test_strict_flush_enabled'* ]];then
    echo 'Refusing a diagnostic or fault-injected engine object' >&2
    exit 2
fi
