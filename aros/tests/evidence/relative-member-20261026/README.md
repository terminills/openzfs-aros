# Explicit relative member lookup

Isolated QEMU run P2AdY43b reproduced a real CLI failure: `zpool online
public_force_target ./disk2` could not find the existing member while the
command's current directory was RAM:OZForceCreate. Runner exit: 1.

The native libzutil lookup now resolves explicit relative paths through DOS
instead of Linux import-directory expansion. Bare shortnames retain upstream
search behavior. The existing DOS path helper validates traversal rather
than lexically removing missing or non-directory components.

Fresh common CLI compilation and all CLI links passed. Run 3TisQLxg used
cli-link.29cnkfv7 with shared-engine.Nmm3fNwi and a rebuilt acceptance client:

- `./disk2`, `././disk2`, and `../OZForceCreate/disk2` successfully online
  the existing member through the real CLI.
- An existing nonmember, a missing file, a missing intermediate directory,
  and traversal through a regular file are rejected. The test requires the
  actual upstream missing-device diagnostic naming each rejected argument,
  not merely a failed command status.
- The fixture restores its DOS current directory before checking results.
- Existing force create/add/attach/replace safeguards, typed Manager paths,
  resilver, exact read-only payload, and shared-library lifecycle tests pass.
- Full public-service runner exit: 0. Successful media hashed and pruned.

Logs and input hashes are retained here. Failed media remain on external
storage. This is private-platform QEMU file-vdev qualification, not official
SDK compatibility or physical-device evidence. No DUT/user-hosted access.
