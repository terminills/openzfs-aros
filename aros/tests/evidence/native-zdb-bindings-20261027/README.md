# Explicit read-only native bindings for upstream zdb

The real debugger now admits native paths explicitly supplied through `-p`.
The existing label reader enforces configured security/root identity and opens
the exact device or partition read-only. The existing binding registry then
retains the handle for libzutil discovery and libzpool I/O. At most 32 distinct
native paths are admitted; duplicate arguments reuse their first binding.
No label-supplied native path is opened unless it was explicitly admitted.

Bindings remain alive through `kernel_fini`, including `zdb_exit` cleanup,
then are unbound and their owner/security references closed. Partial admission
failure unwinds every acquired binding and releases search argument storage.
This is process-local capability admission, not an external-writer lock or
arbitrary-code security boundary. Native ZIO workers use the admitted readonly
handles, not a newly synthesized root identity or ambient direct-device open.

## Builds and runs

- `mmake-debugger.JLOOcjrH`: actual generated MetaMake build, including the
  small launcher and version-2 shared engine. The generic host zdb translation
  unit also passes GCC syntax checking with the retained host configuration;
  that is not a host executable/runtime test.
- `zdb-label-run.XBfPxdxZ`: initial command without authentication is denied
  with EACCES before native pool inspection. Retained as a failed test setup,
  not fixed by relaxing production authorization.
- `zdb-label-run.Z0bL6FSP`: test-only helper authenticates and verifies the
  actual debugger caller against the disposable root account. Both upstream
  `-P -e -p .../partition/0 -p .../partition/2 -bcc topo_source` and `-dddd`
  exit 0 and match host accounting. Actual UID 77 caller is denied admission.
- `zdb-label-run.La1hCkXw`: fresh guest repeats both walks, then admits the
  first extent and rejects nonexistent partition 1 with ENOENT/exit 1. A third
  successful block walk includes a duplicated `-p .../partition/0` argument.
  Its accounting also matches host. Four actual root-caller identity markers
  are required for this extended test, plus actual UID 77 denial.

Each guest uses eight virtual CPUs. A new 256-MiB MBR fixture embeds original
member0 and member2 in separate 64-MiB extents at sectors 4097 and 139265,
native positions 0 and 2. It retains the original pool's normal and special
allocation classes. A private qcow2 overlay is the only writable guest disk;
original members and every original input retain their hashes. Overlay/base
comparison reports identical whole-disk contents after each successful guest.
No DUT, user hosted session, network or host filesystem share is used.

Both final runs match seven block accounting counters, three dataset/root
descriptions, 21 feature-reference checks and 72 exact object rows, report
no leaked blocks, and retain complete native report bytes. As in prior
qualification, derived percentage rounding is not compared. The original
file-label, damaged-label and shared-library lifetime tests also pass.

No generic raw-device discovery, writable diagnostics, GPT-specific fixture,
L2ARC inspection, broad topology qualification or physical durability is
claimed here. Public SDK/runtime and complete release qualification remain
unfinished. The root/user fixture helpers are test-only, never installed
with the production launcher.
