/* SPDX-License-Identifier: CDDL-1.0 */
/* Native lifecycle/I/O around upstream zvol.c. No recreated on-disk format. */
#include <sys/zfs_context.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_tx.h>
#include <sys/dsl_prop.h>
#include <sys/zap.h>
#include <sys/zil.h>
#include <sys/zfs_rlock.h>
#include <sys/dataset_kstats.h>
#include <sys/zvol.h>
#include <sys/zvol_impl.h>
#include <sys/txg.h>
#include <zfs_namecheck.h>
#include "zvol-native.h"
struct zvol_state_os {uint64_t capacity;boolean_t readonly,dying;};
struct oz_zvol_handle {zvol_state_t *volume;boolean_t writable;};
static boolean_t initialized;
static kmutex_t create_lock;
static unsigned registered;

int zvol_init(void)
{
    if(initialized)return EBUSY;
    int error=zvol_init_impl();
    if(!error){mutex_init(&create_lock,NULL,MUTEX_DEFAULT,NULL);initialized=B_TRUE;}
    return error;
}
void zvol_fini(void)
{
    ASSERT(initialized);
    zvol_fini_impl();
    ASSERT0(registered);
    mutex_destroy(&create_lock);initialized=B_FALSE;
}
int zvol_busy(void){return __atomic_load_n(&registered,__ATOMIC_ACQUIRE)!=0;}

void zvol_os_free(zvol_state_t *zv)
{
    ASSERT0(zv->zv_open_count);ASSERT0P(zv->zv_objset);
    ASSERT0P(zv->zv_dn);ASSERT0P(zv->zv_zilog);ASSERT0P(zv->zv_zso);
    dataset_kstats_destroy(&zv->zv_kstat);
    zfs_rangelock_fini(&zv->zv_rangelock);
    rw_destroy(&zv->zv_suspend_lock);
    cv_destroy(&zv->zv_removing_cv);mutex_destroy(&zv->zv_state_lock);
    kmem_free(zv,sizeof(*zv));
}
void zvol_os_remove_minor(zvol_state_t *zv)
{
    ASSERT(MUTEX_HELD(&zv->zv_state_lock));
    ASSERT0(zv->zv_open_count);ASSERT(zv->zv_flags&ZVOL_REMOVING);
    kmem_free(zv->zv_zso,sizeof(*zv->zv_zso));zv->zv_zso=NULL;
    unsigned previous=__atomic_fetch_sub(&registered,1,__ATOMIC_RELEASE);
    ASSERT3U(previous,>,0);
}
void zvol_os_set_capacity(zvol_state_t *zv,uint64_t sectors)
{zv->zv_zso->capacity=sectors<<9;}
void zvol_os_set_disk_ro(zvol_state_t *zv,int readonly)
{zv->zv_zso->readonly=readonly!=0;}
int zvol_os_update_volsize(zvol_state_t *zv,uint64_t size)
{
    ASSERT(MUTEX_HELD(&zv->zv_state_lock));
    zv->zv_zso->capacity=size;zv->zv_changed=1;
    return 0;
}
int zvol_os_rename_minor(zvol_state_t *zv,const char *name)
{
    ASSERT(RW_WRITE_HELD(&zvol_state_lock));
    ASSERT(MUTEX_HELD(&zv->zv_state_lock));
    if(strlen(name)>=sizeof(zv->zv_name))return ENAMETOOLONG;
    if(zv->zv_flags&ZVOL_REMOVING)return ENXIO;
    strlcpy(zv->zv_name,name,sizeof(zv->zv_name));
    hlist_del(&zv->zv_hlink);zv->zv_hash=zvol_name_hash(name);
    hlist_add_head(&zv->zv_hlink,ZVOL_HT_HEAD(zv->zv_hash));
    dataset_kstats_rename(&zv->zv_kstat,name);
    return 0;
}
boolean_t zvol_os_is_zvol(const char *path)
{
    if(!initialized||!path||strncmp(path,"ZVOL:",5)||!path[5])return B_FALSE;
    const char *name=path+5;
    zvol_state_t *zv=zvol_find_by_name_hash(name,zvol_name_hash(name),RW_NONE);
    if(!zv)return B_FALSE;
    boolean_t present=!(zv->zv_flags&ZVOL_REMOVING)&&!zv->zv_zso->dying;
    mutex_exit(&zv->zv_state_lock);return present;
}
void zvol_wait_close(zvol_state_t *zv)
{
    mutex_enter(&zv->zv_state_lock);zv->zv_zso->dying=B_TRUE;
    while(zv->zv_open_count)cv_wait(&zv->zv_removing_cv,&zv->zv_state_lock);
    mutex_exit(&zv->zv_state_lock);
}

int zvol_os_create_minor(const char *name)
{
    if(!initialized)return ENODEV;
    if(!name||dataset_namecheck(name,NULL,NULL))return EINVAL;
    if(strlen(name)>=sizeof(((zvol_state_t *)0)->zv_name))return ENAMETOOLONG;
    if(zvol_inhibit_dev)return 0;
    mutex_enter(&create_lock);
    uint64_t hash=zvol_name_hash(name),mode=0,size=0;
    zvol_state_t *zv=zvol_find_by_name_hash(name,hash,RW_NONE);
    if(zv){mutex_exit(&zv->zv_state_lock);mutex_exit(&create_lock);return EEXIST;}
    int error=dsl_prop_get_integer(name,zfs_prop_to_name(ZFS_PROP_VOLMODE),&mode,NULL);
    if(error)goto done;
    if(mode==ZFS_VOLMODE_DEFAULT)mode=zvol_volmode;
    if(mode==ZFS_VOLMODE_NONE)goto done;
    objset_t *os=NULL;
    error=dmu_objset_own(name,DMU_OST_ZVOL,B_TRUE,B_TRUE,FTAG,&os);
    if(error)goto done;
    dmu_object_info_t info;
    error=dmu_object_info(os,ZVOL_OBJ,&info);
    if(!error)error=zap_lookup(os,ZVOL_ZAP_OBJ,"size",8,1,&size);
    if(!error)error=zvol_check_volsize(size,info.doi_data_block_size);
    if(error){dmu_objset_disown(os,B_TRUE,FTAG);goto done;}
    zv=kmem_zalloc(sizeof(*zv),KM_SLEEP);
    mutex_init(&zv->zv_state_lock,NULL,MUTEX_DEFAULT,NULL);
    cv_init(&zv->zv_removing_cv,NULL,CV_DEFAULT,NULL);
    rw_init(&zv->zv_suspend_lock,NULL,RW_DEFAULT,NULL);
    zfs_rangelock_init(&zv->zv_rangelock,NULL,NULL);
    zv->zv_zso=kmem_zalloc(sizeof(*zv->zv_zso),KM_SLEEP);
    strlcpy(zv->zv_name,name,sizeof(zv->zv_name));zv->zv_hash=hash;
    zv->zv_volmode=mode;zv->zv_volsize=size;zv->zv_volblocksize=info.doi_data_block_size;
    zv->zv_zso->capacity=size;zv->zv_threading=B_TRUE;zv->zv_objset=os;
    if(dmu_objset_is_snapshot(os)||!spa_writeable(dmu_objset_spa(os)))zv->zv_flags|=ZVOL_RDONLY;
    error=dataset_kstats_create(&zv->zv_kstat,os);
    if(!error && !(zv->zv_flags&ZVOL_RDONLY)) {
        zv->zv_zilog=zil_open(os,zvol_get_data,&zv->zv_kstat.dk_zil_sums);
        boolean_t replayed;
        error=zil_replay_checked(os,zv,zvol_replay_vector,&replayed);
        zil_close(zv->zv_zilog);zv->zv_zilog=NULL;
    }
    zv->zv_objset=NULL;dmu_objset_disown(os,B_TRUE,FTAG);
    if(error) {
        kmem_free(zv->zv_zso,sizeof(*zv->zv_zso));zv->zv_zso=NULL;
        zvol_os_free(zv);
    } else {
        rw_enter(&zvol_state_lock,RW_WRITER);
        zvol_insert(zv);__atomic_fetch_add(&registered,1,__ATOMIC_RELAXED);
        rw_exit(&zvol_state_lock);
    }
done:
    mutex_exit(&create_lock);return error;
}

/* An inherited readonly change does not necessarily call zvol_set_ro for
 * this registered child. Resolve the current upstream policy at native
 * admission/I/O boundaries, rather than granting writes from a stale flag.
 * The state-lock -> DSL lookup order matches upstream zvol_setup_zv. */
static int refresh_readonly(zvol_state_t *zv)
{
    ASSERT(MUTEX_HELD(&zv->zv_state_lock));
    if(!zv->zv_objset||(zv->zv_flags&ZVOL_REMOVING)||zv->zv_zso->dying)return ENXIO;
    uint64_t readonly=0;
    int error=dsl_prop_get_integer(zv->zv_name,"readonly",&readonly,NULL);
    if(error)return error;
    readonly=readonly||dmu_objset_is_snapshot(zv->zv_objset)||
        !spa_writeable(dmu_objset_spa(zv->zv_objset));
    zvol_os_set_disk_ro(zv,readonly!=0);
    if(readonly)zv->zv_flags|=ZVOL_RDONLY;
    else zv->zv_flags&=~ZVOL_RDONLY;
    return 0;
}
int oz_zvol_open(const char *name,boolean_t writable,cred_t *caller,struct oz_zvol_handle **out)
{
    if(!out)return EINVAL;
    *out=NULL;
    if(!initialized)return ENODEV;
    if(!caller||secpolicy_zfs(caller))return EACCES;
    if(!name||dataset_namecheck(name,NULL,NULL))return EINVAL;
    struct oz_zvol_handle *h=calloc(1,sizeof(*h));if(!h)return ENOMEM;
    boolean_t unlock=!spa_namespace_held();
    if(unlock&&!spa_namespace_tryenter(FTAG)){free(h);return EBUSY;}
    zvol_state_t *zv=zvol_find_by_name_hash(name,zvol_name_hash(name),RW_READER);
    int error=ENOENT;
    if(zv) {
        boolean_t opened=B_FALSE;
        error=(zv->zv_flags&ZVOL_REMOVING)||zv->zv_zso->dying?ENXIO:0;
        if(!error && zv->zv_open_count==UINT32_MAX)error=EOVERFLOW;
        if(!error && !zv->zv_open_count) {
            error=zvol_first_open(zv,!writable);opened=!error;
        }
        if(!error)error=refresh_readonly(zv);
        if(!error && writable && (zv->zv_flags&ZVOL_RDONLY))error=EROFS;
        if(error&&opened)zvol_last_close(zv);
        if(!error){++zv->zv_open_count;h->volume=zv;h->writable=writable;}
        mutex_exit(&zv->zv_state_lock);rw_exit(&zv->zv_suspend_lock);
    }
    if(unlock)spa_namespace_exit(FTAG);
    if(error)free(h);else *out=h;
    return error;
}
void oz_zvol_close(struct oz_zvol_handle *h)
{
    ASSERT(h!=NULL);zvol_state_t *zv=h->volume;
    rw_enter(&zv->zv_suspend_lock,RW_READER);mutex_enter(&zv->zv_state_lock);
    ASSERT3U(zv->zv_open_count,>,0);
    if(!--zv->zv_open_count){zvol_last_close(zv);cv_broadcast(&zv->zv_removing_cv);}
    mutex_exit(&zv->zv_state_lock);rw_exit(&zv->zv_suspend_lock);free(h);
}
int oz_zvol_info(struct oz_zvol_handle *h,uint64_t *size,uint64_t *block,boolean_t *readonly)
{
    if(!h||!size||!block||!readonly)return EINVAL;
    zvol_state_t *zv=h->volume;mutex_enter(&zv->zv_state_lock);
    int error=refresh_readonly(zv);
    if(!error){
        *size=zv->zv_volsize;*block=zv->zv_volblocksize;
        *readonly=!h->writable||!!(zv->zv_flags&ZVOL_RDONLY);
    }
    mutex_exit(&zv->zv_state_lock);return error;
}
int oz_zvol_io(struct oz_zvol_handle *h,uint64_t offset,void *buffer,size_t length,boolean_t write,size_t *done)
{
    if(!done)return EINVAL;
    *done=0;
    if(!h||(length&&!buffer)||(offset&511)||(length&511))return EINVAL;
    zvol_state_t *zv=h->volume;
    /* Serializing initial native requests also protects lazy ZIL creation.
     * Future asynchronous Exec requests can use shared suspend + range locks. */
    rw_enter(&zv->zv_suspend_lock,RW_WRITER);mutex_enter(&zv->zv_state_lock);
    int error=(zv->zv_flags&ZVOL_REMOVING)||!zv->zv_objset?ENXIO:0;
    if(!error&&write)error=refresh_readonly(zv);
    if(!error && (offset>zv->zv_volsize || length>zv->zv_volsize-offset))error=EINVAL;
    if(!error && write && (!h->writable||(zv->zv_flags&ZVOL_RDONLY)))error=EROFS;
    if(!error && write && length && !zv->zv_zilog) {
        zv->zv_zilog=zil_open(zv->zv_objset,zvol_get_data,&zv->zv_kstat.dk_zil_sums);
        zv->zv_flags|=ZVOL_WRITTEN_TO;
    }
    mutex_exit(&zv->zv_state_lock);
    if(!error && length) {
        zfs_locked_range_t *range=zfs_rangelock_enter(&zv->zv_rangelock,offset,length,write?RL_WRITER:RL_READER);
        while(*done<length && !error) {
            size_t n=MIN(length-*done,DMU_MAX_ACCESS>>1);
            if(write) {
                dmu_tx_t *tx=dmu_tx_create(zv->zv_objset);
                dmu_tx_hold_write_by_dnode(tx,zv->zv_dn,offset+*done,n);
                error=dmu_tx_assign(tx,DMU_TX_WAIT);
                if(error){dmu_tx_abort(tx);break;}
                dmu_write(zv->zv_objset,ZVOL_OBJ,offset+*done,n,(char *)buffer+*done,tx,0);
                zvol_log_write(zv,tx,offset+*done,n,B_TRUE);dmu_tx_commit(tx);
            } else error=dmu_read_by_dnode(zv->zv_dn,offset+*done,n,(char *)buffer+*done,DMU_READ_PREFETCH);
            if(!error)*done+=n;
        }
        zfs_rangelock_exit(range);
        if(write){dataset_kstats_update_write_kstats(&zv->zv_kstat,*done);if(!error)error=zil_commit(zv->zv_zilog,ZVOL_OBJ);}
        else dataset_kstats_update_read_kstats(&zv->zv_kstat,*done);
    }
    rw_exit(&zv->zv_suspend_lock);return error;
}
/* Match upstream zvol_discard: retain partial edge blocks, journal the free,
 * then use upstream DMU range deallocation. No physical/secure TRIM claim. */
int oz_zvol_discard(struct oz_zvol_handle *h,uint64_t offset,uint64_t length)
{
    if(!h||!length||(offset&511)||(length&511))return EINVAL;
    zvol_state_t *zv=h->volume;
    rw_enter(&zv->zv_suspend_lock,RW_WRITER);mutex_enter(&zv->zv_state_lock);
    int error=refresh_readonly(zv);
    if(!error&&(offset>zv->zv_volsize||length>zv->zv_volsize-offset))error=EINVAL;
    if(!error&&(!h->writable||(zv->zv_flags&ZVOL_RDONLY)))error=EROFS;
    uint64_t start=0,end=0;
    if(!error){
        start=P2ROUNDUP(offset,zv->zv_volblocksize);
        end=P2ALIGN_TYPED(offset+length,zv->zv_volblocksize,uint64_t);
        if(start<end&&!zv->zv_zilog){
            zv->zv_zilog=zil_open(zv->zv_objset,zvol_get_data,&zv->zv_kstat.dk_zil_sums);
            zv->zv_flags|=ZVOL_WRITTEN_TO;
        }
    }
    mutex_exit(&zv->zv_state_lock);
    if(!error&&start<end){
        zfs_locked_range_t *range=zfs_rangelock_enter(&zv->zv_rangelock,start,end-start,RL_WRITER);
        dmu_tx_t *tx=dmu_tx_create(zv->zv_objset);dmu_tx_mark_netfree(tx);
        error=dmu_tx_assign(tx,DMU_TX_WAIT);
        if(error)dmu_tx_abort(tx);
        else{
            zvol_log_truncate(zv,tx,start,end-start);dmu_tx_commit(tx);
            error=dmu_free_long_range(zv->zv_objset,ZVOL_OBJ,start,end-start);
        }
        zfs_rangelock_exit(range);
        if(!error)error=zil_commit(zv->zv_zilog,ZVOL_OBJ);
    }
    rw_exit(&zv->zv_suspend_lock);return error;
}
int oz_zvol_flush(struct oz_zvol_handle *h)
{
    if(!h)return EINVAL;
    zvol_state_t *zv=h->volume;rw_enter(&zv->zv_suspend_lock,RW_READER);
    int error=!zv->zv_objset?ENXIO:zv->zv_zilog?zil_commit(zv->zv_zilog,ZVOL_OBJ):0;
    rw_exit(&zv->zv_suspend_lock);return error;
}
