/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_ONLINE_RECEIVE_H
#define OZ_ONLINE_RECEIVE_H
struct zfsvfs;
struct dmu_recv_cookie;
/* Internal service binding, never a public callback supplied by a client.
 * The synchronous dispatcher retains its worker and mount until return. */
void oz_receive_bind_owner(int (*)(int (*)(void *),void *));
/* Last argument identifies an incoming redacted stream for an existing
 * filesystem; the owner checks the original live dataset before swapping. */
int oz_receive_end_owner(struct zfsvfs *,struct dmu_recv_cookie *,int);
int oz_receive_end_dataset(const char *,struct dmu_recv_cookie *,int);
#endif
