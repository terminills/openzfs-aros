# Recovery refusal reporting

The native transport currently rejects `ZFS_IOC_CLEAR` rewind policies.
Upstream libzfs nevertheless treats most failed TRY_REWIND calls as successful
feasibility checks and attempts to decode a recovery response. On AROS an
unsupported transport request supplies no such response.

Negative eight-CPU QEMU run `public-service-run.3gyDaycs`, using the preceding
MetaMake binaries `mmake-cli.P3GyEUmG`, reproduced `internal error: out of memory`
for `zpool clear -nF public_mirror`. The new test failed at the expected call.
This runtime evidence is a misleading memory error, not proof of a zero exit
status or successful recovery.

The AROS libzfs branch now preserves ENOTSUP/ENOSYS instead of entering that
success path. Fresh generated MetaMake CLI/tool build `mmake-cli.f2fvfhdP` and
the unchanged shared engine `shared-engine.SNTTTWQk` passed isolated eight-CPU
run `public-service-run.dqLZlj6H`. Both `clear -nF` and `clear -F` returned a
nonzero status and the unsupported-operation diagnostic. The remaining full
public storage suite passed, including handler writes and readonly reimport.
The runner requires the new marker whenever packaged CLI tests are selected.

Inputs and normalized serial logs for both runs are retained here. Successful
test media were hashed and pruned by the runner. Selected private SDK/runtime
dependencies are unchanged; this is not stock-nightly or hardware evidence.
Actual clear-with-rewind recovery remains unimplemented and unqualified.
