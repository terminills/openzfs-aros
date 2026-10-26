# Normal full engine only. Diagnostic variants remain in the test builders.
OZE_ROOT := $(abspath $(SRCDIR)/$(CURDIR)/../..)
OZE_OUT := $(GENDIR)/$(CURDIR)/full
OZE_INVENTORIES := $(addprefix $(OZE_ROOT)/,lib/libzpool/Makefile.am lib/libicp/Makefile.am lib/libavl/Makefile.am lib/libunicode/Makefile.am lib/libzstd/Makefile.am)
OPENZFS_CRYPTO_INCLUDE ?= $(AROS_INCLUDES)
OPENZFS_PYTHON ?= python3
OZE_UPSTREAM := $(shell awk '/^[[:space:]]+[[:alnum:]_%/].*\.c([[:space:]]|$$)/ {gsub(/%D%/,"lib/libzpool",$$1);print $$1}' $(OZE_ROOT)/lib/libzpool/Makefile.am)
OZE_CRYPTO := $(shell awk '/^[[:space:]]+module\/.*\.(c|S)([[:space:]]|$$)/ {print $$1}' $(addprefix $(OZE_ROOT)/,lib/libicp/Makefile.am lib/libavl/Makefile.am lib/libunicode/Makefile.am lib/libzstd/Makefile.am) | awk '!/asm-(aarch64|arm|ppc64)\//')
OZE_ZPL_UPSTREAM := module/zfs/zfs_ioctl.c module/zfs/zfs_onexit.c module/zfs/zvol.c \
    module/zfs/fm.c module/zfs/zfs_fm.c module/zfs/spa_errlog.c module/zfs/zfs_fuid.c \
    module/zfs/dmu_direct.c module/zfs/dmu.c module/zfs/sa.c module/zfs/zfs_sa.c \
    module/zfs/zfs_znode.c module/zfs/zfs_byteswap.c module/zfs/zfs_log.c \
    module/zfs/zfs_vnops.c module/zfs/zfs_quota.c module/zfs/zfs_replay.c
OZE_NATIVE := aros/ui/service.c aros/ui/api.c aros/ui/broker.c \
    aros/engine/zpl-fixture.c aros/engine/dos-handler.c aros/engine/properties.c aros/engine/management.c \
    aros/cli/device-path.c lib/libefi/rdwr_efi.c aros/engine/efi-native.c aros/engine/efi-uuid.c aros/engine/efi-label.c \
    aros/zpl/default-quota.c aros/zpl/temporary-hold.c aros/zpl/set-version.c \
    aros/engine/caller.c aros/engine/file-scope.c aros/platform/cpu.c aros/platform/partitions.c \
    aros/platform/storage-inventory.c aros/engine/partition-io.c \
    lib/libzutil/zutil_import.c lib/libtpool/thread_pool.c aros/engine/import-platform.c aros/cli/import-files.c \
    aros/engine/replication.c aros/engine/stream-file.c aros/engine/stream-output.c \
    aros/engine/status.c aros/engine/scan.c aros/engine/pool-create.c aros/engine/pool-import.c \
    aros/engine/pool-split.c aros/engine/pool-clear.c aros/engine/pool-initialize.c aros/engine/pool-trim.c \
    aros/engine/split-config.c aros/engine/history.c aros/zpl/create-fs.c aros/engine/admission.c \
    aros/engine/control-file.c aros/engine/ioctl-platform.c aros/engine/zvol-native.c \
    aros/engine/ioctl-request.c aros/cli/stream-channel.c aros/cli/session-cleanup.c \
    aros/engine/mount-registry-allocation.c aros/engine/cache-policy.c aros/engine/hostid-policy.c \
    aros/engine/hostid-record.c aros/engine/passphrase.c aros/engine/ioctl-copy.c \
    lib/libspl/atomic.c lib/libspl/getexecname.c lib/libspl/list.c lib/libspl/page.c \
    lib/libspl/strlcat.c lib/libspl/strlcpy.c lib/libspl/tunables.c \
    lib/libzutil/zutil_device_path.c lib/libzutil/zutil_pool.c lib/libzutil/zutil_nicenum.c \
    aros/zpl/setattr.c aros/zpl/rename.c aros/zpl/remove.c aros/zpl/rmdir.c aros/zpl/unlinked.c \
    aros/zpl/xattr-dir.c aros/zpl/create.c aros/zpl/symlink.c aros/zpl/mkdir.c aros/zpl/mknode.c \
    aros/zpl/dir-mutation.c aros/zpl/dirlock.c aros/zpl/space.c aros/zpl/write-simple.c \
    aros/zpl/mapping-policy.c aros/zpl/dataset-stats.c aros/zpl/acl-core.c \
    aros/zpl/acl-core-test.c aros/zpl/acl-storage-test.c aros/zpl/native-protection-test.c \
    aros/zpl/native-protection.c aros/zpl/xattr.c aros/zpl/xvattr.c aros/zpl/znode-cache.c \
    aros/zpl/znode-cache-test.c aros/zpl/read-mount.c aros/zpl/legacy-znode-test.c \
    aros/zpl/online-receive.c aros/engine/packet-cred.c aros/engine/vdev-policy.c \
    aros/engine/spl-platform.c aros/engine/device-io.c aros/engine/uio.c aros/engine/abd-pages.c \
    aros/engine/layout-check.c aros/zpl/layout-test.c aros/engine/cred.c aros/engine/idmap.c \
    aros/zpl/timestamps.c aros/zpl/mount-state.c aros/zpl/blocksize.c aros/zpl/file-limits.c \
    aros/zpl/file-limits-test.c
OZE_SOURCES := $(sort $(OZE_UPSTREAM) $(OZE_CRYPTO) $(OZE_ZPL_UPSTREAM) $(OZE_NATIVE))
OZE_OBJECTS := $(addprefix $(OZE_OUT)/objects/,$(addsuffix .o,$(OZE_SOURCES)))
$(OZE_OBJECTS): $(OZE_ROOT)/aros/engine/build-system.mk $(OZE_ROOT)/aros/engine/mmakefile.src
OZE_ZPL := $(filter aros/zpl/%,$(OZE_NATIVE)) $(OZE_ZPL_UPSTREAM) \
    aros/cli/session-cleanup.c aros/engine/control-file.c aros/engine/ioctl-platform.c \
    aros/engine/zvol-native.c aros/engine/ioctl-request.c
OZE_FLAGS := $(CFLAGS) -O2 -g -fno-strict-aliasing -ffunction-sections -fdata-sections \
    -Wno-sign-compare -Wno-missing-field-initializers -Wno-unused-parameter \
    -DLIB_ZPOOL_BUILD -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -DOZ_NATIVE_DISPATCHER -DOZ_NATIVE_ONEXIT \
    -include sys/sysmacros.h \
    -I$(OPENZFS_PTHREAD_INCLUDE) -I$(OZE_OUT)/include \
    -I$(OZE_ROOT)/aros/engine/include \
    -I$(OZE_ROOT)/lib/libspl/include -I$(OZE_ROOT)/lib/libspl/include/os/linux \
    -I$(OZE_ROOT)/include \
    -I$(OZE_ROOT)/include/os/linux/zfs -I$(OZE_ROOT)/module/icp/include \
    -I$(OPENZFS_TIRPC_SOURCE)/tirpc

$(addprefix $(OZE_OUT)/objects/,$(addsuffix .o,$(OZE_ZPL))): OZE_EXTRA += -I$(OZE_ROOT)/aros/zpl/include -include $(OZE_ROOT)/aros/zpl/context.h
$(OZE_OUT)/objects/module/zstd/%.o: OZE_EXTRA += -include $(OZE_ROOT)/module/zstd/include/zstd_compat_wrapper.h -fno-tree-vectorize
$(OZE_OUT)/objects/lib/libzutil/zutil_import.c.o: OZE_EXTRA += -I$(OZE_ROOT)/aros/engine/import-include -include $(OZE_ROOT)/aros/engine/import-include/largefile.h -DTEXT_DOMAIN=\"zfs\"
$(OZE_OUT)/objects/lib/libefi/rdwr_efi.c.o $(OZE_OUT)/objects/aros/engine/efi-label.c.o: OZE_EXTRA += -I$(OZE_ROOT)/aros/cli/include
$(OZE_OUT)/objects/aros/engine/admission.c.o: OZE_EXTRA += -include $(OZE_OUT)/include/engine-metadata.h

$(OZE_OUT)/objects/aros/engine/passphrase.c.o: $(OZE_ROOT)/aros/engine/passphrase.c
	@$(MKDIR) $(dir $@)
	$(AROS_CC) $(TARGET_SYSROOT) $(CFLAGS) -O2 -g -I$(OPENZFS_CRYPTO_INCLUDE) -MMD -MP -MF $@.d -c $< -o $@

$(OZE_OUT)/objects/%.o: $(OZE_ROOT)/% $(OZE_OUT)/include/zfs_gitrev.h $(OZE_OUT)/include/engine-metadata.h
	@$(MKDIR) $(dir $@)
	$(AROS_CC) $(TARGET_SYSROOT) -include $(OZE_ROOT)/aros/engine/config.h $(OZE_EXTRA) $(OZE_FLAGS) -MMD -MP -MF $@.d -c $< -o $@

$(OZE_OUT)/include/engine-metadata.h: $(OZE_ROOT)/META $(OZE_ROOT)/aros/cli/generate-metadata.py
	@$(MKDIR) $(dir $@)
	$(OPENZFS_PYTHON) $(OZE_ROOT)/aros/cli/generate-metadata.py $(OZE_ROOT)/META $@

# Recheck HEAD and tracked dirty state on incremental builds. Unchanged
# content is not rewritten by make_gitrev.sh, avoiding needless rebuilds.
.PHONY: openzfs-engine-gitrev-check
openzfs-engine-gitrev-check:

$(OZE_OUT)/include/zfs_gitrev.h: $(OZE_ROOT)/META $(OZE_ROOT)/scripts/make_gitrev.sh openzfs-engine-gitrev-check
	@$(MKDIR) $(dir $@)
	cd $(OZE_OUT) && bash $(OZE_ROOT)/scripts/make_gitrev.sh include/zfs_gitrev.h

$(OZE_OUT)/full-engine.o: $(OZE_OBJECTS) $(OZE_INVENTORIES) $(OZE_ROOT)/aros/engine/tunables.ld
	$(AROS_LD) -r -T $(OZE_ROOT)/aros/engine/tunables.ld $(OZE_OBJECTS) -o $@
	sha256sum $@ >$(OZE_OUT)/engine-object.sha256
	printf 'service_test_modes=0\nfull_dispatcher=1\nstatus_trace=0\n' >$(OZE_OUT)/service-build-options.txt

.PHONY: contrib-openzfs-engine
contrib-openzfs-engine: $(OZE_OUT)/full-engine.o
	cp $(OZE_OUT)/include/engine-metadata.h $(OZE_OUT)/engine-metadata.h
	sha256sum $(OZE_ROOT)/META $(OZE_ROOT)/aros/cli/generate-metadata.py $(OZE_OUT)/engine-metadata.h >$(OZE_OUT)/engine-metadata.sha256
	printf '%s\n' $(OZE_SOURCES) >$(OZE_OUT)/sources.txt
	cd $(OZE_ROOT) && sha256sum $(OZE_SOURCES) >$(OZE_OUT)/sources.sha256
-include $(addsuffix .d,$(OZE_OBJECTS))
