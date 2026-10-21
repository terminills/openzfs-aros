/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef AROS_VDEV_POLICY_H
#define AROS_VDEV_POLICY_H
#include <stdint.h>
struct oz_vdev_policy;
struct spa;
int oz_pool_change_guid(struct spa *,const uint64_t *);
/* Service owner, after upstream config authorization; native identity and
 * retained admission are transacted before changing the upstream label path. */
int oz_pool_vdev_setpath(struct spa *,uint64_t,const char *);
/* Paths are copied. Keep registration until all pool opens/reopens finish. */
int oz_vdev_policy_begin(uint64_t,char *const *,unsigned,struct oz_vdev_policy **);
int oz_vdev_policy_check(uint64_t,const char *);
/* Splitting generates its GUID inside upstream. Bind the temporary destination
 * by exact pool name before entering upstream, and retain until unload/export.
 * Named and GUID rules intersect; neither can relax the other. */
int oz_vdev_policy_begin_named(const char *,char *const *,unsigned,struct oz_vdev_policy **);
int oz_vdev_policy_check_named(uint64_t,const char *,const char *);
/* Service-task configuration changes only, after native blank-file preflight.
 * Remove a newly added path if the corresponding upstream mutation fails. */
struct oz_vdev_extension;
int oz_vdev_policy_extend(uint64_t,const char *,const char *,struct oz_vdev_extension **);
void oz_vdev_policy_extend_end(struct oz_vdev_extension *,int);
/* Service-owner transaction after separate backing identity/ownership checks.
 * All matching rules must contain oldpath and exclude newpath. Both paths are
 * admitted until end; success retires oldpath, failure retires newpath. Native
 * newpath must already have an available binding. This does not create or
 * authenticate a binding, change labels, or grant a caller raw-device access.
 * Keep matching registrations alive through end, on the same service owner. */
struct oz_vdev_repath;
int oz_vdev_policy_repath_begin(uint64_t,const char *,const char *,const char *,
    struct oz_vdev_repath **);
void oz_vdev_policy_repath_end(struct oz_vdev_repath *,int);
/* Service-task GUID changes: admit both identities during upstream sync,
 * then commit the new identity or roll back without dropping path rules. */
int oz_vdev_policy_reguid_begin(uint64_t,uint64_t);
void oz_vdev_policy_reguid_end(uint64_t,uint64_t,int);
void oz_vdev_policy_end(struct oz_vdev_policy *,unsigned *,unsigned *);
#endif
