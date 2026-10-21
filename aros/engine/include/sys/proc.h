/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_AROS_PROC_H
#define OZ_AROS_PROC_H
struct proc;
struct oz_caller_scope;
/* Internal service-owned identities, not PIDs or caller-supplied pointers.
 * Hold requires an existing reference/registry lock. Scopes do not propagate
 * to workers; the service must validate a session and explicitly pass it. */
int oz_caller_create(struct proc **);
int oz_caller_hold(struct proc *);
void oz_caller_release(struct proc *);
int oz_caller_enter(struct proc *, struct oz_caller_scope **);
int oz_caller_leave(struct oz_caller_scope *);
struct proc *oz_caller_capture(void); /* retained, NULL without a scope */
boolean_t zfs_proc_is_caller(struct proc *);
#endif
