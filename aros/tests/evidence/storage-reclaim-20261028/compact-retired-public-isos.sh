#!/usr/bin/env bash
set -euo pipefail
base=/mnt/short-term/openzfs-bootenv-candidate-20261025/build-audit
test "$(findmnt -no UUID -T "$base")" = 18df445b-e065-44d2-b4ec-d98c69af396a
for name in public-service-run.fbsFjNOj/test.iso public-service-run.P2AdY43b/test.iso public-service-run.y39LczDw/test.iso;do
    path="$base/$name"
    test -f "$path" && test ! -L "$path" && test ! -e "$path.zst"
    if fuser "$path" 2>/dev/null;then exit 1;else test "$?" -eq 1;fi
    identity=$(stat -c '%d:%i:%s:%Y:%Z' "$path")
    expected=$(sha256sum "$path");expected=${expected%% *}
    zstd -T2 -3 --keep "$path"
    actual=$(zstd -dc "$path.zst" | sha256sum);actual=${actual%% *}
    test "$expected" = "$actual"
    test "$identity" = "$(stat -c '%d:%i:%s:%Y:%Z' "$path")"
    if fuser "$path" 2>/dev/null;then exit 1;else test "$?" -eq 1;fi
    sync "$path.zst"
    printf '%s  %s\n' "$expected" "$path" >>"$base/retired-public-isos-original.sha256"
    sha256sum "$path.zst" >>"$base/retired-public-isos-archives.sha256"
    unlink "$path"
    printf 'Verified compressed archive retained: %s.zst\n' "$path"
done
