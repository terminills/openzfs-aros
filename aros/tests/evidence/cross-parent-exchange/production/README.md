# Production service regression

Normal incremental build `sibling-production.liEWQyre` and shared engine
`shared-engine.n9wYTwcQ` contain c6e8ce4. The production verifier accepted
full dispatcher, disabled test modes and disabled status tracing. Upstream
objects were seeded from the retained source build; native adapters and ZPL
were recompiled. This is not clean-source closure of the entire OS/toolchain.

Eight-vCPU QEMU `public-service-run.T0qVsQpo` exited zero for public CLI,
handler, service lifecycle and encrypted native-device cold-import tests.
The fresh reader required a key, returned exact data, rejected writes and
left the backing image unchanged. Other disk hash checks also passed.
The runner pruned its two disposable ISOs after success, retaining manifests,
logs and backing files. All run/build artifacts are under
`/mnt/short-term/openzfs-qualification-20261024.aGFlVF7T`.

This verifies production integration, not an exchange API exposed through
DOS or the Manager. Direct cross-parent exchange coverage is in the parent
directory. Those tests restore exchanged names before cold import, so a
separate non-restored exchange persistence fixture remains to be added.
