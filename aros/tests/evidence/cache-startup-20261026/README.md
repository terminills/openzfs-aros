# AROS startup cache admission regression

The service deliberately starts libzpool with an empty `spa_config_path`,
then selects its trusted cache policy after startup. Upstream libzpool's
fallback to `/boot/zfs/zpool.cache` defeated that intent: a cache could add
unadmitted entries to the SPA namespace before explicit import.

AROS now returns without opening a file for a null/empty path and does not
fall back from a configured path to the unrelated boot cache. Non-AROS
behavior is unchanged. This does not disable explicit, admission-checked
cache imports or normal pool creation/import.

## Verification

`test-cache-startup.py` compiles the actual loader function with observable
file/namespace boundaries. AROS empty/null paths perform no opens; an
explicit missing path does not fall back; an explicit present path loads.
The non-AROS build retains the upstream fallback. These are host boundary
tests, not native disk-I/O proof.

Two fresh isolated 8-CPU AROS QEMU boots used the same public client, CLI,
handler, dependencies and synthetic XDR cache. The cache describes one
`unadmitted_probe` file vdev at `/RAM/unadmitted-probe-never-created`;
there is no physical device or real pool backing. The native probe verifies
the exact `/boot/zfs/zpool.cache` path is readable and decodes successfully.

- Old engine `shared-engine.3FShD5Gn`, run `public-service-run.z3nZ5eSZ`:
  probe PASS, then operation 3 returned status 9 and **one pool row**.
  The public client's empty-namespace assertion at line 4166 failed.
- Fixed engine `shared-engine.1B5mfjSP`, run `public-service-run.co8jj80d`:
  probe PASS, empty initial namespace, all public-service lifecycles and
  authenticated mirror/DOS write-read/read-only reimport passed. Manager
  clear policy/authorization/mounted-refusal checks also passed. Harness
  exited zero and pruned its generated ISO after completion.

Hashes and serial output are retained alongside this document. Full builds
and run evidence remain under the external candidate's `build-audit`.

Initial runs `RyJMKvud` and `JdxkuC62` are **invalid comparisons**: the
probe was linked without AROS `-nix` and returned before the service started.
The retained screenshot shows the shell prompt, not a system hang. The
corrected probe uses `-nix` and emits errors on every failure path. An initial
compile also required the nvpair API's `const char **` string result type.

## Reproduction

Generate the fixture with `make-boot-cache-probe.py HOST_LIBNVPAIR OUTPUT`,
which refuses an existing output and never opens a device. Build
`boot-cache-probe.c` with the selected AROS SDK, `-nix`, the port's nvpair
archive/private pthread, SPL headers and `aros/engine/config.h`.

Pass `AROS_TEST_BOOT_CACHE_FIXTURE` and `AROS_TEST_BOOT_CACHE_PROBE` to
`test-qemu-public-service.sh` along with its ordinary explicit artifact
inputs. The runner maps the cache into the ISO, assigns `BOOT:`, runs the
visibility probe and requires its PASS marker. Persistent mode is rejected
because its separate startup sequence is not qualified for this probe.

This closes startup namespace admission, **not** damaged-pool `clear -F`
rewind qualification, stock-nightly SDK gaps or physical-device durability.
