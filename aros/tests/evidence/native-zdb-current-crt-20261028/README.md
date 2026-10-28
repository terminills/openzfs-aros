# Native debugger with selected source-built runtime libraries

`zdb-label-run.BygYEOql` completed in isolated QEMU with 8 vCPUs, 2 GiB RAM,
no network, a read-only ISO and disposable ATA overlays. The selected POSIX,
stdc, stdcio, DOS64 and openzfs libraries were extracted from the prepared ISO
and compared byte-for-byte with their selected inputs before boot.

Passed checks: exact host/native labels twice, shared-library lifecycle and
caller-exit tests, damaged-label recovery, root/unprivileged/root native device
access with EACCES denial, L2ARC header endian/missing cases, populated log
endian/corruption/recovery, malformed entry-count bounds/recovery, and unchanged
ATA/L2ARC device overlays. The retained mode and comparison logs define the
scope. This is not the complete GUI/cold/encrypted public-package acceptance
matrix, physical-device durability, or completion of the whole port.

Before launch, inspection found that the shared-library option reset the shell
array containing CRT mappings. The runner now appends that mapping and verifies
actual ISO library bytes; hashing inputs alone would not catch this defect.

`zdb-label-run.ZtNr9leW` FAILED the missing-security fixture before reaching the
debugger: removing Libs/security.library left its boot-resident module in the
BSP package. No EACCES pass is claimed for this attempt. The runtime helper now
reports which missing-security precondition failed instead of silently returning20.

`zdb-label-run.LuO2nC45` is PREPARED ONLY. It uses a validated package transform
to remove security.library while retaining the other31 module records exactly;
the prepared ISO's BSP bytes match this transformed package. The disk-library
inventory is also empty. Its native missing-security boot remains pending the
coordinated QEMU handback from the SysInfo lane. Five parser tests cover exact
retention, missing/duplicate names, truncation and invalid headers/names. Device,
L2ARC and label-verifier tests also passed (1/11/1 tests).

Original evidence directories are under the candidate's build-audit directory.
Only logs, hashes, mode declarations and the reproducible run recipe are retained
here; ISOs and disposable device images are not committed.
