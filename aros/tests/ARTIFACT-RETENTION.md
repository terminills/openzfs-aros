# Qualification artifact retention

Retain a reproducible record, not an indefinitely preserved virtual machine.
Passing and failing runs both need their evidence; neither requires retaining
every generated ISO, extracted staging directory or compiler object forever.

## Keep

- Exact OpenZFS and AROS revisions, uncommitted patches and selected input hashes.
- Compiler/toolchain and dependency versions, build configuration and commands.
- Build/run logs, failure diagnostics, screenshots and relevant UI actions.
- Executable/package archives requested for distribution or reproduction, with
  checksums and an explicit statement of what was actually qualified.
- Unique source changes and user data. These are never disposable artifacts.

## Temporary

Generated test ISOs, VM disks, extracted staging copies and build trees are
working files. After validation and packaging, preserve the record above and
remove the obsolete working files. A failure does not justify retaining a
large image indefinitely: preserve the failure and reproduction inputs, and
retain a disk only while a specific follow-up still needs its exact state.

Before deleting, resolve the exact path and ownership, inspect open-file
holders and running jobs, and check whether another active test uses it as a
backing image or dependency. Never remove another task's active artifacts,
shared SDKs or a source tree as part of a broad space-reclamation sweep.
Record the removed paths and measured allocated space. State that removed
disposable files must be regenerated; they are not recoverable from a log.

## Capacity

Check free space before a large build or VM run and monitor growth. Use the
designated external artifact filesystem, not the main server filesystem.
External storage is finite: use bounded concurrency and clean completed work
instead of repeatedly moving an ever-growing collection between disks.

Do not start another bulk job while an urgent cleanup is outstanding. Report
free space separately for the main disk and artifact disk; reclaiming external
space does not fix a full main disk. Avoid attributing another task's cleanup
to this project merely because free space increased during the same interval.
