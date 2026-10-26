# Included by the repository mmakefile.src. Paths belong to the configured build.
OZ_SOURCE := $(SRCDIR)/$(CURDIR)
OZ_CLI_OBJ := $(GENDIR)/$(CURDIR)/cli
OPENZFS_TIRPC_INCLUDE ?= $(AROS_INCLUDES)
OPENZFS_CRYPTO_INCLUDE ?= $(AROS_INCLUDES)
OPENZFS_NVPAIR_ARCHIVE ?= $(AROS_LIB)/libnvpair.a
OPENZFS_CRYPTO_ARCHIVE ?= $(AROS_LIB)/libboringssl.a
OPENZFS_ZLIB_ARCHIVE ?= $(AROS_LIB)/libz.static.a
OPENZFS_PTHREAD_ARCHIVE ?= $(OPENZFS_PTHREAD_LIBDIR)/libpthread.a
# Older SDKs require a generated pipe2 import stub matching their POSIX runtime.
# A current SDK can leave this empty if its import library exports pipe2.
OPENZFS_PIPE2_OBJECT ?=
OPENZFS_PYTHON ?= python3

OZ_CLI_FLAGS := $(CFLAGS) -O2 -g -ffunction-sections -fdata-sections \
    -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -DOZ_NATIVE_LABEL_CLIENT \
    -DTEXT_DOMAIN=\"zfs\" -DSYSCONFDIR=\"ENV:OpenZFS\" \
    -DZFSEXECDIR=\"SYS:Tools/OpenZFS\" -DPKGDATADIR=\"SYS:Storage/OpenZFS\" \
    -include $(OZ_SOURCE)/aros/engine/config.h -include $(OZ_CLI_OBJ)/metadata.h \
    -I$(OPENZFS_PTHREAD_INCLUDE) -I$(OZ_SOURCE)/aros/cli/include \
    -I$(OPENZFS_CRYPTO_INCLUDE) -I$(OZ_CLI_OBJ) \
    -I$(OZ_SOURCE)/aros/engine/include -I$(OZ_SOURCE)/lib/libspl/include \
    -I$(OZ_SOURCE)/lib/libspl/include/os/linux -I$(OZ_SOURCE)/include \
    -I$(OZ_SOURCE)/include/os/linux/zfs -I$(OZ_SOURCE)/cmd/zpool \
    -I$(OZ_SOURCE)/cmd/zfs -I$(OZ_SOURCE)/cmd -I$(OPENZFS_TIRPC_INCLUDE)

OZ_CLI_UPSTREAM := $(patsubst $(OZ_SOURCE)/%.c,%,\
    $(wildcard $(OZ_SOURCE)/lib/libzfs/*.c $(OZ_SOURCE)/lib/libzfs_core/*.c \
    $(OZ_SOURCE)/lib/libzutil/*.c)) lib/libshare/libshare
OZ_CLI_NATIVE := aros/cli/key-prompt aros/cli/gpt aros/cli/share-unavailable \
    aros/cli/project aros/cli/rewrite aros/cli/stream-client aros/cli/stream-channel \
    aros/cli/arguments aros/cli/process aros/cli/output aros/cli/identity \
    aros/cli/progress-wait aros/cli/title aros/cli/version aros/cli/lifecycle \
    aros/cli/platform-hooks aros/cli/shares-object aros/cli/control-client \
    aros/ui/api aros/ui/admission-client aros/cli/import-files aros/cli/file-vdev \
    aros/cli/device-path aros/cli/native-device aros/cli/device-wait \
    aros/cli/native-label aros/platform/cpu aros/cli/mount aros/cli/zone aros/cli/hostid
OZ_CLI_SUPPORT := module/avl/avl module/zcommon/zfs_namecheck \
    module/zcommon/zfs_comutil module/zcommon/zfs_prop module/zcommon/zpool_prop \
    module/zcommon/zprop_common module/zcommon/zfeature_common \
    module/zcommon/zfs_fletcher module/zcommon/zfs_fletcher_superscalar \
    module/zcommon/zfs_fletcher_superscalar4 module/zcommon/zfs_valstr \
    lib/libuutil/uu_avl lib/libuutil/uu_alloc lib/libuutil/uu_misc \
    lib/libuutil/uu_ident lib/libuutil/uu_list lib/libuutil/uu_string \
    lib/libspl/atomic lib/libspl/list lib/libspl/page lib/libspl/timestamp \
    lib/libnvpair/libnvpair lib/libnvpair/libnvpair_json lib/libtpool/thread_pool
OZ_CLI_COMMON := $(OZ_CLI_UPSTREAM) $(OZ_CLI_NATIVE) $(OZ_CLI_SUPPORT)
OZ_CLI_ARCHIVES := $(OPENZFS_NVPAIR_ARCHIVE) $(OPENZFS_PTHREAD_ARCHIVE) \
    $(OPENZFS_ZLIB_ARCHIVE) $(OPENZFS_CRYPTO_ARCHIVE)
OZ_CLI_LINK := $(LDFLAGS) -Wl,--start-group $(OZ_CLI_ARCHIVES) -Wl,--end-group

$(OZ_CLI_OBJ)/metadata.h: $(OZ_SOURCE)/META $(OZ_SOURCE)/aros/cli/generate-metadata.py
	@$(MKDIR) $(dir $@)
	$(OPENZFS_PYTHON) $(OZ_SOURCE)/aros/cli/generate-metadata.py $(OZ_SOURCE)/META $@

# Git HEAD and tracked dirty state can change without touching META. The
# upstream generator compares content, preserving the header mtime if equal.
.PHONY: openzfs-cli-gitrev-check
openzfs-cli-gitrev-check:

$(OZ_CLI_OBJ)/zfs_gitrev.h: $(OZ_SOURCE)/META $(OZ_SOURCE)/scripts/make_gitrev.sh openzfs-cli-gitrev-check
	@$(MKDIR) $(dir $@)
	cd $(OZ_CLI_OBJ) && bash $(OZ_SOURCE)/scripts/make_gitrev.sh zfs_gitrev.h

# Select only the real upstream check_file_generic helper for commands other
# than zpool. Never section-GC the final AROS executable or its startup roots.
$(OZ_CLI_OBJ)/check-file-generic.o: $(OZ_CLI_OBJ)/cmd/zpool/zpool_vdev.o
	$(AROS_LD) -r --gc-sections -u check_file_generic $< -o $@

OZ_CLI_HEADER_USERS := $(addprefix $(OZ_CLI_OBJ)/,$(OZ_CLI_COMMON) \
    cmd/zfs/zfs_main cmd/zfs/zfs_iter cmd/zfs/zfs_project \
    cmd/zpool/zpool_main cmd/zpool/zpool_iter cmd/zpool/zpool_util cmd/zpool/zpool_vdev \
    aros/cli/startup aros/cli/startup-mount aros/ui/startup-import \
    aros/ui/startup-settings aros/ui/startup-settings-file aros/ui/preferences aros/ui/preferences-file)
$(addsuffix .o,$(OZ_CLI_HEADER_USERS)) $(addsuffix .d,$(OZ_CLI_HEADER_USERS)): \
    $(OZ_CLI_OBJ)/metadata.h $(OZ_CLI_OBJ)/zfs_gitrev.h

$(OZ_CLI_OBJ)/startup-library/aros/cli/startup-mount.o $(OZ_CLI_OBJ)/startup-library/aros/cli/startup-mount.d: $(OZ_CLI_OBJ)/metadata.h $(OZ_CLI_OBJ)/zfs_gitrev.h
$(OZ_CLI_OBJ)/startup-library/aros/cli/startup-mount.o $(OZ_CLI_OBJ)/startup-library/aros/cli/startup-mount.d: | $(OZ_CLI_OBJ)/startup-library/aros/cli
$(OZ_CLI_OBJ)/cmd/zpool/zpool_vdev.o $(OZ_CLI_OBJ)/cmd/zpool/zpool_vdev.d: | $(OZ_CLI_OBJ)/cmd/zpool

$(OZ_CLI_OBJ)/startup-library/aros/cli $(OZ_CLI_OBJ)/cmd/zpool:
	@$(MKDIR) $@

$(AROS_C)/zfs $(AROS_C)/zpool $(AROS_C)/OpenZFSStartup $(AROS_C)/OpenZFSStartupMount: $(OZ_CLI_ARCHIVES)
