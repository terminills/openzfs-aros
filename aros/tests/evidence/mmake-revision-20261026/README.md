# Incremental build revision provenance

Both real MetaMake fragments previously depended only on META when generating
zfs_gitrev.h. In isolated Git fixtures, a tracked source edit left the old
clean revision embedded in the header. Both pre-fix tests failed on that
specific mismatch; the retained log is the negative reproduction.

The CLI and engine rules now have distinct phony recheck prerequisites and
explicit dependencies on the upstream generator. The generator already
compares content before replacing the header, so a no-change build does not
rewrite it or rebuild dependent targets.

`aros/distribution/test-mmake-gitrev.py` invokes GNU make on copies of the real
fragments and generator. Both tests pass initial generation, no-change header
and consumer timestamp stability, tracked dirty-source refresh, and a new
committed revision without changing META. A small dependent file checks make's
rebuild decision; it is not an AROS binary or substitute for runtime testing.
The existing 11 package-staging evidence tests also pass.

Commands (temporary fixture trees are automatically removed):

```
TMPDIR="$PWD/build-audit" python3 aros/distribution/test-mmake-gitrev.py
TMPDIR="$PWD/build-audit" python3 aros/distribution/test-stage-mmake.py
```

This is host build-rule verification, not a fresh MetaMake engine build,
official SDK qualification, or native runtime result. No DUT/user-hosted use.
