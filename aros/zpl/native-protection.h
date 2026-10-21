/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_NATIVE_PROTECTION_H
#define OZ_NATIVE_PROTECTION_H
/* Private OS metadata in a normal upstream xattr. Not a replacement ACL.
 * Reading is internal metadata access: caller must separately enforce ZFS
 * rights and native policy before returning user data or performing changes. */
#define OZ_PROTECTION_XATTR "system.aros.protection"
int oz_native_protection_get(znode_t *, uint32_t *, boolean_t *);
/* Internal persistence only. Public property changes additionally require
 * the actual packet security.library relationship check. */
int oz_native_protection_store(znode_t *, uint32_t, cred_t *);
#endif
