# Current native package input validation

`validation.log` records a successful `--validate-only` run against the exact
native GUI replay, persistent cold suite, normal encrypted-root discovery suite,
and current GPT debugger suite. The shell recipe creates only symlinks to
existing selected artifacts. No bundle was created or installed.

The first attempt identified symlinks instead of their target ELF files; the
validator now uses `file -Lb`, consistent with the existing byte hashes. The
rerun passed every pre-copy gate, including all four CRT libraries and their
source inventories, DOS/FAT/AFS snapshots, normal shared engine symbols and
matching companions. All 21 staging unit tests pass, including refusal of
invalid acceptance in validation-only mode before any bundle allocation.

The native evidence predates this staging-only change. This does not claim a
new runtime feature, successful copy/relocation, SDK source closure, or full
port completion. Source/copy verification of the eventual bundle remains due.

## Subsequent actual staging

The recipe now accepts `--stage` and an explicit `OZ_PACKAGE_OUTPUT_PARENT`.
Actual staging passed into
`/home/terminills/Documents/GitHub/openzfs-package-staging/mmake-bundle.xt1czzqv`.
The source/artifact filesystem was below its reserve despite old-artifact
cleanup; the destination had over 400 GiB free and passed the unchanged 100-GiB
reserve check. Only this 69-MiB package was allocated there, not VM images.

All 4,995 entries passed an independent `sha256sum -c SHA256SUMS`; no symlinks
remain. FAT, DOS, POSIX, stdc, stdcio, DOS64 and AFS source snapshot validators
were rerun against the copied bundle and all passed. `staged/` retains the
qualification record, complete manifest and checksum-check output. Source
compilation was not repeated in this step; source replay evidence remains
the earlier recorded builds. Package bytes match the accepted runtime inputs.
