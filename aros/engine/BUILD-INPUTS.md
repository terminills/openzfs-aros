# Engine build-input manifest

The `contrib-openzfs-engine` target records `compiler-inputs.json` alongside
`full-engine.o`. Compiler dependency files include system headers (`-MD`), not
just project headers. Recording requires the exact object-path inventory from
`sources.txt`; an equal number of stale dependency files is not sufficient.

The manifest hashes compiler-discovered inputs, dependency files, each object,
the linked engine object, and the build fragments/upstream source inventories.
Dependency-file text is parsed as data, never evaluated as shell or Make code.
Header symlink paths are retained so a later retarget to different bytes fails
verification.

To check a retained build:

```sh
python3 aros/engine/record-build-inputs.py --verify /path/to/full/compiler-inputs.json
python3 aros/engine/test-record-build-inputs.py
```

This is a **post-build drift check**, not proof that files were immutable during
compilation. It does not replace a clean build, compiler/toolchain provenance,
dependency source packaging, or runtime verification. Paths are absolute and
describe the local qualification environment; the manifest is not a relocatable
source release. Source files and headers are retained separately from evidence.
