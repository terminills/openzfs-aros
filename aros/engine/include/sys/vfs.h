/* libzpool has no kernel mount/vfs structures. Do not pull Linux filesystem
 * ioctl definitions into its AROS translation units. The DOS frontend will
 * require its own real mount/lock layer, not these Linux definitions. */
#ifndef LIB_ZPOOL_BUILD
#error AROS kernel VFS integration is not implemented
#endif
