/* SPDX-License-Identifier: CDDL-1.0 */
/* Compile the real helper against bounded synthetic parent graphs. This is
 * error/reference bookkeeping coverage, not native scheduling evidence. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
typedef struct { uint64_t z_root; int z_rename_lock; } zfsvfs_t;
typedef struct node {
    uint64_t z_id, parent;
    zfsvfs_t *fs;
    struct node *z_sa_hdl;
    int refs;
} znode_t;
#define ZTOZSB(z) ((z)->fs)
#define ASSERT(x) assert(x)
#define MUTEX_HELD(p) (*(p))
#define SET_ERROR(e) (e)
#define SA_ZPL_PARENT(fs) ((void)(fs),0)
static znode_t nodes[5];
static int reads, fail_read;
static int zfs_zget(zfsvfs_t *fs, uint64_t id, znode_t **out)
{
    (void)fs;
    if (id>=5) return ENOENT;
    *out=&nodes[id]; (*out)->refs++; return 0;
}
static void zrele(znode_t *node)
{ assert(node->refs==1); node->refs--; }
static int sa_lookup(znode_t *node, int attr, uint64_t *out, size_t size)
{
    (void)attr; assert(size==8);
    if (++reads>64) {
        fputs("FAIL exchange ancestry did not terminate on a five-node graph\n",stderr);
        exit(99);
    }
    if (reads==fail_read) return EIO;
    *out=node->parent; return 0;
}
#include "exchange-ancestry.inc"
static int oracle(unsigned moved, unsigned parent)
{
    unsigned visited=0;
    for (;;) {
        if (parent==moved) return EINVAL;
        if (!parent) return 0;
        if (parent>=5) return ENOENT;
        if (visited & (1u<<parent)) return EIO;
        visited |= 1u<<parent;
        parent=nodes[parent].parent;
    }
}
static void check(unsigned moved, unsigned parent, int expected)
{
    reads=0;
    int error=exchange_ancestry(&nodes[moved],&nodes[parent]);
    if (error!=expected) {
        fprintf(stderr,"FAIL moved=%u parent=%u error=%d expected=%d\n",
            moved,parent,error,expected); exit(1);
    }
    for (unsigned i=0;i<5;i++) assert(!nodes[i].refs);
}
int main(void)
{
    zfsvfs_t fs={0,1};
    for (unsigned i=0;i<5;i++) nodes[i]=(znode_t){i,0,&fs,&nodes[i],0};
    unsigned cases=0;
    for (unsigned graph=0;graph<625;graph++) {
        unsigned code=graph;
        for (unsigned i=1;i<5;i++) { nodes[i].parent=code%5; code/=5; }
        for (unsigned moved=1;moved<5;moved++)
            for (unsigned parent=0;parent<5;parent++) {
                check(moved,parent,oracle(moved,parent)); cases++;
            }
    }
    nodes[2].parent=1; nodes[1].parent=0;
    check(0,2,EINVAL);
    for (fail_read=1;fail_read<=2;fail_read++) check(4,2,EIO);
    fail_read=0; nodes[1].parent=9; check(4,2,ENOENT);
    printf("PASS production exchange ancestry graphs=%u root=1 lookup-errors=3 refs=0\n",cases);
    return 0;
}
