/* SPDX-License-Identifier: CDDL-1.0 */
/* Fault injection around the actual production ancestry helper. This host
 * test proves error/lock handling, not native filesystem or SMP execution. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
typedef struct { int held; } krwlock_t;
typedef int krw_t;
enum { RW_READER, RW_WRITER, KM_SLEEP };
struct fs { uint64_t z_root; };
typedef struct node {
    uint64_t z_id, parent;
    krwlock_t z_parent_lock;
    struct fs *fs;
    void *z_sa_hdl;
    int refs;
} znode_t;
#define ZTOZSB(z) ((z)->fs)
#define SA_ZPL_PARENT(fs) ((void)(fs), 0)
#define SET_ERROR(e) (e)
static znode_t nodes[5];
static int calls, fail_at, allocations;
static void *kmem_alloc(size_t size, int flags)
{ (void)flags; allocations++; void *p = calloc(1,size); assert(p); return p; }
static void kmem_free(void *p, size_t size)
{ (void)size; allocations--; free(p); }
static int rw_tryenter(krwlock_t *lock, krw_t mode)
{ (void)mode; assert(!lock->held); lock->held=1; return 1; }
static void rw_enter(krwlock_t *lock, krw_t mode)
{ assert(rw_tryenter(lock,mode)); }
static void rw_exit(krwlock_t *lock)
{ assert(lock->held); lock->held=0; }
static void zfs_zrele_async(znode_t *node)
{ assert(node->refs); node->refs--; }
static int zfs_zget(struct fs *fs, uint64_t id, znode_t **out)
{ (void)fs; if (id>=5) return ENOENT; *out=&nodes[id]; (*out)->refs++; return 0; }
static int sa_lookup(void *handle, int attr, uint64_t *out, size_t size)
{
    (void)attr; assert(size==8);
    if (++calls==fail_at) return EIO;
    *out=((znode_t *)handle)->parent; return 0;
}
#include "rename-lock.inc"
static void run(int failure, int expected)
{
    struct fs fs={.z_root=0};
    calls=0; fail_at=failure;
    for (unsigned i=0;i<5;i++) {
        nodes[i]=(znode_t){.z_id=i,.parent=i ? i-1 : 0,.fs=&fs};
        nodes[i].z_sa_hdl=&nodes[i];
    }
    /* Move source 4 beneath destination 2, source parent 3. Walk 2->1->0. */
    zfs_zlock_t *locks=NULL;
    int error=zfs_rename_lock(&nodes[4],&nodes[2],&nodes[3],&locks);
    zfs_rename_unlock(&locks);
    assert(!locks && !allocations);
    for (unsigned i=0;i<5;i++) assert(!nodes[i].refs && !nodes[i].z_parent_lock.held);
    if (error!=expected) {
        fprintf(stderr,"lookup failure=%d got=%d expected=%d\n",failure,error,expected);
        exit(1);
    }
}
int main(void)
{
    run(0,0);
    run(1,EIO);
    run(2,EIO);
    puts("PASS production rename ancestry: success, first/deep lookup errors, lock/reference cleanup");
    return 0;
}
