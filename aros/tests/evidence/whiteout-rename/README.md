# Native whiteout rename

`3387a2c` enables the upstream native whiteout transaction and replay callback.
It replaces caller-input assertions with EINVAL checks for missing/extra
whiteout attributes, incorrect mode, nonzero device and incompatible flags.
The existing upstream ACL, quota and transaction paths remain in use.

Eight-CPU five-boot run `qemu-run.GHeP0YIN` passed. The writer creates a tagged
file, rejects malformed requests, renames it and leaves a character-device
marker with device zero, zero size, one link and the expected parent. The
destination retains the original object and bytes. Cold healthy and each
surviving-member imports recover this state and refuse read-only mutations.
Writable reopen and the remaining mirror regression also pass.

Independent original and reopened-pool `zdb -bcc` checks report no leaks;
all four extracted member hashes remain unchanged during inspection.
Logs and hashes are retained here; complete external artifacts are under
`/mnt/short-term/openzfs-qualification-20261024.aGFlVF7T`.

This qualifies internal ZPL whiteout metadata, not union/overlay filesystem
semantics, a new DOS packet, arbitrary special-device I/O, or physical media.
This run does not qualify pending whiteout log replay after abrupt loss.
The separate crash test added in `838981d` is intended to cover that boundary.
Builds reuse upstream objects and selected patched AROS runtime prerequisites.
