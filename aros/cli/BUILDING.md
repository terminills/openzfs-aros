# Native upstream CLI source build

The command build can use a fresh common/dependency compile instead of keeping
the original compile-probe directories. This is not yet a clean-checkout,
dependency-bootstrap build or a production AROS build-system target.

From the repository root, with an ABI-matched SDK, compiler and private pthread
build selected:

```sh
export AROS_CC=/absolute/path/to/x86_64-aros-gcc
export AROS_SDK=/absolute/path/to/AROS/Developer
export AROS_PTHREAD_BUILD=/absolute/path/to/private-pthread-build
export AROS_PTHREAD_INCLUDE="$AROS_PTHREAD_BUILD/source"
export AROS_NVPAIR_ARCHIVE=/absolute/path/to/libnvpair.a
export AROS_CRYPTO_INCLUDE=/absolute/path/to/boringssl/include
export AROS_CRYPTO_ARCHIVE=/absolute/path/to/libboringssl.a
AROS_TEST_BUILD_INIT=1 bash aros/cli/probe-upstream.sh --all
```

Use the reported `Evidence:` directory as both first and second arguments:

```sh
bash aros/cli/probe-link.sh /absolute/path/to/fresh-cli-probe \
    /absolute/path/to/fresh-cli-probe /absolute/path/to/native-pipe-build
```

The third argument supplies `stubs/posixc_pipe2_stub.c` and `posixc.library`.
It is a runtime-ABI dependency, not the experimental reader-close policy
handler. The latter remains a separate QEMU test overlay. Matching private
pthread headers and archive must be used throughout; a successful link does
not establish ABI compatibility.

`probe-link.sh` now compiles `lib/libnvpair/libnvpair.c` directly alongside
the native adapters. It no longer reads the old
`full-native-libzfs-init/client/lib_libnvpair_libnvpair.c.o` fixture. It also
recompiles the current upstream libzfs_core wrapper and requires exactly one
replacement of that common object. Common command/library units come from
the explicitly supplied probe. The linker records source/object/archive
hashes, including thin archive members.

Remaining build prerequisites include generated `build-aros/include` headers,
the pinned extracted libtirpc headers, nvpair/pthread/crypto archives, and the
native pipe/runtime overlays. They are not bootstrapped by these two commands.
The service/engine has its own build and packaging requirements. Do not call
this a standalone source release, shared-engine build, or stock-nightly port.

## Fresh compile evidence, 2026-09-28

`cli-probe.Vq4XLj6U` compiled all 25 common upstream units plus the native init
dependency objects, exit 0. `cli-link.L12HV1ow` used that one fresh directory
for both inputs and linked zfs, zpool, OpenZFSStartupMount and OpenZFSStartup,
exit 0. Compiler warnings remain in `cli-fresh-source-1.log`; this is not a
warning-free build claim. Link output is `cli-fresh-link-1.log`.

`native-bundle.Dsq7Qdkn` stages those commands, the existing verified
pipeline-query.ZvtOS0wZ service and its Manager. Its manifest passes and the
archive is 12,707,512 bytes. Staging alone is not runtime verification.

The staged binaries subsequently passed the complete native-import regression
in `native-import-run.bM2HeoLi`, runner exit 0, with both original backing
hashes unchanged. This includes actual CLI plain/raw resumption, eight reused
concurrent pipelines, reader-close failure delivery under the private handler
policy, native DOS I/O, authorization and export/reimport. Evidence is
registered under the development test archive; log: `cli-fresh-bundle-native-1.log`. Manager was
packaged but not visually exercised. The run still requires the private OS
overlays; it is not stock-nightly or standalone dependency-bootstrap proof.
