#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
run=$(mktemp -d "$repo/build-aros/size-parser-compare.XXXXXXXX")
upstream=71a9f9578616a90c3c14bb59629fb4d31bfd68d1
# Extract complete source functions as immutable test inputs. Fail if the
# pinned source layout changes; never silently generate an empty reference.
git show "$upstream:lib/libzfs/libzfs_util.c" |
    perl -0777 -ne '/(static int\nstr2shift\(.*?\n\}\n\n\/\*.*?\nint\nzfs_nicestrtonum\(.*?\n\})/s or die "reference functions not found";print "$1\n"' \
    > "$run/size-parser-reference.inc"
perl -0777 -ne '/(int\nzfs_nicestrtonum\(.*?\n\})/s or die "wrapper not found";print "$1\n"' \
    lib/libzfs/libzfs_util.c > "$run/size-parser-wrapper.inc"
make -C build-control -j2 libzutil.la > "$run/build.log" 2>&1
cc -O2 -Wall -Wextra -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 \
    -include build-control/zfs_config.h -Ibuild-control/include -Iinclude \
    -Ilib/libspl/include -Ilib/libspl/include/os/linux -I"$run" \
    aros/engine/size-parser-compare.c build-control/lib/libzutil/libzutil_la-zutil_nicenum.o \
    -lm -o "$run/compare" >> "$run/build.log" 2>&1
sha256sum "$run/size-parser-reference.inc" "$run/size-parser-wrapper.inc" \
    lib/libzutil/zutil_nicenum.c aros/engine/size-parser-compare.c > "$run/inputs.sha256"
"$run/compare" | tee "$run/result.log"
echo "Evidence: $run"
