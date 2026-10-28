# Retained stdc source build and hosted qualification

The stdio snapshot builder now also supports `--library stdc`. It rebuilds
all 518 objects selected by the matching AROS stdc link map, including x86-64
math/setjmp assembly. It does not reuse the map's cached object files. The
generated assembly header is an explicit `--asm-header` input; ABI headers,
locale table, source contents and licenses are retained. Library archives,
SDK headers and the compiler remain external, hash-bound dependencies.

## Build and replay

- Candidate: `build-audit/stdc-source.px9y2jvo`.
- Relocated replay: `build-audit/fat-replay.tzt0l7rd`.
- Both stdc.library hashes:
  `41152036cec6d3da3eac703e2931ea622b60a31201e6d16204b48c5217697cee`.
- All 1348 retained input files passed the packaging snapshot verifier.
- The unchanged stdcio default path was rebuilt as
  `build-audit/stdcio-source.uo0iplm_`; it reproduces the previously qualified
  `a1265379f3ae9a1004c3c75bdc7a266c91fb09162acc215086e14b0b3aeb7d48` binary.
- The 18 staging tests include missing assembly header, changed locale table,
  nonlocal ABI include, mismatched recipe, and substituted binary rejection.

Use the retained replay recipe with the matching toolchain to reproduce the
binary. It copies retained source/header files to a new build directory and
does not need the original source checkout or cached objects. Recorded
absolute `__FILE__` strings are preserved with a compiler prefix map.

## Isolated hosted tests

`yLI5U7Ir` / inputs `5mCZIXjX` passed full writer and cold-reader storage tests
with the new stdc candidate, stdcio gphvn1pg, POSIX j099bkwp and DOS 4CUMjP0r.
`Yzmkgkbu` / inputs `ujlYVS3B` repeated these with the rebuilt stdcio uo0iplm_
and actual Manager interaction: native response save, authenticated snapshot
creation, Manager close and fresh-process snapshot discovery. Runtime checks
passed on both boots, including errno, three redirected-stdio exit paths,
ExecHelper and DOS execute protection. Independent host zdb and unchanged
cold member hashes passed. Logs and named-window screenshots are retained.

These tests are not physical-device qualification or proof of every libc/math
function. No DUT or user hosted session was used. Native QEMU exact-binary
qualification is still pending the other owner's VM release.

## Failed build attempts retained

- `stdc-source.vnv9latn`: missing generated `aros/x86_64/asm.h`.
- `stdc-source.m8rezxt_`: missing retained math include directory for BSD math
  sources. The final builder supplies the math and ld80 include directories.

Both failures are build-recipe issues; no platform source was patched to hide
them. Their build logs remain in this evidence set.

## Packaging

Staging now requires `--stdc-source-snapshot` and retains it as `Sources/StdC`.
The selected stdc binary must still match GUI, cold, encrypted-root and native
debugger evidence. The hosted results here do not bypass those gates. No new
release bundle has been staged from these results alone.
