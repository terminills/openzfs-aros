# Current native combined suite: failure localization

`public-service-run.TWxWdCyM` failed before boot. The new actual-ISO runtime
check found a retained source GPT/APM disk layout whose endpoint exceeded
the smaller rebuilt image. xorriso extracted POSIX but exited 32 with SORRY.
The public-service test generator now clears the inherited system area and
creates its explicit GRUB El Torito entry. This private QEMU CD is not a USB
or UEFI installer qualification.

`public-service-run.z7H7Yy2O` subsequently extracted all five selected runtime
libraries without the disk-layout error and booted the guest successfully.
It failed the persistent configuration test's DOS-only rename collision probe:
Rename returned success but the destination could not be opened (DOS 205).
The noncolliding control succeeded. The recipe had omitted the known fixed
AFS handler, so the stock ISO handler was used for OZARCH. No
`afs-handler.sha256` was present in this run. This matches the existing
AFS pinned-link issue; the test was not weakened.

The selected follow-up handler is `afs-source.wg6ei6nj/afs-handler`.
The source-snapshot verifier accepts its source/patch/build/binary bindings.
The private run recipe now explicitly selects it, in addition to the current
shared engine, client, CLI/Manager and four CRT dependencies. A passing
follow-up native run is still required. These failures do not qualify a bundle.

Both failed images are eligible for verified zstd compaction after process
exit; all logs and mutable test pool/archive files remain retained.

## Fixed-handler native writer/cold/release pass

`public-service-run.rSDRJJ6u` completed with runner exit zero using the selected
AFS source snapshot and current service/CLI/client/CRT package. Both collision
and noncollision rename controls now read the exact destination data.
Persistent MuFS archive settings, IFF host identity, trusted preparation and
disabled-record restore passed. The writer performed authenticated mirror
creation, native DOS writes/reads, mount controls and readonly reimport.

Fresh cold boot passed healthy and degraded reads, readonly write refusal,
beyond-parity refusal with a responsive service, and restored-member recovery.
Pool members remained byte-identical during readonly validation. A further
writable cold release persisted deletion and passed independent host zdb.
The raw transcripts, hashes and host integrity logs are under `cold-pass/`.

The successful runner's existing post-verification cleanup removed 11
generated images/member copies (15.18 GiB allocated); `image-pruning.log`
records this. These disposable images are not recoverable from that run;
logs, source recipe, hashes and result evidence are retained. This is not
physical-device durability evidence.

The initial writer ISO had the cleared system area. Follow-on cold/release
ISO builders still inherited the old area in this run, although both booted
and completed. The same explicit El Torito-only generation is now applied
to every follow-on ISO builder; future runs must validate that change.
The encrypted-root attempt after this pass was refused before any preparation
or launch because another agent's QEMU appeared. No competing VM was started.

## Encrypted native cold writer and resumed reader

`TFkyLdzR` stopped during preparation because the partition-test SDK fallback
pointed at an obsolete `/opt` location. The harness now requires an explicit
SDK for this mode; the private recipe selects the matching current SDK.
No guest ran in that failed preparation.

`XMxcRVST` passed the encrypted native writer, including locked import,
wrong-key rejection, key reload and exact data. QEMU then refused to start
the cold reader because the run path plus `native-cold-monitor` exceeded the
Unix socket path limit. The error is retained in `encrypted-cold/`.
The runner now uses the shorter `n-monitor` basename for its sequential native
cold phases. No unrelated process or socket is used.

The retained cold ISO and writer disk were resumed without rebuilding either.
The pre-cold disk hash was checked before launch. The reader passed locked
mount refusal, key loading, exact reads, readonly write denial and export;
the complete pool image hash was unchanged afterward. All five cold ISO
runtime files were extracted and compared with the verified writer runtime.
The private resume script and transcripts are retained here.

This is a verified encrypted cold-read result, not a complete release gate:
the separate native discovery/corrupt/conflicting-label scenarios are not
executed by NATIVE_COLD mode and remain required. The original full harness
exited nonzero at launch; the explicit resumed reader exited zero. Neither
result is relabeled as a complete combined acceptance run.

## Per-phase image verification

`verify-iso-runtime.sh` now supplies the image check for every writer, cold,
release, and native recovery launch in the public-service harness. Each phase
extracts its own libraries into a new evidence directory and records hashes;
it does not infer cold-image contents from the writer manifest. The verifier
rejects existing output, duplicate selected basenames, empty selection, absent
guest files, and content mismatch. xorriso errors remain fatal.

Four host-only tests passed using small generated ISO fixtures. The same helper
also passed against the retained real `XMxcRVST/native-cold.iso`, binding its
five selected libraries in `iso-phase-check.OxjiqlFm`. Its manifest is retained
here as `per-phase-cold-runtime.sha256`. This is an image-content check, not an
additional native boot. The next native suites will exercise the integrated
per-phase call sites.

## Encrypted release evidence and retained storage

The encrypted-root staging gate now explicitly requires the native wrong-key
marker: two denials, both pools remaining locked, both mount denials, and two
successful recoveries with the correct key. The runner already required this;
the stager previously checked key reload without independently requiring the
negative-key result. Tests now reject a missing marker, incomplete recovery,
and a pool that did not remain locked. All 20 staging tests pass. This does
not substitute the retained cold-only run for the full discovery suite.

Completed debugger ISOs `LuO2nC45`, `EsMKxiba`, and `CaLV7RkO` were compacted
after their terminal results and inactive-use checks. Decompressed SHA-256
matched before unlinking each uncompressed ISO. About 11.7 GiB was reclaimed;
`test.iso.zst`, pool images and logs remain. Updated archive/original manifests
are retained alongside this note. This preserves MAME's next QEMU turn and
restores storage headroom without deleting another agent's artifacts.
