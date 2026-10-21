/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_IOCTL_REQUEST_H
#define OZ_IOCTL_REQUEST_H
#include <sys/zfs_context.h>
struct oz_session_cleanup;
struct oz_packet_security;
/* Input/output storage is the service's private request snapshot. Fresh
 * credentials and a retained authenticated session come from admission. */
int oz_ioctl_execute(void *,size_t,const struct oz_packet_security *,struct oz_session_cleanup *);
/* Only from a retained worker, never the DOS owner request loop. */
int oz_ioctl_execute_wait(void *,size_t,const struct oz_packet_security *,struct oz_session_cleanup *);
/* Service-only descriptor of the retained sequential reader/writer. Not client fd. */
int oz_ioctl_execute_stream(void *,size_t,const struct oz_packet_security *,struct oz_session_cleanup *,int);
size_t oz_ioctl_request_limit(void);
#endif
