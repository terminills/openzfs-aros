# Native zdb: GPT gaps, platform positions, and read-only pool traversal

Eight-vCPU isolated QEMU, 2048 MiB RAM, no network or host share. No DUT or
user hosted session. The debugger is the unchanged matching MetaMake launcher
and version-2 library from `mmake-debugger.JLOOcjrH`; their hashes are recorded
in each run's inputs. The original two pool members are embedded in new
256-MiB disk fixtures, with 64-MiB extents beginning at sectors 4097 and 139265.
`sgdisk` creates and verifies real primary/backup GPT headers and tables.
On-disk slots are 0 and 2, deliberately leaving slot 1 unused.

## Failure investigation and controlled comparison

- `iCwNz9XS`: initial GPT run, stock partition library, ENOENT at native pool
  admission. This test incorrectly assumed GPT PT_POSITION preserved slots.
- `zavj2FuZ`: private partition-library gap fix applied, but the same incorrect
  position assumption still produces ENOENT. Retained, not a passing result.
- `LRQP52DN`: corrected harness uses GPT PT_POSITION values 0 and 1, with 2
  absent. The private gap fix is loaded from a replaced boot fs package.
  Full block walk, MOS walk, partial-admission rejection, and duplicate-path
  retry pass. Seven block counters, three dataset descriptions, 21 feature
  checks, and 72 object rows match the independent host debugger.
- `BZHb8jdm`: same corrected harness, stock fs package, ENOENT at admission.
  This control demonstrates the gap fix is necessary in this platform build.
- `ixRonr68`: fresh MBR regression with the corrected harness, without the
  private overlay. Positions remain 0 and 2; missing position 1 is rejected.
  Full pool, label, damaged-label and shared-library lifetime tests pass.

AROS GPT does not implement a table-specific PT_POSITION attribute: the
generic implementation returns the index in the populated children list.
MBR supplies its own original slot position. OpenZFS already uses the native
API's positions; no production offset guessing or partition parsing was added.
The existing platform patch advances the GPT entry pointer even when an unused
slot takes `continue`. Only private copies were patched and relinked; all
shared platform source, objects, library and selected inputs remain unchanged.

## Reproduction

The overlay builder now accepts AROS_CC and requires an external artifact root.
This run used AROS_PARTITION_BUILD `aros-adt-pc-x86_64-clean`,
AROS_PARTITION_SOURCE `aros-adt-upstream-merge/rom/partition`, and the same
source tree's `tools/package/pkg` via AROS_PACKAGE_TOOL. Complete paths and
hashes are retained in `overlay/`. Its fs archive changes only partition.library.

From the qualification checkout, using the existing explicit dependency recipe:

```sh
AROS_TEST_ZDB_BUILD="$PWD/build-audit/mmake-debugger.JLOOcjrH" \
AROS_TEST_ZDB_PARTITIONS=1 AROS_TEST_ZDB_NATIVE_POOL=1 \
AROS_TEST_ZDB_PARTITION_TABLE=gpt \
AROS_TEST_FS_PACKAGE="$PWD/build-audit/partition-gpt.YGZVq2U0/aros-fs.pkg.xz" \
bash build-audit/run-zdb-device.sh
```

Omit AROS_TEST_FS_PACKAGE for the stock negative control. Omit both that and
AROS_TEST_ZDB_PARTITION_TABLE for the MBR regression. These are private matched
SDK/runtime inputs, not a public SDK or final release qualification.

The package helper supports both legacy version-only and current ADT
version/size header returns. It requires an exact declared archive size;
tests against the real package tool verify unchanged unrelated member bytes,
order, replacement, and rejection of short/long sizes and truncation.

Input hashes and byte-for-byte qcow2/base comparisons are retained, including
failed GPT runs. Only inactive generated ISOs are pruned after hash recording;
serial logs, source recipes, fixtures, overlays and results remain available.
Native L2ARC, complete public packaging and physical durability are not proven
by these tests. The full port remains incomplete.
