/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_vnops.h>
#include <sys/zfs_dir.h>
#include <sys/zap.h>
#include <pthread.h>
#include <proto/exec.h>
#include <proto/dos.h>

static int child(znode_t *parent, char *name, znode_t **out)
{
    uint64_t entry;
    int error = zap_lookup(ZTOZSB(parent)->z_os, parent->z_id, name, 8, 1, &entry);
    return (error ? error : zfs_zget(ZTOZSB(parent), ZFS_DIRENT_OBJ(entry), out));
}
static int directory(znode_t *parent, char *name, znode_t **out)
{
    vattr_t va = {.va_mask = ATTR_MODE, .va_mode = S_IFDIR | 0700};
    return (zfs_mkdir(parent, name, &va, out, kcred, 0, NULL, NULL));
}
static int tagged_file(znode_t *parent, char *name, unsigned char tag, znode_t **out)
{
    vattr_t va = {.va_mask = ATTR_MODE, .va_mode = S_IFREG | 0600};
    int error = zfs_create(parent, name, &va, 1, S_IRUSR | S_IWUSR, out, kcred, 0, NULL, NULL);
    unsigned char bytes[64]; memset(bytes, tag, sizeof (bytes));
    size_t residual = sizeof (bytes);
    if (!error) error = zfs_write_simple(*out, bytes, sizeof (bytes), 0, &residual);
    if (!error && residual) error = EIO;
    if (!error) error = zfs_fsync(*out, 0, kcred);
    return (error);
}
static int tag_check(znode_t *file, unsigned char tag)
{
    unsigned char bytes[64]; struct iovec vec = {bytes, sizeof (bytes)};
    zfs_uio_t uio;
    zfs_uio_iovec_init(&uio, &vec, 1, 0, UIO_SYSSPACE, sizeof (bytes), 0);
    int error = zfs_read(file, &uio, 0, kcred);
    if (!error && (uio.uio_resid || file->z_size != sizeof (bytes))) error = EIO;
    for (unsigned i = 0; i < sizeof (bytes) && !error; i++) if (bytes[i] != tag) error = EIO;
    return (error);
}
#define RENAME(s,n,t,m,f) zfs_rename(s,n,t,m,kcred,0,f,NULL,NULL)
extern zil_replay_func_t *const zfs_replay_vector[TX_MAX_TYPE];
/* Exercise the real replay dispatch, including swapped-endian record headers.
 * This is record dispatch qualification, not an abrupt-crash recovery test. */
static int replay_exchange(znode_t *left, znode_t *right)
{
    znode_t *a=NULL, *b=NULL;
    int error=child(left,"a",&a);
    if (!error) error=child(right,"moved",&b);
    size_t size=sizeof(lr_rename_t)+sizeof("a")+sizeof("moved");
    lr_rename_t *record=kmem_zalloc(size,KM_SLEEP);
    for (unsigned swapped=0; swapped<2 && !error; swapped++) {
        record->lr_rename.lr_common.lrc_txtype=TX_RENAME_EXCHANGE;
        record->lr_rename.lr_common.lrc_reclen=size;
        record->lr_rename.lr_sdoid=left->z_id;
        record->lr_rename.lr_tdoid=right->z_id;
        memcpy(record->lr_data,"a\0moved",sizeof("a\0moved"));
        if (swapped) byteswap_uint64_array(record,sizeof(*record));
        error=zfs_replay_vector[TX_RENAME_EXCHANGE](ZTOZSB(left),record,swapped);
        znode_t *found=NULL;
        if (!error) error=child(left,"a",&found);
        if (!error && found!=(swapped?a:b)) error=EIO;
        if (found) oz_znode_rele(found);
        found=NULL;
        if (!error) error=child(right,"moved",&found);
        if (!error && found!=(swapped?b:a)) error=EIO;
        if (found) oz_znode_rele(found);
    }
    kmem_free(record,size);
    if (a) oz_znode_rele(a);
    if (b) oz_znode_rele(b);
    if (!error) {
        const char *proof="Native exchange replay dispatch PASS native=1 byteswap=1 restored=1\n";
        for (const char *p=proof;*p;p++) RawPutChar(*p);
    }
    return error;
}
/* Linked only into the private QEMU diagnostics. Publish the owner last;
 * unrelated upstream worker tasks always call the real SA implementation. */
static struct Task *lookup_fault_owner;
static sa_handle_t *lookup_fault_handle;
static sa_attr_type_t lookup_fault_attr;
static unsigned lookup_fault_hits;
static sa_handle_t *lookup_cycle_second;
static uint64_t lookup_cycle_first_parent, lookup_cycle_second_parent;
int __real_sa_lookup(sa_handle_t *, sa_attr_type_t, void *, uint32_t);
int __wrap_sa_lookup(sa_handle_t *handle, sa_attr_type_t attr, void *buf,
    uint32_t size)
{
    if (__atomic_load_n(&lookup_fault_owner, __ATOMIC_ACQUIRE) == FindTask(NULL) &&
        (handle == lookup_fault_handle ||
         (lookup_cycle_second && handle == lookup_cycle_second)) && attr == lookup_fault_attr) {
        lookup_fault_hits++;
        if (lookup_cycle_second) {
            if (size!=sizeof(uint64_t)) return EIO;
            uint64_t value=handle==lookup_fault_handle ?
                lookup_cycle_first_parent : lookup_cycle_second_parent;
            memcpy(buf,&value,sizeof(value));
            return 0;
        }
        return EIO;
    }
    return __real_sa_lookup(handle,attr,buf,size);
}
static int ancestry_failure(znode_t *left, znode_t *right, znode_t *tree,
    znode_t *fault_parent, int cycle)
{
    uint64_t left_size=left->z_size, right_size=right->z_size;
    uint64_t left_links=left->z_links, right_links=right->z_links;
    lookup_fault_handle=fault_parent->z_sa_hdl;
    lookup_fault_attr=SA_ZPL_PARENT(ZTOZSB(fault_parent));
    lookup_fault_hits=0;
    if (cycle) {
        lookup_fault_handle=right->z_sa_hdl;
        lookup_cycle_second=fault_parent->z_sa_hdl;
        lookup_cycle_first_parent=fault_parent->z_id;
        lookup_cycle_second_parent=right->z_id;
    }
    __atomic_store_n(&lookup_fault_owner,FindTask(NULL),__ATOMIC_RELEASE);
    int error=RENAME(left,"tree",right,"tree",0);
    __atomic_store_n(&lookup_fault_owner,NULL,__ATOMIC_RELEASE);
    lookup_cycle_second=NULL;
    if (error != EIO || (cycle ? lookup_fault_hits<2 || lookup_fault_hits>8 :
        lookup_fault_hits != 1)) return EIO;
    znode_t *found=NULL;
    error=child(left,"tree",&found);
    if (!error && (found != tree || left->z_size != left_size ||
        right->z_size != right_size || left->z_links != left_links ||
        right->z_links != right_links ||
        zap_contains(ZTOZSB(right)->z_os,right->z_id,"tree") != ENOENT)) error=EIO;
    if (found) oz_znode_rele(found);
    return error;
}
struct exchange_worker {
    znode_t *source, *target;
    char *from, *to;
    int error;
    unsigned *ready, *release, completed;
};
static int sibling_exchange(znode_t *parent, const char *first,
    const char *second, znode_t *a, znode_t *b)
{
    uint64_t size=parent->z_size, links=parent->z_links;
    uint64_t asize=a->z_size, alinks=a->z_links;
    uint64_t bsize=b->z_size, blinks=b->z_links;
    int error=0;
    for (unsigned i=0; i<2 && !error; i++) {
        error=RENAME(parent,(char *)first,parent,(char *)second,RENAME_EXCHANGE);
        znode_t *found=NULL;
        if (!error) error=child(parent,(char *)first,&found);
        if (!error && found!=(i?a:b)) error=EIO;
        if (found) oz_znode_rele(found);
        found=NULL;
        if (!error) error=child(parent,(char *)second,&found);
        if (!error && found!=(i?b:a)) error=EIO;
        if (found) oz_znode_rele(found);
        uint64_t ap=0,bp=0;
        if (!error) error=sa_lookup(a->z_sa_hdl,SA_ZPL_PARENT(ZTOZSB(a)),&ap,8);
        if (!error) error=sa_lookup(b->z_sa_hdl,SA_ZPL_PARENT(ZTOZSB(b)),&bp,8);
        if (!error && (ap!=parent->z_id || bp!=parent->z_id ||
            parent->z_size!=size || parent->z_links!=links ||
            a->z_size!=asize || a->z_links!=alinks ||
            b->z_size!=bsize || b->z_links!=blinks)) error=EIO;
    }
    return error;
}
static int missing_exchange(znode_t *source, znode_t *target, znode_t *tree)
{
    uint64_t ssize=source->z_size, slinks=source->z_links;
    uint64_t tsize=target->z_size, tlinks=target->z_links;
    uint64_t parent=0;
    int error=RENAME(source,"tree",target,"absent-exchange",RENAME_EXCHANGE);
    if (error!=ENOENT) return EIO;
    znode_t *found=NULL;
    error=child(source,"tree",&found);
    if (!error && found!=tree) error=EIO;
    if (found) oz_znode_rele(found);
    if (!error) error=sa_lookup(tree->z_sa_hdl,SA_ZPL_PARENT(ZTOZSB(tree)),&parent,8);
    if (!error && (parent!=source->z_id || source->z_size!=ssize ||
        source->z_links!=slinks || target->z_size!=tsize || target->z_links!=tlinks ||
        zap_contains(ZTOZSB(target)->z_os,target->z_id,"absent-exchange")!=ENOENT)) error=EIO;
    return error;
}
static int cross_exchange(znode_t *left, char *first, znode_t *right,
    char *second, znode_t *a, znode_t *b)
{
    uint64_t ls=left->z_size, rs=right->z_size;
    uint64_t ll=left->z_links, rl=right->z_links;
    uint64_t al=a->z_links, bl=b->z_links;
    int delta=(S_ISDIR(b->z_mode)?1:0)-(S_ISDIR(a->z_mode)?1:0);
    int error=0;
    for (unsigned i=0;i<2 && !error;i++) {
        error=RENAME(left,first,right,second,RENAME_EXCHANGE);
        znode_t *found=NULL;
        if (!error) error=child(left,first,&found);
        if (!error && found!=(i?a:b)) error=EIO;
        if (found) oz_znode_rele(found);
        found=NULL;
        if (!error) error=child(right,second,&found);
        if (!error && found!=(i?b:a)) error=EIO;
        if (found) oz_znode_rele(found);
        uint64_t ap=0,bp=0;
        if (!error) error=sa_lookup(a->z_sa_hdl,SA_ZPL_PARENT(ZTOZSB(a)),&ap,8);
        if (!error) error=sa_lookup(b->z_sa_hdl,SA_ZPL_PARENT(ZTOZSB(b)),&bp,8);
        if (!error && (ap!=(i?left:right)->z_id || bp!=(i?right:left)->z_id ||
            left->z_size!=ls || right->z_size!=rs ||
            left->z_links!=ll+(i?0:delta) || right->z_links!=rl-(i?0:delta) ||
            a->z_links!=al || b->z_links!=bl)) error=EIO;
    }
    return error;
}
static int exchange_ancestry_failure(znode_t *left, znode_t *right,
    znode_t *empty, znode_t *tree, znode_t *ancestor, unsigned mode)
{
    uint64_t ls=left->z_size, rs=right->z_size;
    uint64_t ll=left->z_links, rl=right->z_links;
    lookup_fault_handle=(mode==1?left:right)->z_sa_hdl;
    lookup_fault_attr=SA_ZPL_PARENT(ZTOZSB(left));
    lookup_fault_hits=0;
    if (mode==2) {
        lookup_cycle_second=ancestor->z_sa_hdl;
        lookup_cycle_first_parent=ancestor->z_id;
        lookup_cycle_second_parent=right->z_id;
    }
    __atomic_store_n(&lookup_fault_owner,FindTask(NULL),__ATOMIC_RELEASE);
    int error=RENAME(left,"empty",right,"tree",RENAME_EXCHANGE);
    __atomic_store_n(&lookup_fault_owner,NULL,__ATOMIC_RELEASE);
    lookup_cycle_second=NULL;
    if (error!=EIO || (mode==2 ? lookup_fault_hits<2 || lookup_fault_hits>8 :
        lookup_fault_hits!=1)) return EIO;
    znode_t *found=NULL;
    error=child(left,"empty",&found);
    if (!error && found!=empty) error=EIO;
    if (found) oz_znode_rele(found);
    found=NULL;
    if (!error) error=child(right,"tree",&found);
    if (!error && found!=tree) error=EIO;
    if (found) oz_znode_rele(found);
    if (!error && (left->z_size!=ls || right->z_size!=rs ||
        left->z_links!=ll || right->z_links!=rl)) error=EIO;
    return error;
}
static void *exchange_names(void *arg)
{
    struct exchange_worker *w = arg;
    __atomic_fetch_add(w->ready,1,__ATOMIC_RELEASE);
    while (!__atomic_load_n(w->release,__ATOMIC_ACQUIRE)) Delay(1);
    for (unsigned i = 0; i < 32 && !w->error; i++) {
        w->error = RENAME(w->source,w->from,w->target,w->to,RENAME_EXCHANGE);
        if (!w->error) w->completed++;
    }
    return (NULL);
}
static int exchange_race(znode_t *left, char *first, znode_t *right, char *second)
{
    unsigned ready=0, release=0;
    struct exchange_worker workers[2] = {
        {left,right,first,second,0,&ready,&release,0},
        {right,left,second,first,0,&ready,&release,0}
    };
    pthread_t threads[2]; unsigned started = 0; int error = 0;
    for (; started < 2; started++) {
        error = pthread_create(&threads[started],NULL,exchange_names,&workers[started]);
        if (error) break;
    }
    unsigned polls=0;
    while (__atomic_load_n(&ready,__ATOMIC_ACQUIRE)<started && polls++<500) Delay(1);
    if (__atomic_load_n(&ready,__ATOMIC_ACQUIRE)!=started && !error) error=ETIMEDOUT;
    __atomic_store_n(&release,1,__ATOMIC_RELEASE);
    for (unsigned i = 0; i < started; i++) {
        int joined=pthread_join(threads[i],NULL);
        if (!error) error=joined;
        if (!error) error = workers[i].error;
        if (!error && workers[i].completed!=32) error=EIO;
    }
    return (error);
}
static int marked_directory(znode_t *parent, char *name, unsigned char tag,
    znode_t **out)
{
    int error=directory(parent,name,out);
    znode_t *payload=NULL;
    if (!error) error=tagged_file(*out,"payload",tag,&payload);
    if (payload) oz_znode_rele(payload);
    return error;
}
static int persisted_entry(znode_t *parent, char *name, boolean_t isdir,
    unsigned char tag)
{
    znode_t *node=NULL, *payload=NULL;
    int error=child(parent,name,&node);
    uint64_t stored_parent=0;
    if (!error) error=sa_lookup(node->z_sa_hdl,SA_ZPL_PARENT(ZTOZSB(node)),&stored_parent,8);
    if (!error && (stored_parent!=parent->z_id ||
        (isdir ? !S_ISDIR(node->z_mode) : !S_ISREG(node->z_mode)) ||
        node->z_links!=(isdir?2:1))) error=EIO;
    if (!error && isdir) {
        if (node->z_size!=3) error=EIO;
        if (!error) error=child(node,"payload",&payload);
        if (!error) error=sa_lookup(payload->z_sa_hdl,SA_ZPL_PARENT(ZTOZSB(payload)),&stored_parent,8);
        if (!error && (stored_parent!=node->z_id || payload->z_links!=1)) error=EIO;
    }
    if (!error) error=tag_check(isdir?payload:node,tag);
    if (payload) oz_znode_rele(payload);
    if (node) oz_znode_rele(node);
    return error;
}
/* Unlike the stress pairs, these names stay exchanged on disk. Cold readers
 * must discover the new object types, parents and unique payloads unaided by
 * any in-memory znode identities from the creating process. */
static int persistent_exchange(znode_t *fixture, boolean_t write)
{
    znode_t *base=NULL, *left=NULL, *right=NULL, *slots[4]={NULL};
    int error=write?directory(fixture,"durable",&base):child(fixture,"durable",&base);
    if (!error) error=write?directory(base,"left",&left):child(base,"left",&left);
    if (!error) error=write?directory(base,"right",&right):child(base,"right",&right);
    if (!error && write) {
        error=marked_directory(left,"tree",0x11,&slots[0]);
        if (!error) error=marked_directory(right,"tree",0x22,&slots[1]);
        if (!error) error=tagged_file(left,"mixed",0x33,&slots[2]);
        if (!error) error=marked_directory(right,"mixed",0x44,&slots[3]);
        if (!error) error=RENAME(left,"tree",right,"tree",RENAME_EXCHANGE);
        if (!error) error=RENAME(left,"mixed",right,"mixed",RENAME_EXCHANGE);
        if (!error) error=zil_commit(ZTOZSB(fixture)->z_log,0);
    }
    for (unsigned i=0;i<4;i++) if (slots[i]) oz_znode_rele(slots[i]);
    if (!error) error=persisted_entry(left,"tree",B_TRUE,0x22);
    if (!error) error=persisted_entry(right,"tree",B_TRUE,0x11);
    if (!error) error=persisted_entry(left,"mixed",B_TRUE,0x44);
    if (!error) error=persisted_entry(right,"mixed",B_FALSE,0x33);
    if (!error && (left->z_links!=4 || right->z_links!=3 ||
        left->z_size!=4 || right->z_size!=4)) error=EIO;
    if (right) oz_znode_rele(right);
    if (left) oz_znode_rele(left);
    if (base) oz_znode_rele(base);
    char text[128];
    snprintf(text,sizeof(text),"Native persistent directory exchange write=%d directories=1 mixed=1 parents=1 payloads=4 result=%d\n",write,error);
    for (char *p=text;*p;p++) RawPutChar(*p);
    return error;
}
static int replace_empty(znode_t *left, znode_t *right)
{
    znode_t *source = NULL, *target = NULL, *found = NULL;
    zfsvfs_t *fs = ZTOZSB(left);
    int error = directory(left,"temporary",&source);
    if (!error) error = directory(right,"vacant",&target);
    if (!error) error = RENAME(left,"temporary",right,"vacant",0);
    if (!error) error = child(right,"vacant",&found);
    if (!error && (found != source || !target->z_unlinked || target->z_links ||
        oz_unlinked_drain_exclusive(fs) != EBUSY)) error = EIO;
    if (found) oz_znode_rele(found);
    if (target) oz_znode_rele(target);
    if (!error) error = oz_unlinked_drain_exclusive(fs);
    if (!error) error = zfs_rmdir(right,"vacant",NULL,kcred,0);
    if (source) oz_znode_rele(source);
    if (!error) error = oz_unlinked_drain_exclusive(fs);
    return (error);
}
int oz_rename_test(zfsvfs_t *fs, boolean_t write)
{
    znode_t *root = NULL, *parent = NULL, *fixture = NULL, *left = NULL, *right = NULL;
    znode_t *a = NULL, *b = NULL, *c = NULL, *tree = NULL, *branch = NULL, *empty = NULL;
    int error = zfs_zget(fs, fs->z_root, &root);
    if (!error) error = child(root, "subdir", &parent);
    if (!error) error = write ? directory(parent, "rename-fixture", &fixture) :
        child(parent, "rename-fixture", &fixture);
    if (!error) error = write ? directory(fixture, "left", &left) : child(fixture, "left", &left);
    if (!error) error = write ? directory(fixture, "right", &right) : child(fixture, "right", &right);
    if (!error && write) {
        error = tagged_file(left, "a", 0xa1, &a);
        if (!error) error = tagged_file(left, "b", 0xb2, &b);
        if (!error && (RENAME(left,"missing",left,"missing",0) != ENOENT ||
            RENAME(left,"a",left,"a",0) != 0 ||
            RENAME(left,"a",left,"a",RENAME_NOREPLACE) != EEXIST ||
            RENAME(left,"a",left,"b",RENAME_NOREPLACE) != EEXIST ||
            RENAME(left,"a",right,"absent",RENAME_EXCHANGE) != ENOENT ||
            zfs_rename(left,"a",right,"x",NULL,0,0,NULL,NULL) != EACCES)) error = EIO;
        cred_t *reader = oz_cred_create(77,77,88,0,NULL);
        if (!reader) error = ENOMEM;
        else {
            int denied = zfs_rename(left,"a",right,"x",reader,0,0,NULL,NULL);
            if (!error && denied != EACCES && denied != EPERM) error = EIO;
            oz_cred_release(reader);
        }
        zilog_t *saved = fs->z_log; fs->z_log = NULL;
        if (!error && RENAME(left,"a",right,"x",0) != ENOTSUP) error = EIO;
        fs->z_log = saved;
        if (!error) error = RENAME(left,"a",right,"moved",0);
        if (!error) error = RENAME(left,"b",right,"moved",0);
        if (!error && (!a->z_unlinked || a->z_links ||
            oz_unlinked_drain_exclusive(fs) != EBUSY)) error = EIO;
        if (!error) error = tag_check(a,0xa1);
        if (a) { oz_znode_rele(a); a = NULL; }
        if (!error) error = oz_unlinked_drain_exclusive(fs);
        if (!error) error = tagged_file(left,"a",0xc3,&c);
        if (!error) error = RENAME(left,"a",right,"moved",RENAME_EXCHANGE);
        if (!error) error = exchange_race(left,"a",right,"moved");
        if (!error) error = replay_exchange(left,right);
        if (!error) error = replace_empty(left,right);
        if (!error) error = directory(left,"tree",&tree);
        if (!error) error = directory(tree,"branch",&branch);
        if (!error && (RENAME(left,"tree",tree,"branch",RENAME_EXCHANGE)!=EINVAL ||
            RENAME(tree,"branch",left,"tree",RENAME_EXCHANGE)!=EINVAL)) error=EIO;
        if (!error) error = missing_exchange(left,left,tree);
        if (!error) error = missing_exchange(left,right,tree);
        if (!error) {
            const char *proof="Native missing directory exchange PASS same-parent=1 cross-parent=1 unchanged=1\n";
            for (const char *p=proof; *p; p++) RawPutChar(*p);
        }
        if (!error) error = directory(left,"sibling",&empty);
        if (!error) error = exchange_race(left,"tree",left,"sibling");
        if (!error) error = sibling_exchange(left,"tree","sibling",tree,empty);
        if (!error) error = exchange_race(left,"a",left,"tree");
        if (!error) error = sibling_exchange(left,"a","tree",b,tree);
        if (!error) error = zfs_rmdir(left,"sibling",NULL,kcred,0);
        if (empty) { oz_znode_rele(empty); empty=NULL; }
        if (!error) error = oz_unlinked_drain_exclusive(fs);
        if (!error) {
            const char *proof="Native sibling directory exchange PASS directories=1 mixed=1 restored=1 opposing-swaps=128\n";
            for (const char *p=proof; *p; p++) RawPutChar(*p);
        }
        if (!error && RENAME(left,"tree",branch,"cycle",0) != EINVAL) error = EIO;
        if (!error) error = ancestry_failure(left,right,tree,right,0);
        if (!error) error = ancestry_failure(left,right,tree,fixture,0);
        if (!error) error = ancestry_failure(left,right,tree,fixture,1);
        if (!error) {
            const char *proof="Native ordinary rename corrupt ancestry PASS unchanged=1\n";
            for (const char *p=proof; *p; p++) RawPutChar(*p);
        }
        if (!error) error = RENAME(left,"tree",right,"tree",0);
        if (!error) {
            const char *proof="Native rename ancestry SA failure/retry result=0\n";
            for (const char *p=proof; *p; p++) RawPutChar(*p);
        }
        if (!error) error = directory(left,"empty",&empty);
        /* Failed replacement must restore the source name and link counts. */
        if (!error && RENAME(left,"empty",right,"tree",0) != ENOTEMPTY) error = EIO;
        for (unsigned mode=0;mode<3 && !error;mode++)
            error=exchange_ancestry_failure(left,right,empty,tree,fixture,mode);
        if (!error) {
            const char *proof="Native exchange ancestry failure PASS forward=1 reverse=1 corrupt-cycle=1 unchanged=1\n";
            for (const char *p=proof;*p;p++) RawPutChar(*p);
        }
        if (!error) error = exchange_race(left,"empty",right,"tree");
        if (!error) error = cross_exchange(left,"empty",right,"tree",empty,tree);
        if (!error) error = exchange_race(left,"a",right,"tree");
        if (!error) error = cross_exchange(left,"a",right,"tree",b,tree);
        if (!error) {
            const char *proof="Native cross-parent directory exchange PASS directories=1 mixed=1 cycles-denied=2 opposing-swaps=128 restored=1\n";
            for (const char *p=proof; *p; p++) RawPutChar(*p);
        }
        if (!error && (RENAME(left,"a",right,"tree",0) != EISDIR ||
            RENAME(left,"empty",right,"moved",0) != ENOTDIR)) error = EIO;
        if (!error) error = zil_commit(fs->z_log,0);
    } else if (!error && RENAME(left,"a",right,"x",0) != EROFS) error = EIO;
    if (b) { oz_znode_rele(b); b = NULL; }
    if (c) { oz_znode_rele(c); c = NULL; }
    if (tree) { oz_znode_rele(tree); tree = NULL; }
    if (empty) { oz_znode_rele(empty); empty = NULL; }
    if (!error) error = child(left,"a",&b);
    if (!error) error = child(right,"moved",&c);
    if (!error) error = child(right,"tree",&tree);
    if (!error) error = child(left,"empty",&empty);
    if (!error) error = tag_check(b,0xb2);
    if (!error) error = tag_check(c,0xc3);
    uint64_t moved_parent = 0, count;
    if (!error) error = sa_lookup(tree->z_sa_hdl,SA_ZPL_PARENT(fs),&moved_parent,8);
    if (!error && (moved_parent != right->z_id || tree->z_links != 3 ||
        tree->z_size != 3 || empty->z_links != 2 || empty->z_size != 2 ||
        b->z_links != 1 || c->z_links != 1 || left->z_links != 3 ||
        right->z_links != 3 || left->z_size != 4 || right->z_size != 4 ||
        zap_contains(fs->z_os,left->z_id,"b") != ENOENT ||
        zap_contains(fs->z_os,left->z_id,"tree") != ENOENT ||
        zap_contains(fs->z_os,right->z_id,"x") != ENOENT)) error = EIO;
    if (!error) error = persistent_exchange(fixture,write);
    if (!error) error = zap_count(fs->z_os,fs->z_unlinkedobj,&count);
    if (!error && count) error = EIO;
    znode_t *nodes[] = {a,b,c,tree,branch,empty,left,right,fixture,parent,root};
    for (unsigned i = 0; i < sizeof (nodes)/sizeof (*nodes); i++)
        if (nodes[i]) oz_znode_rele(nodes[i]);
    char text[100]; snprintf(text,sizeof (text),
        "Native upstream rename/move/replace/exchange write=%d result=%d\n",write,error);
    for (char *p=text; *p; p++) RawPutChar(*p);
    return (error);
}
