#!/usr/bin/env bash
# Preserve old repository-owned temporary ISO copies. Never removes originals.
# A later explicit cleanup must recheck hashes and live users before unlinking.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
for tool in rg fuser sha256sum tar zstd;do command -v "$tool" >/dev/null;done
out=$(mktemp -d "$repo/build-aros/iso-retention.XXXXXXXX")
echo "Evidence: $out"
cp "$0" "$out/"
git rev-parse HEAD > "$out/source-head.txt"
git diff --binary > "$out/source.patch"
# Only directories referenced by this project's retained run manifests.
while IFS= read -r manifest;do
    IFS= read -r dir < "$manifest"
    [[ $dir =~ ^/tmp/openzfs-(admission|gui|cold)-iso\.[[:alnum:]]{8}$ ]] || continue
    [[ -d $dir && ! -L $dir && -O $dir ]] || continue
    printf '%s\n' "$dir" >> "$out/directories.unsorted"
    printf '%s\t%s\n' "$manifest" "$dir" >> "$out/run-references.tsv"
done < <(rg -l --no-ignore --glob iso-location.txt '^/tmp/openzfs-' build-aros)
LC_ALL=C sort -u "$out/directories.unsorted" > "$out/directories.txt"
while IFS= read -r dir;do
    while IFS= read -r -d '' file;do
        [[ ! -L $file && -O $file ]] || exit 1
        [[ $(basename -- "$file") =~ ^(write|verify|replication|resume|create|add|cancel|split|read|gui)\.iso$ ]] || exit 1
        if fuser "$file" >/dev/null 2>&1;then
            printf 'SKIP live %s\n' "$file" >> "$out/skipped.txt";continue
        fi
        printf '%s\0' "${file#/tmp/}" >> "$out/files.relative0"
        sha256sum "$file" >> "$out/originals.sha256"
    done < <(find "$dir" -mindepth 1 -maxdepth 1 -type f -name '*.iso' -print0)
done < "$out/directories.txt"
test -s "$out/files.relative0"
# A 1-GiB window retains repeated ISO package bytes across adjacent images.
# Unlike deleting rebuildable ISOs, this keeps the exact original evidence.
tar -C /tmp --null -T "$out/files.relative0" -cf - |
    zstd -3 -T2 --long=30 -o "$out/isos.tar.zst" > "$out/compress.log" 2>&1
zstd -d --long=30 -c "$out/isos.tar.zst" |
    tar -C /tmp --compare -f - > "$out/archive-compare.log" 2>&1
sha256sum "$out/isos.tar.zst" > "$out/archive.sha256"
sha256sum -c "$out/originals.sha256" > "$out/originals-verified.log"
printf 'PASS archive and byte comparison; originals retained\n' > "$out/RESULT.txt"
echo "PASS: preserved ISO scratch. Evidence: $out"
