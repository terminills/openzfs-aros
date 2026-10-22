# BoringSSL portability patch

`boringssl-aros.patch` preserves the three portability changes from the
AROS Chromium BoringSSL source tree at upstream commit
`4fa4804c8ab4521079af62dba5260a99c34b8a29`. Upstream source and its licenses
remain in the prepared dependency checkout; this repository does not vendor
or replace the crypto implementation.

- `crypto/internal.h`: use the existing fallback for carry/borrow operations
  in AROS C++ builds, where the C `_Generic` implementation is not applicable.
- `crypto/x509/x509_def.c`: select `PROGDIR:etc/ssl` as the AROS certificate
  directory default. OpenZFS passphrase derivation does not consume it.
- `include/openssl/target.h`: identify AROS and disable socket BIOs. AROS socket
  library bases are task-owned; these BIOs lack the required opener context.
  This does not disable pthread locking or provide an alternative socket layer.

The original Chromium-context comment is retained for provenance. OpenZFS uses
the crypto archive for PBKDF2, not for socket/TLS networking. This patch and
the private compiler shim are portability integration, not new cryptography.

Use `aros/engine/prepare-crypto-source.sh` followed by `build-crypto.sh` as
documented in the engine build guide. Both operate in private artifact
directories. Pinned-source reproducibility is separate from security currency,
FIPS certification and qualification on physical hardware.
