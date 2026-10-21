/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_NATIVE_XATTR_H
#define OZ_NATIVE_XATTR_H
/* Caller retains znode/mount lifetime. Positive errno; length queries use
 * NULL/zero. No borrowed nvlist storage escapes the xattr read lock. */
int oz_xattr_get(znode_t *, const char *, void *, size_t, size_t *, cred_t *);
#define OZ_XATTR_CREATE 1
#define OZ_XATTR_REPLACE 2
/* Internal named-attribute API; public namespace/native-property admission
 * must be applied by its caller. NULL value removes, non-NULL/zero creates
 * an empty value. All mutations retain upstream transaction/ZIL paths. */
int oz_xattr_set(znode_t *, const char *, const void *, size_t, int, cred_t *);
#endif
