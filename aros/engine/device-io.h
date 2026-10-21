/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OPENZFS_AROS_DEVICE_IO_H
#define OPENZFS_AROS_DEVICE_IO_H
#include <stdint.h>
#include <stddef.h>
struct oz_device;
/* Snapshot collision check only, not a write lease or device existence test. */
int oz_device_dos_claim(const char *,unsigned);
/* Queue behind this service's synchronous raw write chunks before attempting
 * DOS namespace publication. External DOS lock contention still returns EBUSY.
 * Caller must hold neither DOS-list nor engine locks, and must perform only
 * bounded DOS-list edits between begin/end: no engine I/O, callbacks or waits.
 * Successful begin must be paired with end on the same task. */
int oz_device_publication_begin(void);
void oz_device_publication_end(void);
/* Startup may be called with a different task holding DOS LDF_READ. Match
 * AddDosEntry's reader-safe insertion contract, still serialized with raw
 * writes and other publishers. Never use this for deletion/repartitioning. */
int oz_device_startup_publication_begin(void);
void oz_device_startup_publication_end(void);
/* Writable extents exclude overlapping writers and admitted readonly handles
 * in this linked service. Disjoint extents and unbound inspection reads may
 * coexist. This does not reserve
 * the unit against external processes or coordinate DOS mount changes. */
/* Each write chunk holds shared list/entry locks, excluding DOS publication
 * while allowing independent admission readers. This spans the recognizable-claim
 * check through completion. Service-local writers queue before taking this
 * publication interval, so independent ZIO workers do not reject one another.
 * Reads do not take that queue lock. External DOS-list
 * lock contention returns EBUSY
 * without revoking the handle, never waiting on a possible service client.
 * A detected claim
 * permanently revokes writes on that handle (EBUSY); reopening is required.
 * This is not a lifetime lease: DOS publication between chunks is possible,
 * and opaque/custom startup messages or external raw users are not excluded.
 * Reads and flushing previously submitted I/O remain available. */
int oz_device_open(const char *,unsigned,int,struct oz_device **);
/* Exact byte extent of an already identified/authorized device. This does
 * not discover partitions, establish identity, or authorize raw access. */
int oz_device_open_range(const char *,unsigned,int,uint64_t,uint64_t,struct oz_device **);
/* Retain requires an already owned live reference. Each owner releases with
 * close after its synchronous I/O ends. Final release closes the native unit
 * and drops its writer reservation; retaining does not reopen or reidentify
 * the device and grants no additional access rights. */
int oz_device_retain(struct oz_device *);
/* Trusted engine admission only: publish an already authorized readonly
 * extent under its exact native path. No device discovery or reopening.
 * Binding and returned handles own references; unbind prevents future opens
 * but does not invalidate I/O already owned by the pool. Caller must enforce
 * per-pool path policy in addition to this service-wide identity binding.
 * Admission refuses overlapping service writers. Once admitted, the handle
 * excludes overlapping writers through final reference release, not merely
 * unbind. This is not exclusion against other processes or DOS mounts. */
struct oz_device_binding;
int oz_device_bind_readonly(const char *,struct oz_device *,struct oz_device_binding **);
/* Trusted writable admission only. Requires an existing writable reserved
 * extent, never upgrades a readonly handle or reopens a device. One writable
 * handle may have only one binding. Bound opens retain the exact handle;
 * unbind prevents future opens but the extent reservation lasts until the
 * last owner closes. This is internal engine capability reuse, not a new
 * security authorization or protection against external raw-device users.
 * DOS claim/write checks and revocation still apply to every actual write. */
int oz_device_bind_writable(const char *,struct oz_device *,struct oz_device_binding **);
/* Trusted readonly recovery admission for an explicitly named unavailable
 * member. Opens return ENODEV (not unbound ENOENT), preventing raw fallback or
 * silently adopting a later replacement. Unbind/export is required to retry.
 * Caller validates the canonical path and distinguishes absence from I/O error. */
int oz_device_bind_unavailable(const char *,struct oz_device_binding **);
int oz_device_bound_open(const char *,int,struct oz_device **);
/* Engine-only explicit recovery. Snapshot retains even invalid media. Replace
 * requires an invalid expected handle and an unbound readonly candidate whose
 * pool/member labels the caller has validated. Exact path extent must match.
 * Old references remain permanently invalid; no automatic media adoption. */
int oz_device_binding_snapshot(struct oz_device_binding *,struct oz_device **);
int oz_device_binding_replace(struct oz_device_binding *,struct oz_device *,struct oz_device *);
/* Service-owner transaction for a separately resolved/authorized canonical
 * path. Candidate must describe the same opened device/unit and exact extent.
 * It does not replace the admitted handle or upgrade access. Old and new names
 * both work while pending; end commits (error==0) or rolls back. Keep binding
 * alive until end; do not unbind during the transaction. Admission policy and
 * upstream label update must be coordinated separately by the owner. */
int oz_device_binding_repath_begin(struct oz_device_binding *,struct oz_device *,
    struct oz_device *,const char *);
int oz_device_binding_repath_end(struct oz_device_binding *,int);
void oz_device_unbind(struct oz_device_binding *);
void oz_device_close(struct oz_device *);
uint64_t oz_device_size(const struct oz_device *);
unsigned oz_device_sector(const struct oz_device *);
/* Poll generation-capable admitted handles, without adopting media or
 * invoking pool mutations. Start/stop belong to one engine-owner task;
 * successful stop joins the worker and releases its retained handles. */
/* notify runs after snapshot references/registry lock are released, and may
 * repeat while invalid handles remain. It must not stop/join its own worker. */
int oz_device_media_start(void (*notify)(void));
int oz_device_media_stop(void);
int oz_device_media_invalid(const struct oz_device *);
int oz_device_transfer(struct oz_device *,void *,size_t,uint64_t,int);
/* A native TDERR_DiskChanged, or a changed supported TD_CHANGENUM sampled
 * during binding/reopen, permanently invalidates I/O with ENXIO. Admission
 * samples generation before/after geometry. Close/re-admit to obtain new
 * geometry and ownership checks. This is not continuous monitoring or an
 * atomic media-check-and-write guarantee. */
int oz_device_flush(struct oz_device *);
/* Fixed standard INQUIRY only; no caller-supplied SCSI command or pointers. */
int oz_device_inquiry(struct oz_device *,unsigned char [96],unsigned *);
#endif
