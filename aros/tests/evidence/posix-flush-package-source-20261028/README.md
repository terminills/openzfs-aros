# Packaging-compatible source-retained POSIX flush runtime

The flush snapshot builder now retains the baseline module configuration,
provenance and licenses as well as compiler inputs, and rejects a snapshot
already containing the hook. The resulting `posix-flush-source.j099bkwp` passes
the existing package stager's POSIX source gate (1519 retained paths).
All 16 stage-mmake tests pass; duplicate hook application is rejected before
building. Independent source replay is byte-identical.

Selected POSIX SHA-256:
`8f7eaf59a32d2d2b14e2e7b0dc67056fe1f9af8ea12487ff6d91243184b2c422`.
It differs from the preceding snapshot, so hosted runtime verification was
repeated with these exact bytes, matched stdc, retained fixed stdcio, and the
new independent-reader runtime preflight. The full writer/service suite,
three stdio exit paths, host zdb check, independent cold import, exact DOS
metadata/payload and degraded-mirror checks pass. Read-only phases leave
both member hashes unchanged. Logs and all runtime input identities are here.

This establishes snapshot packaging eligibility and this private hosted
profile, not an updated fully qualified release bundle, QEMU or stock SDK.
No source snapshot replaces the user's installed OS runtime automatically.
