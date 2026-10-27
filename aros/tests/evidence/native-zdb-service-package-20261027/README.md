# Debugger-enabled service regression and legacy bundle assembly

This continues native debugger qualification after commits 3e3452e and 772f89b.
The exact generated MetaMake binaries from `mmake-debugger.JLOOcjrH` were copied
to private `debugger-service.0umlrjD3`. No engine was replaced in a running
guest. A service lifecycle test was freshly cross-built with matching generated
headers; production engine flags and object metadata came from its actual
`mmake-engine.hZ1s42So` input. Binary identities and recipes are retained here.

## Native service regression

`public-service-run.9mcAdUIz` completed eight-vCPU persistent QEMU writer,
cold-read and writable-release boots with the debugger-enabled shared library.
The runner verified public service/library lifecycles, mirror DOS round trip,
fresh healthy/degraded imports, exact file data, unchanged cold pool members,
persisted release deletion, and independent host zdb accounting. Complete
writer/cold/release serial reports, manifests, integrity logs and runner are in
`service/`. The original older run's acceptance client, CLI and handler are
explicit inputs: this is a regression of the selected engine, not a rebuild
or renewed qualification of all frontend programs or every public API.

The runner pruned ten completed disposable images, reclaiming 15.17 GiB;
`pruned-images.jsonl` retains their identities. No DUT, user hosted session,
physical media, network or filesystem share was used.

## Legacy packaging

The real legacy packager now requires shared debugger vector 6, the thin native
launcher, and the same runtime-evidence gate used by the MetaMake packager.
It copies C/zdb and retains native debugger evidence. Negative policy tests
continue to reject diagnostic/incomplete engines, bad service/library hashes,
and invalid Manager provenance. Four new cases reject missing debugger run,
missing launcher, changed launcher bytes and incomplete role evidence before
any bundle is produced. Selected source inputs remain unchanged.

`native-bundle.BgkYrpNE` is a development assembly test using the actual
`debugger-service.0umlrjD3`, `cli-link.dr92BqmY`, and the existing
`source-engine.sa4fWOFY` Manager. It intentionally does not select a handler or
device. The compressed archive was freshly extracted; every SHA256SUMS entry
passed, and C/zdb plus Libs/openzfs.library matched the tested source binaries
byte for byte. Its archive hash and verification output are in `package/`.

This is not a complete end-user filesystem bundle or public release. The legacy
CLI selection lacks the current ExecHelper layout, and this test does not
qualify its Manager/CLI alongside the new engine. Complete MetaMake assembly
still needs matched GUI/cold/encrypted-root/companion and source-closure proof.
The selected public SDK/runtime limitation and physical durability gates remain.
