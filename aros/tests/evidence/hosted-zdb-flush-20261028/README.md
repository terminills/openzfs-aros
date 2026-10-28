# Private hosted debugger and library output flush

No DUT, user hosted process, existing X server, physical device or QEMU was
used. Each attempt copied a retired hosted runtime into a new private
directory, dereferencing links, and launched its own Xvfb/AROS process group.
The runner stopped only those processes after completion. It omitted
security.library and loaded the explicitly selected shared engine.

## Controls and fix

- z0DXzSy2: old candidate mmake-debugger.5XPhlb4E completed its client tests,
  but exact label comparison failed: a final unterminated report line joined
  the next marker. This is retained as a failure, not a qualified pass.
- idjjGAGp: harness now terminates an otherwise unterminated label capture
  line. Labels, lifecycle and absent-security denial pass, but empty L2ARC
  output still lacks its final blank line. Exact comparison rejects it.
- Source diagnosis: print_label_numbers ends in putchar; the empty L2ARC
  path also finishes with a single newline. POSIX fputc uses buffered DOS
  FPutC. The library's nixmain wrapper returned without an executable CRT
  teardown or explicit stdout/stderr flush.
- The production library now attempts both flushes while its runtime and
  exit-status slot remain alive, before restoring caller state. A flush
  error changes a successful result to failure, never the reverse.

## Passing candidate

Generated MetaMake build mmake-debugger.KdVEE7zT includes the flush change
and prior L2ARC count bound. Private hosted runs CRq6USFb and dY50smwE use
that build. The retained comparison logs establish each result; this does
not turn earlier failures into passing runs.

Coverage: two exact label reads, two library lifecycle cycles with invalid
argument/one-shot/reload checks and caller exit restoration, actual missing
security.library denial for /AROS/ata.device/0, three empty/missing-header
cases, populated little/big-endian logs, bad checksum plus healthy retry,
oversized counts in both byte orders plus healthy retry. Fixtures and runtime
input hashes remain unchanged after the runs. No device is opened by the
unauthorized native-path test; it is authorization proof, not ATA I/O proof.

Hosted final markers use storage=private-hosted. The label verifier requires
an explicit --hosted argument, and normal ISO package gates do not accept
these markers. L2ARC verification also refuses a hosted/native-ATA mode mix.
Public package, QEMU SMP/native-storage and full service regressions must be
rerun with the new library before replacing the previously qualified bundle.
Live cache population/rebuild and physical durability remain separate gaps.
