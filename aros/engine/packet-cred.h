/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_PACKET_CRED_H
#define OZ_PACKET_CRED_H
#include <sys/cred.h>
struct secExtOwner;
struct DosPacket;
struct MsgPort;
struct oz_packet_security;
/* Capture before ReplyPkt; filesystem port must outlive the context.
 * These functions accept native packed 16-bit ownership and Amiga protection
 * values, NOT raw OpenZFS FUIDs or POSIX modes. They supplement ZFS ACLs. */
int oz_packet_security_begin(struct DosPacket *, struct MsgPort *,
    struct oz_packet_security **);
void oz_packet_security_end(struct oz_packet_security *);
/* Borrowed immutable credential; hold explicitly if retaining past end. */
cred_t *oz_packet_security_cred(const struct oz_packet_security *);
int oz_packet_security_defaults(const struct oz_packet_security *, uint32_t *);
int oz_packet_security_access(const struct oz_packet_security *, uint32_t,
    uint32_t, uint32_t, uint32_t *);
/* Native stream files may belong to another filesystem. Caller must retain
 * a trusted DOS lock/handle keeping this handler alive, and obtain ownership
 * and protection from that object. Never accept these values from a client. */
int oz_packet_security_access_on(const struct oz_packet_security *,
    struct MsgPort *,uint32_t,uint32_t,uint32_t,uint32_t *);
int oz_packet_security_property(const struct oz_packet_security *, uint32_t);
int oz_packet_security_set_owner(const struct oz_packet_security *,uint32_t,uint32_t);
/* Volume-only policy: object ACLs are checked independently by ZFS. */
int oz_packet_security_volume(const struct oz_packet_security *, uint32_t);
int oz_packet_security_volume_on(const struct oz_packet_security *,struct MsgPort *,uint32_t);
/* Conversion alone does NOT authenticate; input must be a security.library
 * snapshot. Exposed separately for deterministic native identity tests. */
int oz_cred_from_aros_owner(const struct secExtOwner *, cred_t **);
/* Reverse mapping accepts resolved POSIX IDs, never encoded FUIDs.
 * Explicit error for identities the native 16-bit API cannot represent. */
int oz_aros_owner_from_ids(uint64_t, uint64_t, uint32_t *);
/* Object ownership only: POSIX 65535 represents native nobody, never root. */
int oz_aros_ids_from_owner(uint32_t,uint32_t *,uint32_t *);
/* Call before replying to a real DOS packet. Own the returned immutable
 * credential until crfree. No global/service identity or root fallback. */
int oz_cred_from_packet(struct DosPacket *, cred_t **);
#endif
