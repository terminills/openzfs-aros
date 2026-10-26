# Unqualified boot-environment transport candidate

This branch carries the earlier `6d0c81d` candidate separately from the
official-SDK qualification branch. No new native bootenv runtime pass is claimed.

Audit correction: restoring the saved nvlist previously checked only the
SET return code, yet printed `restored=1`. The test now GETs the restored
record and compares its complete packed nvlist with the saved record. A
restore failure is logged explicitly, including when an earlier assertion
already failed. The test still attempts restoration on every exit after
a successful mutation.

The client test translation unit compiles with the unmodified 2026-10-23
official SDK, using the prior official CLI audit flags and treating
incompatible pointers as errors. A temporary change to `char *` was rejected
by this checkout's const-correct nvpair header and reverted; the failed
compile log is retained. There was no production header change.

This does not qualify the service transport, permissions, persistent label
write/read, or cold import. Those require a working engine/CLI runtime pair.
The transport translation unit also compiled with the official SDK, SDK
pthread headers, full dispatcher and ZPL context enabled; its compiler log
is retained. The old private service's recorded task/port/DOS headers and
autoinit archive still match, but those four hashes alone do not establish
a complete compatible runtime/dependency pair for replay.
Current official full-engine and CLI builds still lack pwrite/pipe2 platform
prerequisites. Do not promote this candidate as a tested feature.

Working artifacts: `/mnt/short-term/openzfs-bootenv-candidate-20261025/build-audit`.
