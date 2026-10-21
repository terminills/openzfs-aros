/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_vnops.h>
#include <sys/zfs_dir.h>
#include <sys/zap.h>
#include <pthread.h>
#include <proto/exec.h>

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
/* Linked only into the private QEMU diagnostics. Publish the owner last;
 * unrelated upstream worker tasks always call the real SA implementation. */
static struct Task *lookup_fault_owner;
static sa_handle_t *lookup_fault_handle;
static sa_attr_type_t lookup_fault_attr;
static unsigned lookup_fault_hits;
int __real_sa_lookup(sa_handle_t *, sa_attr_type_t, void *, uint32_t);
int __wrap_sa_lookup(sa_handle_t *handle, sa_attr_type_t attr, void *buf,
    uint32_t size)
{
    if (__atomic_load_n(&lookup_fault_owner, __ATOMIC_ACQUIRE) == FindTask(NULL) &&
        handle == lookup_fault_handle && attr == lookup_fault_attr) {
        lookup_fault_hits++;
        return EIO;
    }
    return __real_sa_lookup(handle,attr,buf,size);
}
static int ancestry_failure(znode_t *left, znode_t *right, znode_t *tree,
    znode_t *fault_parent)
{
    uint64_t left_size=left->z_size, right_size=right->z_size;
    uint64_t left_links=left->z_links, right_links=right->z_links;
    lookup_fault_handle=fault_parent->z_sa_hdl;
    lookup_fault_attr=SA_ZPL_PARENT(ZTOZSB(fault_parent));
    lookup_fault_hits=0;
    __atomic_store_n(&lookup_fault_owner,FindTask(NULL),__ATOMIC_RELEASE);
    int error=RENAME(left,"tree",right,"tree",0);
    __atomic_store_n(&lookup_fault_owner,NULL,__ATOMIC_RELEASE);
    if (error != EIO || lookup_fault_hits != 1) return EIO;
    znode_t *found=NULL;
    error=child(left,"tree",&found);
    if (!error && (found != tree || left->z_size != left_size ||
        right->z_size != right_size || left->z_links != left_links ||
        right->z_links != right_links ||
        zap_contains(ZTOZSB(right)->z_os,right->z_id,"tree") != ENOENT)) error=EIO;
    if (found) oz_znode_rele(found);
    return error;
}
struct exchange_worker { znode_t *source, *target; char *from, *to; int error; };
static void *exchange_names(void *arg)
{
    struct exchange_worker *w = arg;
    for (unsigned i = 0; i < 32 && !w->error; i++)
        w->error = RENAME(w->source,w->from,w->target,w->to,RENAME_EXCHANGE);
    return (NULL);
}
static int exchange_race(znode_t *left, znode_t *right)
{
    struct exchange_worker workers[2] = {
        {left,right,"a","moved",0}, {right,left,"moved","a",0}
    };
    pthread_t threads[2]; unsigned started = 0; int error = 0;
    for (; started < 2; started++) {
        error = pthread_create(&threads[started],NULL,exchange_names,&workers[started]);
        if (error) break;
    }
    for (unsigned i = 0; i < started; i++) {
        pthread_join(threads[i],NULL);
        if (!error) error = workers[i].error;
    }
    return (error);
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
        if (!error) error = exchange_race(left,right);
        if (!error) error = replace_empty(left,right);
        if (!error) error = directory(left,"tree",&tree);
        if (!error) error = directory(tree,"branch",&branch);
        if (!error && RENAME(left,"tree",branch,"cycle",0) != EINVAL) error = EIO;
        if (!error) error = ancestry_failure(left,right,tree,right);
        if (!error) error = ancestry_failure(left,right,tree,fixture);
        if (!error) error = RENAME(left,"tree",right,"tree",0);
        if (!error) {
            const char *proof="Native rename ancestry SA failure/retry result=0\n";
            for (const char *p=proof; *p; p++) RawPutChar(*p);
        }
        if (!error) error = directory(left,"empty",&empty);
        /* Failed replacement must restore the source name and link counts. */
        if (!error && RENAME(left,"empty",right,"tree",0) != ENOTEMPTY) error = EIO;
        if (!error && (RENAME(left,"empty",right,"tree",RENAME_EXCHANGE) != ENOTSUP ||
            RENAME(left,"a",right,"tree",0) != EISDIR ||
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
