# Synthetic populated L2ARC reader coverage: host-qualified, native pending

The fixture generator's optional `--populated` mode writes a single 64-KiB
uncompressed upstream-format log block containing two known entries. Both
little-endian and byte-swapped fixtures use the upstream reader's native
Fletcher-4 checksum over on-disk bytes. A third fixture flips one entry byte
after computing the checksum. It does not modify an existing pool or device.

Independent host zdb reads the healthy fixtures with `-lll`, decodes entries
with birth TXGs 42 and 43, reports one valid log and 65536 bytes, and returns
zero. The damaged fixture reports Invalid cksum, zero valid logs and one
invalid log, and returns one because the header accounts for the missing log.
Logs are retained here. Fixtures reside in ignored
`build-audit/l2arc-log-host.OyvBrfjw/fixtures`.

The native runner accepts `AROS_TEST_ZDB_L2ARC_LOGS=1` together with its
existing `AROS_TEST_ZDB_L2ARC_LIBNVPAIR` input. It compares each native report
exactly to host output, requires the expected exit code, then repeats the
healthy log after the bad checksum to detect retained error state. Existing
header and native-device tests remain separate and unchanged in scope.

The extended AROS client cross-builds with -Wall -Wextra -Werror in
`build-audit/l2arc-log-client.vRd0Uvg9`. Two fixture-layout/checksum tests and
seven transcript-verifier tests pass. The existing native empty-header run
EsMKxiba still passes the updated verifier. No new QEMU run was performed:
the coordinated slot is still reserved for MAME after Bluetooth's handoff.

This does not establish native populated-log support yet, and synthetic
reader tests do not establish live cache population, compressed or chained
logs, rebuild correctness, or device durability. No extra ISO was generated.
