/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <proto/exec.h>
#include <libraries/security.h>
#include <proto/security.h>
#include "packet-cred.h"

/* Native 1..65534 remain numerically compatible. Native root maps to POSIX
 * zero; native nobody group maps to 65535, which cannot collide with another
 * native group's mapped identity. A nobody UID is rejected, not synthesized.
 * This mapping must also be used by future native ownership setters. */
static gid_t native_group(UWORD gid)
{
    return gid == secROOT_GID ? 0 : gid == secNOBODY_GID ? 65535 : gid;
}

int oz_aros_owner_from_ids(uint64_t uid, uint64_t gid, uint32_t *out)
{
    if (!out) return EINVAL;
    *out = 0;
    if (uid > 65535 || gid > 65535) return EOVERFLOW;
    uint32_t native_uid = uid == 0 ? secROOT_UID : uid == 65535 ? secNOBODY_UID : uid;
    uint32_t native_gid = gid == 0 ? secROOT_GID :
        gid == 65535 ? secNOBODY_GID : gid;
    *out = (native_uid << 16) | native_gid;
    return 0;
}
int oz_aros_ids_from_owner(uint32_t owner,uint32_t *uid,uint32_t *gid)
{
    if(!uid||!gid)return EINVAL;
    uint32_t native_uid=owner>>16;
    /* Object nobody is representable; this does NOT authenticate a sender. */
    *uid=native_uid==secROOT_UID?0:native_uid==secNOBODY_UID?65535:native_uid;
    *gid=native_group(owner&0xffff);
    return 0;
}

int oz_cred_from_aros_owner(const struct secExtOwner *owner, cred_t **out)
{
    if (!out) return EINVAL;
    *out = NULL;
    if (!owner || owner->uid == secNOBODY_UID) return EACCES;
    size_t count = owner->NumSecGroups;
    gid_t *groups = count ? malloc(count * sizeof (*groups)) : NULL;
    if (count && !groups) return ENOMEM;
    const UWORD *native = secSecGroups(owner);
    for (size_t i = 0; i < count; i++) groups[i] = native_group(native[i]);
    uid_t uid = owner->uid == secROOT_UID ? 0 : owner->uid;
    /* Packet ownership exposes effective identity, not an independent real
     * UID. Preserve that same identity in ruid; do not invent another user. */
    cred_t *cr = oz_cred_create(uid, uid, native_group(owner->gid), count, groups);
    free(groups);
    if (!cr) return errno ? errno : ENOMEM;
    *out = cr;
    return 0;
}

struct oz_packet_security {
    struct Library *library;
    struct secExtOwner *owner;
    struct MsgPort *filesystem;
    cred_t *cred;
    uint32_t defaults;
};

void oz_packet_security_end(struct oz_packet_security *context)
{
    if (!context) return;
    struct Library *secBase = context->library;
    crfree(context->cred);
    if (context->owner) secFreeExtOwner(context->owner);
    if (secBase) CloseLibrary(secBase);
    free(context);
}

int oz_packet_security_begin(struct DosPacket *packet, struct MsgPort *filesystem,
    struct oz_packet_security **out)
{
    if (!out) return EINVAL;
    *out = NULL;
    if (!packet || !filesystem) return EINVAL;
    /* Follow the native security.library packet-owner contract. An absent
     * sender is not the current handler task and never confers privilege. */
    if (!packet->dp_Port || (packet->dp_Port->mp_Flags & PF_ACTION) != PA_SIGNAL ||
        !packet->dp_Port->mp_SigTask) return EACCES;
    struct Library *secBase = OpenLibrary((CONST_STRPTR)SECURITYNAME, SECURITYVERSION);
    if (!secBase) return ENOTSUP;
    struct oz_packet_security *context = calloc(1, sizeof (*context));
    if (!context) { CloseLibrary(secBase); return ENOMEM; }
    context->library = secBase;
    context->filesystem = filesystem;
    int error = EACCES;
    if (secIsConfigured()) {
        context->owner = secGetPktOwner(packet);
        error = oz_cred_from_aros_owner(context->owner, &context->cred);
        if (!error) context->defaults = secGetPktDefProtection(packet);
    }
    if (error) oz_packet_security_end(context);
    else *out = context;
    return error;
}

cred_t *oz_packet_security_cred(const struct oz_packet_security *context)
{ return context ? context->cred : NULL; }

int oz_packet_security_defaults(const struct oz_packet_security *context,
    uint32_t *defaults)
{
    if (!defaults) return EINVAL;
    *defaults = 0;
    if (!context) return EACCES;
    *defaults = context->defaults;
    return 0;
}

int oz_packet_security_access(const struct oz_packet_security *context,
    uint32_t owner, uint32_t protection, uint32_t access, uint32_t *reason)
{
    return oz_packet_security_access_on(context,context?context->filesystem:NULL,
        owner,protection,access,reason);
}

int oz_packet_security_access_on(const struct oz_packet_security *context,
    struct MsgPort *filesystem,uint32_t owner,uint32_t protection,
    uint32_t access,uint32_t *reason)
{
    if (reason) *reason = secAC_PERMISSION_DENIED;
    if (!context) return EACCES;
    if (!filesystem) return EINVAL;
    if (!access || (access & ~(secAt_Read | secAt_Write | secAt_Execute |
        secAt_Delete))) return EINVAL;
    struct Library *secBase = context->library;
    LONG result = secAccess_Control(secAC_FILESYSTEM_CONTEXT,
        filesystem, context->owner, owner, protection, access);
    if (reason) *reason = (uint32_t)result;
    if (result == secAC_PERMISSION_GRANTED) return 0;
    return (result & secAC_READ_ONLY_FS) ? EROFS : EACCES;
}

int oz_packet_security_property(const struct oz_packet_security *context,
    uint32_t owner)
{
    if (!context) return EACCES;
    struct Library *secBase = context->library;
    return (secGetRelationshipA(context->owner, owner, NULL) &
        secRelF_PROPERTY_ACCESS) ? 0 : EPERM;
}
int oz_packet_security_set_owner(const struct oz_packet_security *context,
    uint32_t oldowner,uint32_t newowner)
{
    int error=oz_packet_security_property(context,oldowner);
    if(error)return error;
    struct Library *secBase=context->library;
    return (secGetRelationshipA(context->owner,newowner,NULL)&
        (secRelF_ROOT_UID|secRelF_UID_MATCH))?0:EPERM;
}

int oz_packet_security_volume(const struct oz_packet_security *context, uint32_t access)
{return oz_packet_security_volume_on(context,context?context->filesystem:NULL,access);}

int oz_packet_security_volume_on(const struct oz_packet_security *context,
    struct MsgPort *filesystem,uint32_t access)
{
    if (!context) return EACCES;
    uint32_t owner = ((uint32_t)context->owner->uid << 16) | context->owner->gid;
    return oz_packet_security_access_on(context, filesystem, owner, 0, access, NULL);
}

int oz_cred_from_packet(struct DosPacket *packet, cred_t **out)
{
    if (!out) return EINVAL;
    *out = NULL;
    if (!packet) return EINVAL;
    /* Compatibility identity-only capture. No access check uses this port. */
    if (!packet->dp_Port) return EACCES;
    struct oz_packet_security *context = NULL;
    int error = oz_packet_security_begin(packet, packet->dp_Port, &context);
    if (!error) {
        *out = context->cred;
        oz_cred_hold(*out);
    }
    oz_packet_security_end(context);
    return error;
}
