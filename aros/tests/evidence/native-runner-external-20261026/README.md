# External native-integration launcher

The native-import runner incorrectly compared an external artifact filesystem
with its checkout filesystem and froze scripts under checkout/build-aros.
An external checkout therefore rejected a valid external destination. The
shared preflight now compares with the system filesystem, validates before
allocation, and freezes under the selected artifact root. An explicit source
root survives execution from the relocated immutable script.

The disposable fixture allowlist now accepts the same qemu-run/native-raidz-run
names under the validated artifact root as under checkout/build-aros. Other
paths remain rejected. This does not authorize physical disks.

`test-native-runner-preflight.sh` passed from this external checkout:

- Two immutable snapshots exactly match the source runner.
- An allowed external fixture reaches the missing-service guard.
- An unrelated fixture is rejected with status 2.
- A writable system-filesystem artifact destination is rejected.
- No ISO or qcow2 image is allocated and no guest is launched.

Command: `bash aros/engine/test-native-runner-preflight.sh "$PWD/build-audit"
/home/terminills/Documents/GitHub`.

An initial test incorrectly expected a filesystem diagnostic for `/`, which
failed the earlier writable-directory check instead. The final test requires
an explicitly writable system directory. Its logs are retained here; the
initial logs remain under build-audit/native-preflight.FnpDWriS.

This is launcher admission evidence, not CLI/UI or native device runtime
qualification. Those remain separate integration runs.
