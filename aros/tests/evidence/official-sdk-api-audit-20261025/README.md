# Official nightly SDK API audit

This is an API prerequisite audit, not full OpenZFS or public-ISO qualification.

Baseline: published 20261023 SMP ISO, SHA-256
`38b5689f4d5c23b0765a3fbc266c67928d841d81c754675a107dd8bc0b50d6e3`.
SDK: its extracted, unmodified `iso-Developer`, not the private integration SDK.
Published nightly source commit remains unknown. Current upstream source
`827810f5f1` was inspected separately, not asserted identical to that ISO.

Artifacts: `/mnt/short-term/openzfs-official-sdk-audit.xi9RkF3r`.
Official and custom serial probe subdirectories contain source snapshots,
compiler version, command log, link map, SDK path and input hashes.
Use `aros/cli/build-errno-test.sh` with explicit SDK, new output directory and
`AROS_CC` to reproduce those builds. Custom runtime execution is a separate
coordinator-owned matrix and is not qualified by this report.

## Verified

* Standalone serial errno test builds with pristine SDK, no private stubs.
* Official caller on official runtime: BEGIN, three cases with accessor/writer
  and restoration success, final PASS, shell exit-success marker.
* Standard pipe/fcntl test: 16 cycles of FD_CLOEXEC set/read, exact byte transfer,
  writer close, reader EOF and reader close, followed by shell exit-success.
* Both positive and deliberately mismatched errno-writer host tests pass.
* ExecHelper builds/links against the pristine SDK.
* Current pipe2 test fails linking against it with undefined `pipe2`.

Native run: private diskless QEMU, KVM, CPU host, 2 CPUs, 2048 MiB, no network,
BIOS CD boot, no physical disks or user-hosted/DUT access. Only test programs,
Startup-Sequence and GRUB configuration overlaid; OS modules unmodified.

## Limits and failed attempts retained

The first minimal startup omitted mounting PIPE and SER. Errno cases passed,
but pipe returned ENOENT; this was a harness failure, not a pipe defect. A fresh
boot after explicit mounts produced the passing `serial-mounted.log`.

Hybrid boot metadata replay initially failed with overlapping GPT partitions.
BIOS-only repack booted. Repacking that derivative emitted an oversized inherited
protective-MBR warning and xorriso exit 32, despite finishing image writing.
The subsequent guest completed both tests, but this image is NOT a distributable
ISO or evidence of hybrid-boot correctness. All pack logs are retained.

`pipe` followed by `fcntl` does not establish atomic close-on-exec descriptor
creation against concurrent exec. No pipe2 replacement was implemented.

## Environment contract versus implementation

Requirement: an explicit child environment must not inherit unrequested values
or mutate shared ENV:/ENVARC:. It is not a requirement to use a particular tag.
Current port implementation uses NP_EnvLocalOnly/PRF_ENVLOCALONLY and fails
closed if unsupported. At inspected upstream source 827810f5f1, NP_CopyVars
controls copying local variables, but GetVar can still fall back to ENV: and
ENVARC:, and posixc getenv calls GetVar without GVF_LOCAL_ONLY. Thus NP_CopyVars
FALSE alone is not proof of exact child environment isolation. Native testing
of alternatives remains open; no global assign manipulation is authorized.
