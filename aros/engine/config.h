/* Bootstrap capabilities actually checked with the ADT AROS compiler.
 * No Linux-generated configure result is reused. */
#ifndef OPENZFS_AROS_CONFIG_H
#define OPENZFS_AROS_CONFIG_H
/* Match upstream --enable-debug as a coherent configuration: enabling ASSERT
 * alone is invalid because release-only helpers deliberately return dummies. */
#define DEBUG 1
#define ZFS_DEBUG 1
#undef NDEBUG
#define HAVE_IMPLICIT_FALLTHROUGH 1
/* The ABI uses 64-bit off_t unconditionally; verified by SPL static assert. */
#define O_LARGEFILE 0
#endif
