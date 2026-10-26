# Current generated MetaMake CLI and tools

At clean source revision 886132ba57e35b4249786b3de553ed25a328a955, private
build mmake-cli.P3GyEUmG generated a fresh makefile from the root mmakefile.src
using the selected AROS templates. Empty output directories compiled and
linked zfs, zpool, both startup commands, ExecHelper, RuntimeCheck, HostIdentity,
Manager and the resident-device tool. Each target was selected with MetaMake's
TARGET variable and eight GNU make workers. Compiler/SDK, configuration,
nvpair/pthread/crypto archives and pipe2 import remain explicit matched
prerequisites; this is not an official SDK/bootstrap qualification.

Retained setup failures distinguish fixture mistakes from port defects:
missing generated target configuration; missing generator dependency source;
and omitted TARGET causing no-op quick targets, caught by missing-artifact
checks. None of those incomplete builds was run in AROS. The corrected
four-command trial succeeded before the full nine-target build recorded here.

Private eight-CPU QEMU run public-service-run.ncufFf4U used the fresh command
directory and device binary with shared-engine.SNTTTWQk. It exited zero with
the full public storage PASS, shared-library lifecycle, real CLI force
operations, native DOS/relative member spelling, authenticated Manager API,
resilver, standalone handler/device and exact read-only round-trip checks.
The actual fresh Manager binary was built but not opened in this run; typed
API tests are not visual GUI qualification. Cold recovery with this new CLI
directory also remains a separate test.

## Subsequent cold and visual qualification

Fresh eight-CPU persistent run public-service-run.4LUkzNl2 exited zero using
the same freshly generated CLI/device set. Real CLI forced dRAID distributed
spare replacement, sequential rebuild and scrub completed. Separate cold-VM
healthy/degraded/rebuilt redundancy and beyond-parity refusal/restoration
passed with exact encrypted/plain payload and device access. Host FAT checks
passed after both phases, full member hashes remained unchanged during cold
verification, and independent zdb reported no leaks. Evidence is in persistent/.

Private eight-CPU native-gui-run.yCohsZWz opened the actual fresh Manager
binary, queried live Simple-mode pool health, and saved the raw response.
The screenshot shows ONLINE pool/mirror/members and zero error counters.
After normal close, the acceptance client verified the saved response contains
raw topology/GUIDs, error counters, scan and checkpoint data rather than only
the display summary. Read-only reimport and the complete storage suite passed;
runner exit zero. Screenshots, input hashes and actions are in gui/.

The first attempted close dragged the window left instead; the retained
after-close screenshot identifies this input-placement mistake. The close
gadget at the new position succeeded. An optional capture attempted after
that failed because the successful runner had already removed its QMP socket;
it was not a VM crash or failed test. Both successful runs hashed and pruned
their generated media. These results close the separate checks described
above, not the official-runtime or physical-hardware qualification gaps.

The full build log is compressed under build/; build recipe, configuration,
generator and binary hashes, serial output and pruned-media records are
retained. environment.sh is referenced by build.sh with its final alternate
builder invocation omitted. No DUT or user-hosted session was accessed.
