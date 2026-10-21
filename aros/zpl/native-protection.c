/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_vnops.h>
#include "xattr.h"
#include "native-protection.h"

/* Eight bytes: ASCII ARP1, followed by the complete 32-bit Amiga protection
 * word in big-endian order. Absence means no additional native restriction,
 * not an unowned file or a replacement for the upstream ACL. */
int oz_native_protection_get(znode_t *zp, uint32_t *bits, boolean_t *present)
{
    if (!zp || !bits || !present) return EINVAL;
    *bits = 0; *present = B_FALSE;
    unsigned char record[8]; size_t size = 0;
    int error = oz_xattr_get(zp, OZ_PROTECTION_XATTR, record, sizeof (record), &size, kcred);
    if (error == ENODATA) return 0;
    if (error == ERANGE) return EIO;
    if (error) return error;
    if (size != sizeof (record) || memcmp(record, "ARP1", 4)) return EIO;
    *bits = ((uint32_t)record[4] << 24) | ((uint32_t)record[5] << 16) |
        ((uint32_t)record[6] << 8) | record[7];
    *present = B_TRUE;
    return 0;
}

int oz_native_protection_store(znode_t *zp, uint32_t bits, cred_t *cr)
{
    if (!zp) return EINVAL;
    if (!cr) return EACCES;
    int error = zfs_zaccess(zp, ACE_WRITE_ATTRIBUTES, 0, B_FALSE, cr, NULL);
    if (error) return error;
    unsigned char record[8] = {'A','R','P','1', bits >> 24, bits >> 16, bits >> 8, bits};
    return oz_xattr_set(zp, OZ_PROTECTION_XATTR, record, sizeof (record), 0, cr);
}
