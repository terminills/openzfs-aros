# Source-built stdcio qualification

Candidate `stdcio-source.gphvn1pg` compiles all 69 objects named by the
selected hosted stdcio link map. It reuses no cached object. It retains the
selected stdio flag-restoration source, other source files, generated ABI
headers, licensing, source provenance and a replay recipe. SDK archives,
SDK/toolchain headers and compiler remain hash-bound external inputs;
this is not whole-toolchain source closure or proof of an official SDK ABI.

Replay `fat-replay.rfs5yv3h` rebuilt from retained sources in a new directory
and produced byte-identical stdcio.library:
`a1265379f3ae9a1004c3c75bdc7a266c91fb09162acc215086e14b0b3aeb7d48`.
The original source checkout and cached module objects are not replay inputs.

Hosted `IPnpxDDM` installs this exact library into a fresh private runtime.
Both writer and cold preflights pass: errno, configured security, explicit
flush/normal-return/exit stream teardown, actual ExecHelper subprocess and
LoadSeg/NewLoadSeg on/off/on. Full persistent storage/CLI/handler checks,
independent host zdb No leaks, cold exact/degraded reads and unchanged member
hashes pass. All runtime inputs are hashed and checked after execution.

Companion runtime still uses matched SDK stdc, j099bkwp POSIX flush variant,
and 4CUMjP0r hosted loader-policy DOS overlay. No QEMU or physical DUT claim.
The retained manifest and provenance identify all source/build inputs.

Build interface:

```
python3 aros/engine/build-stdcio-snapshot.py \
  --upstream /path/to/aros-source \
  --generated /path/to/gen/compiler/crt/stdc/stdcio \
  --sdk /path/to/AROS/Developer --cc /path/to/x86_64-aros-gcc \
  --stdio-source /path/to/reviewed/__stdio.c \
  --output-parent /private/artifacts
python3 aros/engine/rebuild-fat-snapshot.py /private/artifacts/stdcio-source.ID \
  --output-parent /private/artifacts
```

Generated ABI inputs must match the intended runtime. A successful rebuild
alone is not permission to replace system libraries; qualify the candidate
in an isolated runtime first.
