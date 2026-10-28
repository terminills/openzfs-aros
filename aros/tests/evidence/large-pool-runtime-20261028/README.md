# Large-pool runtime requalification

This tests the source-built DOS64, POSIX, stdc and stdcio library combination
with retained historical pool/reader executables. It does not establish current
engine source qualification, physical storage durability or native QEMU acceptance.

`hosted-large-pools.ReA9h0gT` completed successfully in a new private hosted AROS
runtime and X server. Five sparse members each have 14293651161088 bytes.
The RAIDZ1 fixture completed write/export/import/read comparison, independent
host zdb reported no leaks, and a separate cold hosted boot read the pool through
the DOS handler. The handler reported 56939688495360 available bytes at 32768-byte
blocks. Structural sparse digests (size, hole layout, allocated bytes) matched
before and after the cold read. They are not full logical-file SHA256 digests.
No physical NAS, user session or DUT was accessed.

The initial attempt `hosted-large-pools.D41I3aCY` used an older fixture binary
that did not support ARRAY50. Its three 8-GiB members passed their own roundtrip,
but the harness correctly rejected their topology/size before independent zdb or
cold-reader execution. It is not a 50-TB pass. Failure logs remain in `rejected`.

The harness now selects an explicit runtime, independent host zdb and library
path; supports selected CRT copies; hashes all selected runtime libraries; uses
the external artifact-root storage guard; and preserves the original repository
root when executing its immutable private runner snapshot. Unexpected member
count/size now produces a diagnostic instead of a silent shell exit.

`selected-inputs.txt` and `inputs.sha256` bind the successful executable paths
and hashes. The retained fixture executables are not presented as rebuilt from
the current checkout. Pool files and whole runtime copies are not committed.
