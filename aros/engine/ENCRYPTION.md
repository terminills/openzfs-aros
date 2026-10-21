# Native upstream encryption bring-up

## Authenticated management encrypted creation

### Cold API-created format and encrypted-volume recovery

The encryption persistence writer now retains the API-created hexadecimal and
passphrase filesystems and raw-key volume under distinct names, unloads their
keys and syncs the pool. Writer native-import-run.FCtmiBjX and fresh read-only
reader native-import-run.LHkmR0ZL both exit zero. Each cold object starts locked;
CLI verify-only succeeds while leaving it locked, then explicit key-file load
succeeds. Filesystem reads and encrypted-volume block read at offset 512 match
the original 512 bytes exactly. New filesystem writes and zvol writes are
refused, followed by clean close/unmount and key unload. RAM key files are
removed. Mandatory writer and cold-reader markers prevent using an older
fixture without this coverage. Independent control-integrity.MCMXPp2q passes
zdb -bcc with No leaks; original sources, retained writer disks and read-only
guest disk hashes remain unchanged. This is clean export/cold recovery, not
power-loss qualification or an encrypted filesystem mounted on a zvol.

### Interactive encrypted creation acceptance

AROS_TEST_ENCRYPTED_CREATE_GUI=1 (with GUI and writable zvol regression enabled)
replaces the raw-key API creation call with a synchronous actual Manager window;
there is no automatic creation fallback. Native-import-run.aZsj71O7 exits zero
after selecting Datasets/create, entering qemu_mirror/gui-crypt, the public RAM
fixture key path and encryption=on,keyformat=raw, disabling dry-run, reviewing
the exact request and submitting. The Manager reports authenticated creation
errno=0. Screenshots 01 through 06 and ui-actions.jsonl preserve the interaction.
After normal Manager close, independent upstream CLI queries check encryption
and prompt keylocation; DOS writes/flushes 512 bytes, then unmount/unload/reload/
remount and exact readback pass. The dedicated GUI marker is mandatory.
The rest of the native regression passes, including encrypted zvol I/O; the
final Manager window is also closed normally before terminal hash checks.
Independent control-integrity.RqNqig9I passes zdb -bcc with No leaks and source/
extracted-member hashes unchanged. This covers one root-authorized raw-key GUI
creation, not all key formats through GUI or cold API-created persistence.

### API preparation and format coverage

Native-import-run.jsgaVwqu verifies malformed key material through authenticated
creation: raw files of 31/33 bytes, hexadecimal text of 63 bytes or a non-hex
character, and passphrases of 7/513 bytes all fail with EINVAL. After each
failure the actual CLI confirms the target dataset is absent and the source
file can be deleted. A valid raw key then creates the same target successfully,
reports keystatus available and is destroyed. The mandatory invalid-material
marker and the full native regression pass with runner exit zero. This tests
input validation and retry cleanup, not allocation-failure injection or a
general proof that every secret buffer is erased.

Authenticated creation accepts optional pbkdf2iters for passphrase keys. It
defaults to upstream DEFAULT_PBKDF2_ITERATIONS (350000); explicit values must
meet upstream MIN_PBKDF2_ITERATIONS (100000) and the native BoringSSL uint32
iteration ABI. Raw/hex keys reject this option. Validation precedes key-file
reads and derivation, and the same value is persisted and used for the KDF.
Native service/Manager encryption-iterations.ALaumEQY builds. Writer pHZRIiMZ
passes explicit 350001 property readback, key reload, exact DOS data and three
invalid requests (99999, 4294967296, raw key with iterations), each leaving no
dataset. Fresh reader JpRUsEVT passes cold key verification/load and exact data
for the retained custom-iteration passphrase filesystem, hexadecimal filesystem
and raw-key volume. Both complete runners exit zero; cold reader/source/writer
disk hashes remain unchanged. No interactive custom-iteration GUI test is
claimed by these API/CLI checks; the Manager help documents the option.

The encryption=on alias is passed to upstream dsl_crypto_params_create, which
resolves it to AES-256-GCM. The previous native preparer incorrectly rejected
the alias because its enum precedes OFF. Service encryption-alias.EXa2KJz5
and native-import-run.WFEgJcEu pass the full regression: actual CLI reports
aes-256-gcm, DOS data survives unload/reload, and encryption=off with supplied
key material fails without creating a dataset. The runner requires the alias
marker separately from explicit-algorithm tests.

Dataset create (operation 37) now accepts an optional fully qualified native
key-file path in Argument and encryption/keyformat in its existing comma-
separated options. No key bytes or passphrase enter the message or job record.
The bound create adapter checks caller create/mount authority and writable,
nonsuspended pool state before reading the file with the captured native
identity. The prepared CREATE nvlist uses upstream hidden_args; CREATE retains
upstream property, feature, permission and transaction checks. Temporary key
buffers, hidden-nvlist copies and packed request bytes are wiped on cleanup.

The adapter prepares raw/hex/passphrase material, with fresh native salt and
default upstream PBKDF2 iteration count for passphrases. Persistent keylocation
is prompt, not the source path; subsequent Manager loads name a key file and
CLI loads use -L. The native UI labels Argument as Key file (optional) and
documents the options. There is no secret prompt or encrypted-IFF keystore.
With no key file, existing unencrypted creation retains its original route.

Service/Manager encrypted-create.RtWgbRlV builds. Native API test
native-import-run.YzidZaEg exits zero with the full CLI/zvol regression:
missing keyformat is refused without creating a dataset; UID77 without
create permission is refused with EPERM and no dataset; root creates a
raw-key AES-256-GCM filesystem. Independent actual CLI queries confirm crypto
and prompt location, then native DOS writes/flushes the payload. Unmount,
unload, explicit-key-file reload, remount and exact DOS readback pass before
cleanup. This is API acceptance, not actual GUI clicks. Cold API-created
recovery and wider negative cases still need their own tests; implemented
branches are not counted as verified merely by compilation.

The expanded native-import-run.LRsY1YP5 also exits zero with the full native
regression. Authenticated creation covers hexadecimal and passphrase filesystems
and a raw-key encrypted volume. Actual CLI queries check keyformat/type,
passphrase iterations (350000), and volume size (2097152 bytes). All three
unload and reload via the CLI key-file path. Both filesystems preserve exact
512-byte DOS payloads after reload. Volume coverage is creation/properties/keys,
not encrypted block I/O. The mandatory crypto-formats marker records these
bounds. Independent control-integrity.7ZiJY6kw passes upstream zdb -bcc with
No leaks, unchanged extracted-member hashes and original backing hashes.

Follow-up native-import-run.SxHZlPZu exits zero with encrypted volume data I/O:
the authenticated zvol transport writes 512 patterned bytes at offset 512,
flushes and closes the descriptor/session. Actual CLI unload/reload follows,
then a fresh read-only descriptor/session reads exactly the original bytes.
Cleanup destroys the volume. The separate mandatory encrypted-volume-I/O
marker distinguishes this from property-only acceptance. This is warm key
cycling, not cold encrypted-zvol persistence or filesystem-on-encrypted-zvol.

Retained aYVkfRse created the encrypted dataset successfully, then failed an
incorrect extra mount: upstream zfs set mountpoint already mounted it during
postfix processing. The corrected test explicitly unmounts before mounting;
the service binary is unchanged.
Independent control-integrity.2UYxynXj passes upstream zdb -bcc with No leaks;
extracted-member and original backing hashes are unchanged.

## Cold CLI encrypted dataset and replacement-key recovery

AROS_TEST_ENCRYPTION_PERSIST=write/read adds mandatory gates to the isolated
native writer/cold-reader harness. The writer retains the encrypted dataset
after unmount and pool sync; the fresh reader requires the writer marker.
Both use the actual upstream commands and native DOS handler, not direct
fixture crypto calls. Public deterministic key files are recreated only in
each guest's RAM:; the replacement wrapping key and encrypted payload must
survive through the retained disks.

Writer native-import-run.xWRemUVe and cold reader native-import-run.XvO8iuNN
both exit zero with service cli-key-ioctls.Ngt7Wz15 and CLI cli-link.qLJvc5Kp.
The fresh read-only import starts with keystatus unavailable. Old-key load is
refused. `zfs load-key -n` verifies the new key without changing keystatus;
normal load succeeds, mount exposes the exact 512-byte DOS payload, and
file creation is refused. Key replacement is also refused: keylocation is
unchanged, subsequent old-key load still fails and the replacement key still
loads. Final unload succeeds. Original backings, retained writer overlays and
cold-reader overlays preserve their recorded hashes.

The CLI reports generic `Key change error: encryption failure` for this
read-only refusal; no specific error string is fabricated by the test. The
test checks failure and retained key behavior. This is clean sync/export and
fresh-guest recovery, not interrupted-write/power-loss qualification, and it
does not yet cover cold CLI passphrase or hexadecimal key files.
Independent control-integrity.rFODaGAK passes upstream zdb -bcc with No leaks;
extracted member, original source and retained writer hashes also pass.
In writer-retain mode, the earlier encrypted-create cleanup marker covers
temporary RAM key-file removal; the separate retained marker explicitly
records that the dataset was not destroyed.

## Native upstream CLI key transport

The authenticated ioctl transport now admits ZFS_IOC_LOAD_KEY, UNLOAD_KEY and
CHANGE_KEY. Requests retain the existing aggregate allocation bound and native
credential snapshot. Additional caller pointers/configuration, history and
inline operation flags are rejected; packed upstream nvlists retain the
upstream schema, hidden_args handling and delegated load-key/change-key checks.
Upstream also enforces active key references, suspended pools and read-only
restrictions; load/unload are memory operations, not automatically classified
as persistent writes. Client and service transport copies are explicitly wiped
before release by the existing oz_ioctl_wire_clear cleanup.

Service cli-key-ioctls.Ngt7Wz15 builds successfully. Retained native run
55P0dWGW correctly refused unloading a mounted dataset as busy, but failed its
fixture's incorrect exit-code expectation (1 instead of upstream -1).
cmd/zfs load_unload_keys returns -1 when any requested key operation fails;
the corrected fixture preserves that contract, with no engine workaround.

Native native-import-run.OhPYl2Ml exits zero with this service and CLI
cli-link.qLJvc5Kp. Actual commands verify mounted unload busy, nonowner UID77
Permission denied with key still available, root unload, wrong-key refusal
with key still unavailable, correct reload, raw wrapping-key replacement,
old-key refusal, new-key reload, and exact DOS payload after remount. Dataset
and public fixture keys are removed before the remaining full regression.
Independent control-integrity.YN2ReoXS reports No leaks and unchanged member
and source hashes. This is warm raw-key lifecycle coverage; cold CLI recovery,
passphrase/hex CLI variants, delegated positive cases, verify-only loading and
malformed ioctl-envelope matrices remain separate work. No GUI behavior or
physical media qualification is inferred from this command-line run.

## Actual upstream CLI encrypted creation

The upstream key-file reader uses fopen mode `re`. Native AROS posixc rejects
the `e` suffix with EINVAL; retained `native-import-run.dsVxTQ3R` fails at that
point before dataset creation. The AROS-only adapter uses binary read mode,
then explicitly sets FD_CLOEXEC before consuming key material. A failed fcntl
closes the stream and preserves its error. This is not atomic open+CLOEXEC;
it does not establish a concurrent-exec security guarantee. Other platforms
retain their existing code, and no shared OS source is changed.

All common upstream CLI units compile in cli-probe.SrReQfm7 and the whole
commands link in cli-link.qLJvc5Kp. Native run native-import-run.H6SEaBaV
exits zero: actual zpool enables encryption, actual zfs create supplies
aes-256-gcm/raw/file keylocation, properties confirm encryption and available
key, actual zfs mount exposes OZCRYPT:, and DOS writes/flushes/reads 512 exact
patterned bytes. Unmount, destroy and disposable key-file removal pass, then
the full native CLI/zvol regression passes. The runner requires the encrypted
create marker. The key is public deterministic fixture data in private RAM:.
This proves warm raw-key CLI creation, not cold encrypted CLI persistence,
passphrase CLI creation, Manager creation controls or CLI key load/change/unload.
Independent control-integrity.MHkwTMU4 reports No leaks from upstream zdb -bcc;
extracted member and original backing SHA256 checks remain unchanged.

The AROS ZPL owner now requests decrypted objset ownership and releases that
same ownership mode. This retains upstream key mappings during filesystem use,
verifies authenticated metadata, rejects opens without a loaded key, and keeps
key unload from succeeding while a filesystem is owned. Unencrypted datasets
continue through the same upstream ownership interface.

`encryption-test.c` is a disposable QEMU fixture, not a public key service. Its
deterministic raw key is public test data; no user keys or external storage are
involved. It creates an upstream AES-256-GCM encrypted filesystem and checks
exact file contents, EBUSY unloading an owned filesystem, EACCES accessing a
locked filesystem, wrong-key EACCES, correct-key reload and another read.
The cold phase starts without a key and repeats the lifecycle on a read-only
pool. Engine creation and encryption use upstream crypto parameters/keystore,
not an AROS encryption format.

Initial build `encryption-engine-build-1.log` failed because the test omitted
the RawPutChar declaration; adding proto/exec.h fixed it. Build 2 passed.
`admission-service-run.z1dXfZYf` passed writable and cold encryption checks,
the full three-boot regression suite and independent offline checks. The
runner exited zero (`encryption-run-1.log`); read-only boot disk hashes matched.

The extended fixture writes and byte-checks a 64 KiB patterned file through
native ZPL, replaces the wrapping key with upstream `spa_keystore_change_key`,
unloads it, rejects the original key, and reads the payload with the replacement.
A fresh read-only boot again rejects the original key and recovers all bytes
with the replacement. `admission-service-run.7m9e3LZw` passed these checks plus
the complete three-boot regression and offline checks; the runner exited zero
(`encryption-rekey-run-1.log`, build `encryption-rekey-engine-build-1.log`).
The read-only boot preserved both disk hashes. The additional explicit rekey
runner assertions were added after this frozen runner started; their exact
markers were separately checked against its retained serial evidence.

## Authenticated unload API

Operation 63 (`encryption.unload-key`) uses captured native credentials and
upstream `load-key` delegation, the same permission used by upstream unload.
It accepts only a confirmed encryption-root target, with no argument, options,
force, recursive or dry-run flags. It rejects mounted targets, checks suspended
pools before entering the upstream keystore, and retains upstream active-key
reference checks. Unload changes memory, not disk, so read-only pools support it.
The Manager routes this operation through its authenticated worker; capability
reporting and help text describe the implemented interface.

The separate client verifies unprivileged denial, reads all 64 KiB through the
native DOS handler while mounted, observes EBUSY for mounted unload, unmounts,
unloads successfully, then observes EACCES for both a repeat unload and mount.
The same checks run after cold read-only import. The engine fixture prepares
the public test key; the client is not linked to the engine and cannot bypass
the authenticated service interface.

`admission-service-run.hHpMgmSR` passed the complete three-boot suite and
offline checks, with unchanged read-only boot disk hashes; runner exit zero
is retained in `key-unload-run-1.log`. Build evidence is
`key-unload-engine-build-2.log`, `key-unload-client-build-2.log` and
`key-unload-manager-build-1.log`. Manager compilation is verified; a new
interactive GUI unload run is not yet claimed.

## Authenticated raw-key file loading

Operation 62 (`encryption.load-key`) accepts an encryption-root target and a
fully qualified native key-file path in Argument, with confirmation and no
other flags/options. It checks the captured caller's upstream `load-key`
permission and suspended-pool state before file access. The upstream computed
keyformat must be raw; other formats currently return ENOTSUP. Native external
paths use the caller-authorized locked-file resolver. Own mounted volumes use
the direct DOS stream adapter to avoid calling back into the service task.
The file must contain exactly 32 bytes, including an EOF check. Key bytes never
enter message/result/job buffers; local and temporary nvlist copies are wiped.
The upstream keystore validates and takes ownership of the wrapping key.

`admission-service-run.q8aXhmV9` passed the complete three-boot/offline suite
(`key-load-run-1.log`, exit zero). WRITE and cold read-only VERIFY both reject
unauthorized callers, 31/33-byte files and wrong keys, accept the correct key,
reject duplicate loads, mount and read the full encrypted 64 KiB through DOS,
then unmount/unload. Both read-only disk hashes match. Engine, client and
Manager builds are `key-load-{engine,client,manager}-build-1.log`. Manager is
compiled/wired; interactive key GUI testing remains outstanding.

The extended fixture creates a separate encrypted root with a persisted
upstream `load-key` grant for UID 77. That user is denied when the correct
public test key resides in a root-only RAM file, then succeeds after root
grants file read access; delegated unload also succeeds. The same user still
cannot load or unload the undelegated original root. A separate plaintext
OpenZFS source dataset supplies a key through its mounted DOS volume, proving
the direct own-volume adapter does not deadlock. The key file and delegation
persist into the cold read-only phase, which repeats all checks.

`admission-service-run.pCMu6WG9` passed these tests plus the full three-boot
suite and offline verifiers, with unchanged read-only hashes and runner exit
zero (`key-source-run-1.log`). Builds: `key-source-engine-build-1.log` and
`key-source-client-build-1.log`. All keys and volumes are disposable fixtures.

## Authenticated raw-key replacement

Operation 64 (`encryption.change-key`) uses the same caller-authorized key-file
reader and wiping as load, but requires upstream `change-key` permission and a
writable, non-suspended pool. It calls `spa_keystore_change_key` with
`DCP_CMD_NEW_KEY`, keeping raw format and the existing keylocation. The old key
must be loaded. Upstream permits mounted encrypted datasets to remain usable
during replacement; the AROS adapter does not add an unnecessary unmount rule.
Manager routes the confirmed operation through the authenticated worker and
warns that the old key stops unlocking the encryption root and its snapshots.

`admission-service-run.Zk7GSZHh` passed the complete three-boot and offline
suite (`key-change-run-1.log`, exit zero). A caller with only load-key permission
cannot change keys. A locked root rejects replacement; a loaded, mounted root
accepts replacement and remains readable through DOS. After unload, the old
key fails and the new key opens the filesystem. A fresh read-only boot repeats
old-key rejection and exact DOS content recovery, and rejects key replacement
with EROFS. The read-only disk hashes match. Engine/client build-1 and Manager
build-2 logs use the `key-change-` prefix. The Manager confirmation update was
compiled after the frozen API suite; interactive GUI verification remains open.

At that commit this did not yet provide passphrase derivation, secure prompting, encrypted
dataset creation in Manager, raw encrypted replication or key inheritance UI.
Larger encrypted workloads and failure injection still need coverage.

## Interactive Manager verification

The private `encryption` scenario seeds only public old/new key files. The
launcher never issues replacement (64): it must be performed in Manager.
After Manager closes, an independent authenticated verifier unloads the key,
rejects the old file, loads the new file, mounts the dataset and compares exact
DOS file bytes, then unmounts, unloads and exports the pool.

`native-gui-run.ToCGWewG` passed this workflow. Actual Manager actions were:
load old key, replace with new key, unload, attempt old key (errno 13), load new
key (errno 0), save activity/response, close. `serial.log` retains the saved
UI logs and ends with `RESULT authenticated native GUI PASS`. QMP quit then
finished the private runner with exit zero (`key-ui-run-1.log`); no shared
hosted session or DUT was used. Launcher build: `key-ui-launcher-build-1.log`.

Screenshots in that directory: `key-load-confirm.png`/`key-load-result.png`,
`key-change-confirm.png` (replacement warning), `key-change-complete.png`,
`key-unload-result.png`, `key-old-rejected.png`, and `key-new-loaded.png`.
The initial `key-change-result.png` is an in-progress capture, not evidence
of completion; the later complete capture and independent verifier prove it.

## Hexadecimal key files

Load/replacement now recognize upstream `keyformat=hex` as well as raw.
Hex material follows `libzfs_crypto.c` first-line semantics: exactly 64 ASCII
hexadecimal digits (either case), optionally followed by LF. Later lines are
ignored like upstream getline; CR is not trimmed. Raw files retain their exact
32-byte/EOF rule. Input buffers are bounded, decoded keys and input material
are wiped, and replacement preserves the existing keyformat/keylocation.
Passphrase format still returns ENOTSUP; no guessed or substitute derivation.

`admission-service-run.EzzSgDKe` passed the complete three-boot and offline
suite (`key-hex-run-1.log`, exit zero). Native tests reject 63/65-digit material,
non-hex characters, CRLF and incorrect keys; accept mixed-case hex with no LF,
with LF, and with ignored subsequent text; replace while mounted, reread exact
DOS content, reject the old key and reopen with the replacement. A cold
read-only boot repeats recovery and refuses replacement; disk hashes match.
Build logs use `key-hex-{engine,client,manager}-build-1.log`. The earlier GUI
evidence exercises the same operations with raw keys, not a claimed hex GUI run.

## Passphrase KDF dependency verification

The existing Chromium AROS BoringSSL archive exports the upstream-compatible
`PKCS5_PBKDF2_HMAC_SHA1` operation required by libzfs. Read-only linking against
that archive with the ADT SDK and consistent pthread archive succeeds. No
Chromium source/build artifacts were changed. `build-pbkdf2-test.sh` records
archive/header/binary hashes and each thin-archive member's content hash.

The diskless private QEMU probe passed RFC 6070 password/salt vectors at 1, 2
and 4096 iterations (`pbkdf2-run.Avo1RdVa`, `pbkdf2-run-1.log`). Its runner
printed harmless initial missing-serial-file diagnostics before QEMU created
the log; the polling loop now checks existence first. The extended probe also
passed a 32-byte, 350000-iteration SHA1 vector with salt bytes 08 07 06 05 04 03
02 01, independently generated using host Python hashlib. This verifies both
SHA1 output blocks required for an OpenZFS wrapping key, not only 20 bytes.
Evidence: `pbkdf2-run.Te8UAoKI`, `pbkdf2-run-2.log`, `pbkdf2-link-2.log`; runner
exit zero and native PASS. Dependency manifests and build recipe were copied
into the second evidence directory after its run. The runner now copies them
automatically for subsequent runs. Both tests use only public fixture values.
Passphrase file parsing, salt/iteration plumbing and native dataset lifecycle
integration remain to be implemented; this probe alone is not that milestone.

### Full-engine passphrase derivation adapter

`passphrase.c` isolates the actual BoringSSL ABI from upstream kernel headers,
encodes the 64-bit salt little-endian, derives 32 bytes, rejects zero or
out-of-range iteration counts before conversion, and clears output on errors.
The native probe now calls this same adapter against the independent 350000
iteration vector and verifies invalid-count and null-input handling.
`pbkdf2-run.pkTuNW51` / `passphrase-adapter-run-1.log` passed (runner exit 0).

The adapter and crypto archive also link into the entire management engine,
without garbage-collecting unresolved upstream paths. Build logs are
`passphrase-engine-link-1.log` and `passphrase-engine-link-2.log`.
`admission-service-run.kRR38kfc` / `passphrase-engine-run-1.log` passed the
existing write, read-only cold verification, replication completion and
offline validation suite (runner exit 0). This covers regression of the
existing raw/hex key operations, not passphrase dataset management yet.
The second build adds crypto source/header/archive and thin-member manifests;
its executable SHA256 matches the frozen test executable exactly:
`36fd79bc736675a58b288c96b64f4f97fb7773a3302389a07bee8145fd1eae48`.
Those manifests were copied into this evidence directory after the run;
subsequent admission runs copy them before boot. The PBKDF2 runner also
retains the adapter source/header. Regular archives receive a whole-archive
manifest instead of relying on a stale thin-member list.

At that commit, still next: authenticated passphrase file parsing, stored salt/iteration
lookup, fresh-salt replacement, and native encrypted dataset lifecycle tests.
No DUT or shared hosted session was used.

### Authenticated passphrase files

Operations 62 and 64 now accept passphrase files through the same original-
caller-authorized stream path as raw and hex keys. The dataset determines the
format. Loading reads computed upstream `pbkdf2salt`/`pbkdf2iters`; replacement
uses native entropy for a fresh salt and OpenZFS's default iteration count.
Replacement retains keyformat and keylocation. The first line is read, only
LF is stripped, and the KDF uses the same C-string semantics as libzfs. New
passphrases are restricted to 8-512 bytes; existing-key loads do not inherit
that restriction. Temporary buffers are wiped, including old allocations when
growing a line; keys and passphrases never enter request or job records.

`admission-service-run.CJMlWd5e` / `passphrase-api-run-1.log` passed the complete
three-boot and offline suite, runner exit 0. The encrypted passphrase fixture
starts with 100000 iterations. The independent API client checks wrong-key
rejection, first-line/LF handling, mounted replacement, rejection of too-short
and too-long replacements, unchanged file contents, old-key rejection,
unload/reload, CRLF mismatch, cold replacement-key recovery and read-only
replacement refusal. Both write and verify phases report
`Native passphrase key lifecycle PASS`.

Builds: `passphrase-api-engine-1.log`, `passphrase-api-client-1.log`, and
`passphrase-api-manager-1.log`. The extra cold metadata test initially failed
to compile (`passphrase-api-engine-2.log`) because `ZFS_MAXPROPLEN` is only
provided by a userspace header. It now uses `MAXNAMELEN`, matching the upstream
libzfs keylocation buffer; build 3 passed. This test independently checks a
nonzero changed salt, default iteration count, retained passphrase format and
unchanged `prompt` location on cold import.

The stronger run `admission-service-run.RZg0dSQj` failed its cold metadata
assertion (runner exit 1). Offline inspection confirmed the salt, iteration
count and format were correct, but the fixture's DSL property object was empty:
`dmu_objset_create` consumes crypto parameters without applying ordinary DSL
properties, unlike the public create ioctl. The fixture now explicitly sets
`keylocation=prompt` using `dsl_props_set` before testing its preservation.
This was a fixture-precondition error, not evidence of a lost replacement key.
The failed run and `metadata-zdb.log` remain available. That detailed `zdb -dddd`
traversal later aborted while dumping encrypted objects; do not confuse it
with the passing normal offline `-bcc` validation of the first run. Build 4
adds nonsecret metadata diagnostics; build 5 includes the fixture correction.

`admission-service-run.9YJPbkVe` / `passphrase-api-run-3.log` passed all three
boots and offline validation (exit 0, zdb No leaks). The cold report explicitly
shows a new nonzero salt, 350000 iterations, keyformat 3 and location `prompt`.
The cold metadata assertion is now opted into by `Startup-Admission-Read`
through `OPENZFS_TEST_PASSPHRASE_CHANGE`; focused exec/device suites do not run
the key-changing client and must not assert it happened. The admission runner
requires its success marker rather than silently accepting a skipped check.

Final build 6 and `admission-service-run.AIjyafuB` /
`passphrase-api-run-4.log` passed the explicitly gated cold check and the
entire three-boot/offline suite, runner exit 0. The native report shows
`iters=350000 format=3 location=prompt errno=0`; the read-only disk hashes
match. A search of its serial logs found neither public fixture passphrase.

### Interactive passphrase management

The native GUI scenario `passphrase` prepares only public fixture key files.
Only the real Manager issues replacement operation 64. After the GUI closes,
the launcher independently unloads, rejects the original passphrase, loads
the replacement, mounts the dataset, checks exact DOS file contents, unmounts,
unloads and exports the pool.

`native-gui-run.pU0cNlDz` / `passphrase-gui-run-1.log` passed this sequence:
`GUI replacement key and exact DOS content PASS` and
`RESULT authenticated native GUI PASS`; QMP shutdown and runner exit 0.
Screenshots include `passphrase-change-confirm.png`, `passphrase-changed.png`,
`passphrase-unloaded.png`, `old-key-rejected.png`, and `new-key-loaded.png`.
The Manager activity log and response were saved and dumped to serial before
the independent check. The first GUI load returned ENOENT; re-entering both
fields with separately completed input steps succeeded without a reboot or
backend change. Its failed capture and input action log are retained; input
entry timing is the suspected cause, not a proven backend defect.
After the guest stopped, host `zdb -bcc` exited 0 with No leaks, recorded in
`linux-zdb.log`. A signedness-warning cleanup in the launcher (build 2) produced
the same SHA256 as the tested launcher:
`a809f39c42977967eae5280611a5897428353813194f5033c51de338759fa479`.

Native preference persistence must follow the IFF contract in
`../ui/PREFERENCES.md`. An IFF container may contain authenticated ciphertext,
but this test uses upstream-compatible plaintext passphrase files and does
not implement encrypted IFF key storage or secure prompting.

The manager was rebuilt with accurate file-format help; this run tests its
authenticated backend through the separate native client, not manual GUI
passphrase interaction. Secure prompting, encryption creation controls and
the remaining encryption/replication surface are still unfinished.

### Guided encrypted creation UI (2026-10-02)

The Manager now offers an encrypted filesystem dialog in both views. It
collects a new dataset name, key-file format and authorized AROS file path;
it never collects the secret itself. Review uses the existing operation 37
validator, confirmation and authenticated worker, with encryption=on and
passphrase/raw/hex format. Request fields are not preference settings.

External evidence root: `/mnt/openzfs-artifacts`.
Build: `build-aros/encrypted-ui-20261002/OpenZFSManagerNative` and its input
hash manifest; build exit 0. In `build-aros/native-gui-run.yDWf8j0v`:

- `blank-rejected.png`: empty inputs rejected without submission.
- `review-attempt.png`: actual op37 confirmation with testpool/private,
  RAM:public-fixture.key, encryption=on,keyformat=passphrase, no force flags.
- `unavailable-2.png`: authenticated admission failure reported without
  automatic retry or false creation success. This fixture has no service.
- `save-confirm.png` and serial: Simple Save wrote both preference stores;
  earlier Advanced Use wrote ENV only. `archived-simple.png` shows the
  archived Simple fallback reopened after the fixture deleted ENV settings.

The runner reached its 1800-second limit before the final phase-8 close.
Its terminal result is exit 1, NOT a full preference-suite pass. The earlier
`native-gui-run.tUe0Lwpt` input attempt is also retained as incomplete.
This evidence verifies dialog wiring and selected persistence behavior, not
live encrypted creation through this new dialog. That integration test and
secure prompting remain outstanding; existing backend tests do not close it.

### Production dialog creation and cold handler read (2026-10-02)

`build-aros/startup-cold.ZG0OsZw7` under the external evidence root completed
both QEMU processes with runner exit 0. Production bundle `native-bundle.tVWAy5JT`
and Manager `encrypted-ui-20261002` were used; no engine test modes. Only the
real dialog submitted dataset creation. Independent shipped CLI checks proved
aes-256-gcm, passphrase format, prompt keylocation and the new encryption root.
The native handler wrote proof.txt, then unmounted and unloaded the key.
After reboot and read-only pool import, the verifier observed unavailable key
state and mount refusal, loaded the authorized key file through the public
authenticated API, mounted and checked exact proof.txt bytes, then unmounted,
unloaded and exported. Both original mirror disk hashes remained unchanged.
Screenshots `confirm.png` and `created.png`, both serial logs, binaries, input
hashes and the complete source patch are retained in that directory.

Failures retained before this pass:

- `startup-cold.Sh7a4n5E`: explicitly stopped before interaction because the
  minimal startup omitted the tablet USB-class scan. Fixture corrected.
- `startup-cold.WNJZt6e9`: create returned errno=45 with encryption disabled
  on the minimal baseline pool. The next fixture explicitly enabled encryption
  via shipped zpool on its disposable COW copy; the UI does not silently
  upgrade pools.
- `startup-cold.cLQbtW8N` and `startup-cold.AfjJekIl`: creation and properties
  passed but a redundant mount failed. Captured CLI diagnostics proved that
  upstream set-mountpoint had already mounted the filesystem; corrected the
  verifier to assert mounted=yes, not weaken or change the production path.

Enable with AROS_TEST_ENCRYPTED_CREATE_GUI=1, AROS_TEST_STARTUP_HANDLER=1,
AROS_TEST_MANAGER_BINARY and the normal production-bundle cold-test inputs.
Allow an interactive timeout. This closes passphrase-file dialog creation
and cold access for this fixture, not secure prompting, raw/hex dialog tests,
wrong-key/authorization cases for the new dialog, or physical durability.

### Three-format GUI matrix and incorrect-key rejection (2026-10-02)

The same two-boot production integration passed with runner exit 0 for:

- Raw 32-byte key: `startup-cold.Snojo1dc`.
- Hex 64-digit key: `startup-cold.77CE4AUG`.
- Passphrase file: `startup-cold.HsuC7K2P` (rebuilt Manager
  `encrypted-ui-help-20261002`, explicit pool encryption prerequisite shown).

All directories are under the external evidence root's build-aros directory.
Each cold phase additionally supplied a correctly formatted but incorrect
public fixture key: authenticated load returned EACCES (13), keystatus stayed
unavailable, then the correct key loaded and exact handler bytes were read.
The fixture creates no datasets itself; the dialog remains the only creator.
All original backing hashes remained unchanged. Inputs, screenshots, logs,
binaries and source patches are retained per run. Select formats with
AROS_TEST_ENCRYPTED_KEY_FORMAT=passphrase|raw|hex.

The normal UI does not enable pool features automatically. Its new help line
makes that prerequisite explicit; enabling encryption may change compatibility
and must remain an intentional administrator action. These results do not
establish secure prompting, privilege separation for every UI operation,
malformed key-file coverage, or physical durability.

### Rejected creation does not leave datasets (2026-10-02)

`startup-cold.JahTASbX` (external evidence root) completed both boots and
backing-hash checks with runner exit 0. Before launching the real Manager,
the fixture sent nine negative op37 requests through the production admission
endpoint: raw lengths 31/33, hex lengths 63/65, a non-hex digit, passphrase
lengths 0/7/513, and a missing file. Malformed inputs returned EINVAL; the
missing file returned ENOENT. After each, the shipped CLI explicitly reported
that qemu_mirror/rejected-key did not exist (not just an arbitrary nonzero exit).
The fixture then logged in as MuFS ozreader UID77/GID88, attempted creation
with a valid key path, observed EPERM, restored SYSTEM identity and proved
the target still absent.

The real Manager subsequently created the successful passphrase dataset in
the same service session. Native handler write, reboot, wrong-key rejection,
correct load and exact file read all passed. Negative requests use a distinct
target; the successful dataset still comes only from UI interaction.
Retained evidence includes both serial logs, confirm.png, created.png, tested
binary/source snapshot and manifests. A subsequent indentation-only warning
cleanup was separately compiled to OpenZFSStartupColdTest-warning-clean with
an empty build-warning-clean.log; that separate binary was not boot-tested.

These cases establish a useful rejection matrix, not exhaustive parser or
authorization coverage. Delegated create with denied key-file access, links,
concurrent replacement and direct secret prompting remain separate checks.

### Delegation does not grant key-file access (2026-10-02)

Production run `startup-cold.b4zdHn3f` under the external evidence root passed
both boots, backing-hash checks and terminal exit 0. With create,mount,keylocation
delegated to MuFS UID77/GID88, encrypted creation using the SYSTEM-owned key
returned EACCES and the separate control dataset remained absent. Changing
only the disposable key file owner to UID77/GID88 made the same API creation
succeed. SYSTEM then restored file ownership, destroyed that control dataset,
removed the three grants through zfs unallow and verified the dataset absent.
The primary cold-read dataset was still created only through the real Manager;
its write, cold locked-state and wrong-key rejection, correct load and exact
DOS read all passed afterward. No production authorization was weakened.

Retained failed predecessor `startup-cold.Y8HcwawY` granted create,mount but
omitted keylocation. Private-key access returned EACCES correctly; readable-key
creation then returned EPERM. Upstream crypto removes the creation-only crypto
properties but retains keylocation for zfs_set_prop_nvlist authorization.
Adding only its missing grant produced the passing control above. That failure
was a fixture permission assumption, not a repaired production defect.

This proves separation of dataset permission from native key-file ownership
for the tested SFS-backed path. The unallow command succeeded; a separate
post-revocation attempted create was not included. Link traversal, concurrent
replacement and secure prompting remain outstanding integration cases.

## Native CLI passphrase entry (2026-10-02)

The native CLI now uses a checked raw-console reader for upstream interactive
key entry. Current AROS termios handles ICANON but ignores ECHO; accepting a
successful tcsetattr alone was insufficient. The reader resolves the FILE's
actual DOS handle, requires an interactive console, checks SetMode, emits no
secret bytes, and restores the previous mode on return. Temporary buffers are
wiped before freeing on error or growth. Upstream confirmation and key
validation remain in libzfs. AROS salt generation uses checked getentropy()
instead of opening the Unix-only /dev/urandom path; unavailable entropy fails
closed. Non-AROS code paths are unchanged.

External evidence root: /mnt/openzfs-artifacts/build-aros.
CLI build cli-link.IEAkGRnK linked successfully (existing pthread macro
redefinition warnings remain). In startup-cold.ySU4mCtY, both entry prompts
were visually captured after typing the public fixture passphrase and before
Enter, with no characters echoed. The real zfs create returned zero; a separate
zfs get reported aes-256-gcm, passphrase, and prompt. Destroy then returned zero.
The same run continued through production Manager creation and native handler
write/unload/export. Both boots completed with runner exit zero; the cold phase
verified locked-state mount refusal, wrong-key EACCES, correct-key load, and
exact native-handler content. Original backing images remained hash-identical.
This is not yet a blanket secure-input qualification:
editing, cancellation, confirmation mismatch, mode restoration on failures,
oversized input, and non-console rejection need explicit runtime tests.

Retained failures: xvTxsORW lacked the fixture PIPE mount; ZLqr2ij2 used one
handle for SystemTags input and output contrary to its contract. pHeocjCc hid
both entries but failed at a combined assertion; axe8if6c separated diagnostics
and identified "Failed to generate salt" (command rc1). After native entropy
adaptation, qfzbRkAL command rc0 still failed the fixture's Close(CON) assertion.
console_handler final ACTION_END returns DOSFALSE,error0 on shutdown; the test
now records and distinguishes that pair from a real close error. These fixture
corrections are not production filesystem fixes. No physical-device claim is
made by these disposable QEMU runs.

The follow-up startup-cold.ahsKb8LA reused the same console handle for three
real zfs invocations: Escape cancellation, unequal confirmation, and successful
entry edited with Ctrl-U and Backspace. The first two returned one and left
the target absent; mismatch reported "Provided keys do not match." The edited
entry matched an independently typed confirmation and created the dataset.
After EACH invocation the original handle visibly echoed echo-check before
Enter and delivered its exact complete line to native Read(), proving cooked
input restoration across the tested error and success paths. Captures and
ui-actions.jsonl retain the public-only input sequence. This narrows the prompt
qualification gap; signal delivery, allocation/I/O failure injection, initial
raw-mode preservation, oversized input, and non-console rejection still need
coverage. The reader does not claim protection from privileged memory readers.
The full two-boot runner exited zero, including the subsequent production GUI
create and cold handler read; both original disk hashes remained unchanged.

Packaging verification: native-bundle.kfldcyIw combines cli-link.IEAkGRnK,
the encrypted-create Manager, production shared-engine.u5RwdrGc, and the
qualified device/handler. Archive SHA256 is
52fd55b7500185551363b98f46d3a9685a6cbace3015653d257ff8887246fc0a.
startup-cold.32ickY01 used that bundle without zfs or Manager overrides and
completed both boots with exit zero and unchanged original image hashes.
It additionally created an encrypted dataset using upstream redirected input
(keylocation=prompt with stdin from the public key fixture), queried its real
properties, destroyed it and verified absence. Hidden interactive cancellation,
mismatch, edited confirmation and restoration checks then passed again, followed
by Manager creation and cold wrong-key/correct-key native handler verification.
This verifies the packaged combination, not just a test-only replacement CLI.
stage-policy.zHJgCVmk passed all selected-input rejection cases with inputs
unchanged; policy-test artifacts now honor the external artifact root and
100 GiB storage reserve as the packager already did. The archive remains a
development bundle with the documented native platform prerequisites.

## Delegated key files through native links (2026-10-02)

startup-cold.1EFA6KSC exercises production API creation with UID77's
create,mount,keylocation grants and SFS-backed key files. Absolute and relative
soft links to a SYSTEM-owned private key returned EACCES, as did its direct
path. A self-referential link returned ELOOP (native errno 62). Changing only
the target key ownership to UID77 made direct and both linked-path creation
succeed; each successful test dataset was destroyed. Target ownership was
restored and delegation revoked before the normal Manager creation test.
This verifies target-file authorization after link expansion, not just a
blanket no-links policy. Concurrent path replacement remains untested here.

The retained predecessor startup-cold.lvRNovnH reached those expected API
outcomes but failed cleanup: DeleteFile(OZBOOT:key-absolute) returned false
with IoErr233, ERROR_IS_SOFT_LINK. The layer responsible for that native
SFS-backed deletion result is not established; no shared DOS/SFS fix was
attempted. The passing fixture deliberately retains all three links in its
disposable archive as evidence, separately from the removed ZFS test datasets.
The complete 1EFA6KSC runner exited zero, including Manager creation and a
fresh-guest locked-state/wrong-key/correct-key exact handler read. Both original
backing-image hashes remained unchanged. No physical durability is implied.

## Packaged upstream CLI passphrase rotation (2026-10-02)

startup-cold.BoF9gWCa completed two real zfs change-key calls with redirected
public passphrase fixtures. After each change, native unload followed by the
retired key returned EACCES and left keystatus unavailable; the active key
loaded successfully. The final keylocation remained prompt. Both boots passed,
but this first run wrote its proof file after the rotations, so it did not
establish retention of pre-existing file data.

The strengthened startup-cold.kAIYPjFF writes and Flush()es proof.txt through
the native handler BEFORE rotation, unmounts, rotates twice with the same
old-key refusal/new-key acceptance checks, then remounts and verifies the exact
original bytes and EOF without rewriting the file. Export and fresh QEMU boot
then verify locked mount refusal, wrong-key refusal, correct-key load, and the
same exact handler content. Runner exit zero and both original backing-image
hashes unchanged. The CLI/Manager/service are native-bundle.kfldcyIw inputs,
not per-command test replacements. This qualifies the upstream CLI salt/KDF/
key-change integration for the tested passphrase dataset, not raw/hex rotation,
encryption-root inheritance changes, multi-gigabyte files or power interruption.

Raw and hex rotation follow-up: startup-cold.vqNzg9Z0 (raw) and
startup-cold.imJOTm7n (hex) each completed both QEMU boots with exit zero.
The same packaged CLI inherited the existing keyformat for both change-key
calls. Each retired key failed with EACCES after unload, each active key loaded,
and both keyformat and keylocation remained correct. The native handler file
was written before rotation and matched exactly afterward and after cold boot,
including EOF. Both original backing-image hashes were unchanged in each run.
Manager draft/review/completion PNGs and serial/runner logs are retained under
the external artifact root. This extends the above rotation qualification to
all three supported key formats; it does not cover format conversion,
encryption-root inheritance changes, concurrent key replacement or power loss.

## Revocation enforcement (2026-10-02)

startup-cold.AFUCsMrH verifies more than the return status of zfs unallow.
After UID77 successfully created encrypted datasets using delegated
create,mount,keylocation rights, the packaged CLI revoked those rights. With
the key deliberately still owned by UID77, fresh requests by that same identity
through the direct path and both absolute/relative soft links returned EPERM.
The target dataset remained absent. Key readability therefore cannot explain
the refusal. The complete Manager creation, key rotations and cold handler
read subsequently passed; runner exit zero, original backing hashes unchanged.

The preceding unchanged-fixture run startup-cold.jksanGzy exited one before
reaching revocation: malformed-key creation returned expected EINVAL, but the
following C:zfs list emitted Error 333 and the absence assertion correctly
failed. That log is retained as another occurrence of the unresolved native
CLI launch issue, not attributed to revocation and not erased by the passing
replay. Neither run changes production authorization code.

## Child encryption-root transitions (2026-10-02)

startup-cold.av6iHZsK completed both native QEMU boots with passphrase keys.
The packaged zfs CLI creates a child inheriting the Manager-created parent's
encryption root. A file is written and flushed through OZCHILD: before any
key transition. change-key with an explicit keyformat and a different key
makes the child an independent root, verified by its encryptionroot property.
After child-key unload, the parent's key is rejected with EACCES and the
child remains locked; its independent key loads successfully. Real CLI
change-key -i then restores parent inheritance. The native file contents
and EOF match before export. After fresh boot and loading ONLY the parent's
key, the child mounts and returns the same exact contents and EOF.

Runner exit zero and both original backing hashes unchanged. Screenshots and
serial logs live in the external startup-cold.av6iHZsK directory. This proves
the tested passphrase child transition and persisted return to inheritance,
not arbitrary-depth trees, raw/hex child transitions, independent-child cold
boot, key-format conversion or physical power-loss recovery.

## Native management key inheritance (2026-10-02)

Operation 64 now accepts exactly options=inherit=on with an empty key-file
argument. It remains confirmed and credential-aware, requiring change-key
permission and a writable, nonsuspended pool. The engine calls upstream
DCP_CMD_INHERIT, not FORCE_INHERIT; upstream enforces independent-root/clone
eligibility and both loaded keys. No key file is opened for this form.
Manager exposes the option in operation help and gives a parent-key-specific
confirmation warning instead of the replacement-file warning.

Rebuilt shared-engine.sdooU0t1 and Manager were staged in
native-bundle.NoaKhUwp. startup-cold.vaZuvdlx uses this actual bundle and
tests operation 64 through native authenticated admission: locked child
returns EACCES without changing encryptionroot; loaded child succeeds;
repeating inheritance on the now-inherited child returns EINVAL. Exact
native child-file contents survive the transition and fresh boot with only
the parent's key loaded. Runner exit zero, original backing hashes unchanged.
This run exercises Manager creation and the native inheritance API, not a
manual click-through of Manager's inheritance confirmation dialog.

Retained pre-boot harness failures: startup-cold.zSnXRxfy rejected a nested
bundle outside its allowed root; startup-cold.5uiOOWP5 then exceeded the Unix
socket pathname limit. Moving the bundle to the normal external artifact root
resolved those setup issues without loosening validation or modifying AROS.
Build logs retain existing pthread macro and generated Resident initializer
warnings; no warning-free claim is made.

Manager click-through is now verified in startup-cold.A3Poxai9 using that
same bundle. AROS_TEST_KEY_INHERIT_GUI=1 pauses the native fixture with the
child independently keyed and both keys loaded, then opens the real Manager
instead of submitting the positive API request itself. Advanced > Encryption
> change-key, the child target, empty key-file field and inherit=on produced
the parent-key-specific warning. Submission returned errno=0. After closing
Manager, the fixture independently checked encryptionroot and exact native
file contents, then a fresh boot read the child using only the parent key.
Both boots exited zero and original backing hashes matched.

Captures inheritance-review-corrected.png and inheritance-completed.png
show the actual confirmation and backend result. Initial mouse clicks did
not focus the string fields; an empty-target attempt was correctly rejected.
Tab navigation entered the fields successfully. The cause of the mouse-focus
behavior is not established; this test does not claim it fixed. Input events,
the rejected attempt and all captures remain in the external run directory.

Native policy follow-up startup-cold.WjYKN1zi exited zero across both boots.
With both keys loaded and an independent child root, UID77 (without change-key
delegation) received EPERM for inherit=on; encryptionroot stayed unchanged.
The SYSTEM positive control then succeeded. After cold read-only import,
the same operation returned EROFS and preserved the inherited root. Exact
child-file reads and original backing hashes passed. These exercise the
new option's existing caller and pool policy boundaries, not only loaded-key
checks. No production policy change was necessary for these tests.

## CLI launch discriminator (2026-10-02)

The intermittent Error 333 remains unresolved. The cold-start runner now
accepts AROS_TEST_CLI_LAUNCH_CYCLES=1..1024 with a packaged bundle. It
alternates real zpool/zfs name-property reads, captures SystemTags return and
IoErr immediately, and requires exactly `qemu_mirror\n` plus EOF. It stops on
the first incorrect result rather than retrying. Encryption command logging
also records the immediate return/error pair before Flush/Close can alter it.

Private startup-cold.h0I7XFHY passed 128 launches with the unchanged original
loader-overlay.L7ctzwkQ and native-bundle.NoaKhUwp, then completed both boots
and original backing-file hash checks with runner exit zero. Every launch
returned zero, IoErr zero and exact output. This is a read-only command-launch
baseline, not reproduction or resolution of the property-transition and
malformed-key failure contexts. Log: external cli-launch-stress-20261002.log.

The longer startup-cold.XSJ5Mk5R run also completed with exit zero: all
1,024 alternating reads had return/error zero and exact output, followed by
the fresh-boot handler checks and unchanged original disk hashes. Log:
external cli-launch-stress1024-20261002.log. Repeated read-only CLI launches
alone have therefore not reproduced the failure; the next discriminator
must retain the negative-operation or property-transition context rather
than treating another successful launch-only run as a fix.

AROS_TEST_MALFORMED_KEY_CYCLES=1..128 now repeats the actual nine negative
creation cases before the normal encrypted GUI sequence. The 64-cycle run
startup-cold.0FgMY5vI passed all 576 rejected-request/native CLI absence
pairs, with no retries. The CLI reported the expected return 1 / IoErr 209
and dataset-not-found text, not Error 333. The same live service then
accepted real Manager encrypted creation (captured in
malformed-create-review.png and malformed-create-result.png), passed key
rotation, delegation/revocation and inheritance checks, and read exact parent
and child contents after a fresh boot. Runner exit zero; original backing
hashes unchanged. Log: external cli-malformed-context64-20261002.log.
This exercises the earlier negative-request failure context without
reproducing it; it is not a fix or a dismissal of that retained failure.
