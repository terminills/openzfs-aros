#!/usr/bin/env bash
# Remove only exact verified duplicate ISO files from an already verified
# retention archive. All bytes remain recoverable from that archive.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
archive=$(realpath -- "${1:?verified repo ISO retention directory required}")
[[ $(dirname -- "$archive") == "$repo/build-aros" ]]
[[ $(basename -- "$archive") =~ ^iso-retention\.[[:alnum:]]{8}$ ]]
[[ ! -L $archive && -O $archive ]]
if [[ ${OZ_PRUNE_FROZEN:-0} != 1 ]];then
    frozen=$(mktemp "$archive/prune-script.XXXXXXXX")
    cp -- "$0" "$frozen"
    sha256sum "$frozen" >> "$archive/prune-scripts.sha256"
    OZ_PRUNE_FROZEN=1 exec bash "$frozen" "$archive"
fi
grep -qx 'PASS archive and byte comparison; originals retained' "$archive/RESULT.txt"
sha256sum -c "$archive/archive.sha256"
# A checksum read can hit the page cache. Make the recoverable archive and
# its inventory durable before discarding the tmpfs originals.
# Call fsync explicitly: the host may provide a non-GNU sync implementation.
python3 -c 'import os,sys
for path in sys.argv[1:]:
    fd=os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
    try: os.fsync(fd)
    finally: os.close(fd)
' "$archive/isos.tar.zst" "$archive/archive.sha256" \
    "$archive/originals.sha256" "$archive/files.relative0" \
    "$archive/run-references.tsv" "$archive/RESULT.txt" "$archive"
while read -r expected file;do
    [[ $expected =~ ^[0-9a-f]{64}$ ]]
    [[ $file =~ ^/tmp/openzfs-(admission|gui|cold)-iso\.[[:alnum:]]{8}/(write|verify|replication|resume|create|add|cancel|split|read|gui)\.iso$ ]]
    [[ -f $file && ! -L $file && -O $file ]] || { printf 'SKIP absent/changed %s\n' "$file" >> "$archive/prune.log";continue; }
    [[ ! -L $(dirname -- "$file") && -O $(dirname -- "$file") ]]
    if fuser "$file" >/dev/null 2>&1;then
        printf 'SKIP live %s\n' "$file" >> "$archive/prune.log";continue
    fi
    read -r actual rest < <(sha256sum "$file")
    [[ $actual == "$expected" ]] || { printf 'SKIP hash-changed %s\n' "$file" >> "$archive/prune.log";continue; }
    # Recheck live use after hashing; no recursive deletion or inferred path.
    if fuser "$file" >/dev/null 2>&1;then
        printf 'SKIP became-live %s\n' "$file" >> "$archive/prune.log";continue
    fi
    rm -- "$file"
    printf 'REMOVED %s %s\n' "$expected" "$file" >> "$archive/prune.log"
    # rmdir cannot remove a directory containing any other evidence.
    rmdir -- "$(dirname -- "$file")" 2>/dev/null || true
done < "$archive/originals.sha256"
echo "Verified duplicate cleanup finished; exact files remain in $archive/isos.tar.zst"
