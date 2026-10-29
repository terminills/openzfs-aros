# Fresh handler compile, not runtime qualification

External build `handler-build.4DtJL9h3` completed with exit 0. Its
`handler.o` has no undefined symbols (`nm -u` empty). Generated startup code
emits existing missing-initializer, unused-parameter and pointer-sign warnings.
The final binary hash differs from the retained QEMU-qualified handler, so it
has not been substituted into the qualified bundle or claimed runtime-tested.

An initial invocation from the short-term checkout was refused by the 100-GiB
storage guard before building. The successful invocation used the independent
external source checkout; handler.c and build.sh were byte-compared first.
No storage guard was bypassed. Command from that external source directory:

```sh
AROS_ARTIFACT_ROOT=/run/media/terminills/Amiwest/openzfs-source-replay-20261028/build \
AROS_CC=/home/terminills/Documents/GitHub/aros-adt-crosstools/x86_64-aros-gcc \
AROS_SDK=/home/terminills/Documents/GitHub/aros-adt-pc-x86_64-clean/bin/pc-x86_64-smp/AROS/Developer \
AROS_GENMODULE=/mnt/short-term/openzfs-bootenv-candidate-20261025/build-audit/shared-tools.7guw1X1e/genmodule \
bash aros/handler/build.sh
```

Current source/build identities and the new binary hash are retained here.
Next steps are binary-difference inspection and owned native runtime testing.
