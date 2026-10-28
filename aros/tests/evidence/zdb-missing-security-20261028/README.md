# Missing security.library: prepared, not runtime-qualified

The isolated debugger runner now distinguishes an absent security.library
from the existing unconfigured-library case. Missing mode removes only
`/Libs/security.library` from the disposable output ISO, then inventories that
ISO to reject any remaining file with that name. The source ISO is unchanged.
The actual debugger caller must observe both OpenLibrary(name, 0) failure and
no FindResident(name) before emitting the missing-library marker. A successful
native run must then return EACCES for the attached disposable ATA device.
No production authorization logic is weakened or replaced by a test mock.

All three client variants (configured, unconfigured, missing) cross-compile
with -Wall -Wextra -Werror in `zdb-missing-security-build.WZtSGXlZ`. The final
version-zero absence probe also compiles in the prepared runner below.
The host verifier exercises five modes and rejects incomplete, duplicate,
faulted and wrong-denial transcripts, plus misidentified missing-library data.
These are cross-build and parser checks, not AROS runtime evidence.

Prepared run: `build-audit/zdb-label-run.2c7bWaNK`, using the unchanged matching
MetaMake zdb/library from `mmake-debugger.JLOOcjrH`. Preparation completed with
an empty security-library ISO inventory and unchanged input hashes. No QEMU
was launched: the slot remains handed to MAME. `prepared-only.txt` records
that boundary explicitly. Logs and hashes accompany this note; the generated
ISO remains outside Git for the pending native run.

Reproduce preparation from the qualification checkout:

```sh
AROS_TEST_ZDB_PREPARE_ONLY=1 AROS_TEST_ZDB_MISSING_SECURITY=1 \
AROS_TEST_ZDB_BUILD="$PWD/build-audit/mmake-debugger.JLOOcjrH" \
bash build-audit/run-zdb-device.sh
```

Omit PREPARE_ONLY only after acquiring the isolated QEMU slot. Do not run
against the DUT or user hosted session. Native denial remains unverified.
