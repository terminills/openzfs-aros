# Early SDK link prerequisite checks

Real cross-compiler probes on the retained pristine 20261023 public SDK fail
independently with unresolved `pwrite` and `pipe2`. The selected private SDK
passes `pwrite`, but SDK-only `pipe2` still fails: qualified CLI builds use the
explicit matching pipe overlay. No new fallback or import-vector stub was added.

Retained probe directories:

- `0fyt8btf`: public SDK, both failed links.
- `njq9wi9t`: private SDK, pwrite passes and SDK-only pipe2 fails.
- `xzih1125`: actual `build-source.sh` exits 1 on the public SDK before creating
  the full engine output tree.
- `x0kppnz5`: private SDK engine profile passes.
- `50_szurr`: actual SDK-only `probe-link.sh` exits 1 before compiling its CLI
  inputs. Unused probe/dependency arguments deliberately verify early ordering.

The helper creates a unique small evidence directory for every attempt and
retains full per-symbol commands and diagnostics. The C declarations only
describe the symbols; they do not implement missing functionality. No target
binary is executed. These are compiler/link checks, not tests of the matching
runtime's position locking, atomic descriptor inheritance, error semantics,
ABI compatibility, or filesystem operations. Prior native evidence remains
distinct. All larger build artifacts and prior failures are preserved.
