# Unqualified boot-environment transport candidate

This branch carries the earlier `6d0c81d` candidate separately from the
official-SDK qualification branch. See the bounded private-runtime result below.

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

## Private runtime replay

Fresh eight-vCPU QEMU run `public-service-run.3lhU92Ni` passed the bootenv
GET/SET round trip, missing-version rejection without mutation, and complete
restoration read-back comparison. The surrounding acceptance suite passed
three service lifecycles, authenticated mirror creation, DOS file write/read,
export and read-only reimport. Pools were RAM-backed, no DUT or user hosted
session was used. This is not cold-label persistence qualification.

The client and nvpair dependency were rebuilt from this candidate source using
the recorded private SDK/pthread pair. The service was the retained static
`bootenv-production.U3TSr8Ta` candidate, not the failed official-SDK build.
Its source-head and source patch are retained with the exact binary hash.
The runtime was alpha18c5 with the recorded private DOS and posixc overlays;
the base ISO and retained overlay hashes matched their earlier manifest.
The DOS package verifier confirmed all other boot-module bytes/order unchanged.

The runner returned 2 after all guest assertions because its cleanup helper
incorrectly rejected an external artifact directory colocated with an external
checkout. This was a host cleanup failure, not a guest pass/fail ambiguity:
the serial log includes the exact bootenv marker and final storage PASS.
The correction is on the qualification branch as `2476a8c`. Dry-run eligibility
and subsequent checked pruning retain the ISO hash, logs and source provenance.
An earlier attempt selected the wrong host package tool and never booted; its
failure log is retained too. Neither failure has been erased or called a pass.

Still required before promotion: bootenv cold persistence, explicit denial
and read-only checks, and the complete repaired official runtime/SDK lane.
