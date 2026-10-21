/* SPDX-License-Identifier: CDDL-1.0 */
/* Real upstream objset accessors and native mount references, no pool I/O. */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_ioctl_impl.h>
#include <sys/dmu_objset.h>
#include <aros/debug.h>
#define CHECK(x) do { if (!(x)) { bug("RESULT native CLI mount refs FAIL line=%d\n",__LINE__); return 20; } } while (0)
struct worker { objset_t *os; pthread_mutex_t lock; pthread_cond_t condition;
    int ready,release,error; };
static zfsvfs_t *acquire(objset_t *os,int *error)
{
    mutex_enter(&os->os_user_ptr_lock);
    zfsvfs_t *fs=dmu_objset_get_user(os);
    *error=zfs_vfs_ref(&fs);
    mutex_exit(&os->os_user_ptr_lock);
    return fs;
}
static void *borrower(void *arg)
{
    struct worker *w=arg;
    zfsvfs_t *fs=acquire(w->os,&w->error);
    pthread_mutex_lock(&w->lock);
    w->ready=1;pthread_cond_signal(&w->condition);
    while(!w->release)pthread_cond_wait(&w->condition,&w->lock);
    pthread_mutex_unlock(&w->lock);
    if(!w->error)zfs_vfs_rele(fs);
    return NULL;
}
int main(void)
{
    objset_t *os=calloc(1,sizeof(*os));
    zfsvfs_t *fs=calloc(1,sizeof(*fs)),*other=calloc(1,sizeof(*other));
    CHECK(os&&fs&&other);fs->z_os=os;other->z_os=os;
    mutex_init(&os->os_user_ptr_lock,NULL,MUTEX_DEFAULT,NULL);
    oz_mount_refs_init(fs);oz_mount_refs_init(other);
    for(unsigned i=0;i<64;i++) {
        int error=0;
        CHECK(acquire(os,&error)==NULL&&error==ESRCH);
        CHECK(!oz_mount_publish(fs));
        CHECK(oz_mount_publish(fs)==EBUSY&&oz_mount_publish(other)==EBUSY);
        CHECK(zfs_vfs_held(fs));
        CHECK(acquire(os,&error)==fs&&!error);
        CHECK(oz_mount_unpublish(fs)==EBUSY);
        /* Busy detach must preserve publication, not hide a live mount. */
        CHECK(acquire(os,&error)==fs&&!error);
        zfs_vfs_rele(fs);zfs_vfs_rele(fs);
        CHECK(!oz_mount_unpublish(fs));
        CHECK(!zfs_vfs_held(fs));
        CHECK(acquire(os,&error)==NULL&&error==ESRCH);
    }
    for(unsigned i=0;i<16;i++) {
        CHECK(!oz_mount_publish(fs));
        struct worker w={.os=os};pthread_t thread;
        CHECK(!pthread_mutex_init(&w.lock,NULL));
        CHECK(!pthread_cond_init(&w.condition,NULL));
        CHECK(!pthread_create(&thread,NULL,borrower,&w));
        pthread_mutex_lock(&w.lock);
        while(!w.ready)pthread_cond_wait(&w.condition,&w.lock);
        CHECK(!w.error&&oz_mount_unpublish(fs)==EBUSY);
        /* Model the unpublished interval of objset replacement. A retained
         * borrower must release without dereferencing any objset. This is
         * lifetime isolation, not a substitute for real rollback testing. */
        mutex_enter(&os->os_user_ptr_lock);
        dmu_objset_set_user(os,NULL);
        fs->z_os=NULL;
        mutex_exit(&os->os_user_ptr_lock);
        CHECK(zfs_vfs_held(fs));
        w.release=1;pthread_cond_signal(&w.condition);
        pthread_mutex_unlock(&w.lock);
        CHECK(!pthread_join(thread,NULL));
        mutex_enter(&os->os_user_ptr_lock);
        fs->z_os=os;
        dmu_objset_set_user(os,fs);
        mutex_exit(&os->os_user_ptr_lock);
        CHECK(!oz_mount_unpublish(fs));
        CHECK(!pthread_cond_destroy(&w.condition));
        CHECK(!pthread_mutex_destroy(&w.lock));
    }
    oz_mount_refs_fini(other);oz_mount_refs_fini(fs);
    mutex_destroy(&os->os_user_ptr_lock);free(other);free(fs);free(os);
    bug("RESULT native CLI mount refs PASS runs=64 threads=16\n");
    return 0;
}
