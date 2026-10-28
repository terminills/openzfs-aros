# Reproducible AROS runtime candidates

`build-stdcio-snapshot.py` builds private stdcio or stdc candidates from the
selected AROS source tree and generated link map. It never installs them.
The generated inputs and SDK must belong to the intended ABI. Retained hashes
detect drift, not malicious source or recipes; review inputs before executing.

For stdc:

```sh
python3 aros/engine/build-stdcio-snapshot.py --library stdc \
  --upstream /path/to/aros-source \
  --generated /path/to/gen/compiler/crt/stdc/stdc \
  --asm-header /path/to/gen/include/aros/x86_64/asm.h \
  --sdk /path/to/AROS/Developer --cc /path/to/x86_64-aros-gcc \
  --output-parent /path/to/private/external/builds
```

For stdcio, omit `--library` (or use `--library stdcio`), select the generated
`stdcio` directory, and supply `--stdio-source /path/to/reviewed/__stdio.c`
instead of `--asm-header`. The explicit source override carries the reviewed
inherited-stream flush correction; it must match the qualified runtime.

Each build requires at least 100 GiB free reserve and uses one compiler process
at a time. It retains C/math/assembly sources, generated headers and locale
data, license files, build commands, dependency hashes, logs and the result.
Link archives and SDK headers remain external dependencies. The builder rejects
external cached objects or dependencies on unretained source/generated trees.

Replay a trusted snapshot in a new directory:

```sh
python3 aros/engine/rebuild-fat-snapshot.py /path/to/snapshot \
  --output-parent /path/to/private/external/builds
```

The helper's historical filename mentions FAT, but the recorded artifact can
be a runtime library. Replay verifies hashes and requires byte-identical output.
Run isolated writer/cold-reader and Manager tests with the rebuilt runtime set,
then obtain native acceptance before packaging. A build/replay pass is not a
runtime pass or general stock-nightly compatibility claim.
