#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
run=$(realpath -- "${1:?private admission evidence directory required}")
[[ $(dirname -- "$run") == "$repo/build-aros" && $(basename -- "$run") == admission-service-run.* ]]
[[ $(<"$run/suite.txt") == device ]]
command -v fuser >/dev/null
if [[ -S "$run/write.monitor" || -S "$run/verify.monitor" ]] ||
    fuser "$run/disk0.raw" "$run/disk1.raw" >"$run/recovery-open-users.log" 2>&1;then
    echo 'Refusing recovery verification while guest/storage users remain' >&2;exit 1
fi
for phase in write verify;do
    rg -q '^RESULT standalone admission PASS' "$run/$phase-checked.log"
done
sha256sum "$run/disk0.raw" "$run/disk1.raw" >"$run/recovery-before.sha256"
export LD_LIBRARY_PATH="$repo/build-control/.libs:$repo/build-control/deps/usr/lib/x86_64-linux-gnu"
# Explicitly discover only the member taken offline and subsequently repaired.
# The fixture labels use native /AROS/ata.device paths, absent on this host.
zdb=(build-control/zdb -e -p "${AROS_VERIFY_MEMBER0:-$run/disk0.raw}")
"${zdb[@]}" -bcc qemu_mirror >"$run/recovered-member-zdb.log" 2>&1
rg -q 'No leaks' "$run/recovered-member-zdb.log"
# zdb resolves dataset-relative names; a leading slash attempts a '/' entry.
"${zdb[@]}" -r qemu_mirror/degraded-payload payload.bin "$run/recovered-payload.bin" >"$run/recovered-payload.log" 2>&1
perl -0777 -e '
    use strict; use warnings;
    my $actual=<>;
    die "Wrong recovered payload length\n" unless length($actual)==64*4096;
    for my $block (0..63) {
        for my $i (0..4095) {
            my $expected=(($block*37+$i*13)^($i>>5))&255;
            die "Recovered payload mismatch\n" if ord(substr($actual,$block*4096+$i,1))!=$expected;
        }
    }
    print "PASS: all 262144 recovered payload bytes match independently\n";
' "$run/recovered-payload.bin" >"$run/recovered-payload-check.log"
sha256sum -c "$run/recovery-before.sha256"
echo "PASS: independent recovered member checksum/leak and exact payload verification: $run"
