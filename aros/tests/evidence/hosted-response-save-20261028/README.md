# Native response destination selection

The Manager now uses a native ASL file requester rather than always replacing
`T:OpenZFS-Response.txt`. It saves the current backend response/page, including
details hidden by Simple mode. This does not export separate large command
reports or concatenate all pages. Existing destinations are refused using
`O_CREAT | O_EXCL`. Partial new files are retained on write/close errors.

## Passing isolated hosted run

- Runtime: `build-audit/hosted-security-test.Ij0st8yq`.
- Recipe inputs: `build-audit/hosted-atime-inputs.C0BxNKB9`.
- Fresh generated MetaMake build: `build-audit/mmake-cli.yq57XdhA`.
- `01-save.png`: save to `SYS:OpenZFS-Response.txt` succeeded.
- `02-existing.png`: a second save to that path was refused. SHA-256 before
  and after: `3ade721b2025ae3962278ab903ad935b7e0359ff4a0cd009925fb129d6df5190`.
- `03-cancel.png`: cancellation reported no write; subsequent UI interaction
  remained usable.
- `05-snapshot-confirm.png` and `06-snapshot-result.png`: authenticated native
  snapshot creation succeeded. Cold import independently found the snapshot.
- Writer and cold runtime prerequisites passed, including redirected output,
  ExecHelper, and loader execute protection. The full fixture passed cold
  read-only, missing-member and restored-member checks. Host zdb checks passed.

The saved 2381-byte file is the backend capabilities reply, not the shorter
Simple-mode connection summary. `saved-response.txt` preserves those bytes.
The Linux hosted backing file mode is not proof of AROS security enforcement.

## Host helper tests

Run `bash aros/ui/test-report-save.sh`. Tests cover exact bytes, existing-file
and symlink refusal, empty/invalid inputs, EINTR retry, short writes, ENOSPC,
zero-length write, close failure and retained incomplete outputs. The 0600 mode
assertion applies to the host test only. No hardware durability claim is made.

## Retained incomplete attempts

- `eaVsnCJc`: requester opened; malformed automated filename caused ENOENT.
  The 900-second GUI deadline expired before the fixture completed.
- `KiO2RJwx`: save succeeded with the same response hash, but repeat interaction
  was not verified before the deadline. This is not a passing storage run.
- Final code corrects the help text and explicitly sleeps the application
  around the requester, associating it with the Manager window. The final run
  verifies repeated use. It does not establish a unique cause for every input
  failure in the earlier automation.

These are isolated hosted results, not QEMU or physical-DUT acceptance. No
other hosted session or physical device was used.
