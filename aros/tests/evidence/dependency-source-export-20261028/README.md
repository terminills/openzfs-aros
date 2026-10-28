# Dependency source export qualification

The selected dependency build `dependencies.YF6P1Tca` exported successfully to
`/home/terminills/Documents/GitHub/openzfs-package-staging/dependency-sources.8jpx0irq`.
The export occupies 136 MiB and retains 41 pthread and 7,040 patched crypto
source files plus the pinned complete libtirpc archive and build provenance.
No dependency archive was rebuilt, substituted, or installed.

All 7,101 exported manifest entries passed independent `sha256sum -c`.
`scope.json` identifies the verified original archive hashes and explicit
external requirements. Original absolute-path manifests are provenance;
the exported `SHA256SUMS` is relative and verifies without the old source tree.

Three host unit tests cover valid and invalid manifests, source drift,
duplicate/relative/empty records, literal environment parsing, exact-byte
copying, no-overwrite, unrecorded inputs and symlink rejection. These are
exporter tests, not AROS runtime or fresh dependency compilation evidence.
The matching SDK/toolchain, OpenZFS nvpair sources and host tools remain
external prerequisites. No full bootstrap or binary-release closure is claimed.
