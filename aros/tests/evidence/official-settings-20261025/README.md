# Official-runtime settings qualification

Tested against the unmodified 2026-10-23 nightly SDK and runtime. Base ISO
SHA-256: `38b5689f4d5c23b0765a3fbc266c67928d841d81c754675a107dd8bc0b50d6e3`.
Each isolated QEMU guest had eight vCPUs, 2 GiB RAM, no NIC and no writable
block devices. ENV: and ENVARC: were RAM directories. Only test programs,
startup/GRUB configuration and the disposable configured-security fixture
were added to the ISO; no replacement runtime libraries were installed.
Never install the empty-password security fixture on a real user system.

## Results

- Full native preferences suite: PASS, including startup IFF records,
  malformed-record rejection, transaction recovery, protection and ownership.
- Host identity API plus actual OpenZFSHostIdentity command: PASS. Valid
  records reached both ENV: and ENVARC:, three invalid command forms failed,
  an occupied ENV transaction preserved the previous current record while
  reporting the saved-only result, and retry completed publication.
- A configured unprivileged owner was refused by both API and command;
  previously published records remained unchanged.

The initial host-identity test failed at its logout assertion. secLogoutA
returns the restored owner, not a Boolean. The test now compares it with
the captured original owner; no production implementation was changed.
Both serial logs are retained. Successful retest was a fresh guest.

This is RAM-only settings/security evidence, not disk durability, engine
link/runtime qualification, pool interoperability, or physical-DUT evidence.
The official full-engine/CLI pwrite and pipe2 prerequisite gaps remain open.

Build with `bash aros/ui/build-preferences-test.sh OUTPUT` and
`bash aros/ui/build-hostid-test.sh OUTPUT`, setting AROS_SDK and AROS_CC.
Retained recipes, input hashes and serial logs accompany this report.
Original artifacts: `/mnt/short-term/openzfs-official-settings.CRed8uWI`.
