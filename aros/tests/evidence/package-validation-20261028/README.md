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
