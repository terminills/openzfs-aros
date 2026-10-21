/* SPDX-License-Identifier: CDDL-1.0 */
#include <libzfs.h>
#include <errno.h>
#include <aros/debug.h>
#include <sys/systeminfo.h>
#include <stdlib.h>
#include <string.h>
#include <libzfs_core.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/security.h>
#include <libraries/security.h>
#include "../ui/ioctl-api.h"
#include "../ui/mount-api.h"
#include "../ui/support-api.h"
#include "../ui/admission-api.h"
#include "../../lib/libzfs/libzfs_impl.h"
int oz_upstream_property_ioctl_test(void);

static int bookmark_request(const struct oz_ticket *ticket,LONG fd,int destroy,const char *bookmark,const char *snapshot)
{
    size_t size=0;
    nvlist_t *input=fnvlist_alloc();
    if(destroy)fnvlist_add_boolean(input,bookmark);
    else fnvlist_add_string(input,bookmark,snapshot);
    char *packed=fnvlist_pack(input,&size);
    size_t length=sizeof(struct oz_ioctl_wire)+sizeof(zfs_cmd_t)+size+4096;
    struct oz_ioctl_wire *wire=calloc(1,length);
    if(!wire){fnvlist_pack_free(packed,size);nvlist_free(input);return ENOMEM;}
    wire->session=*ticket;wire->magic=OZ_IOCTL_WIRE_MAGIC;wire->version=OZ_IOCTL_WIRE_VERSION;
    wire->command_size=sizeof(zfs_cmd_t);wire->descriptor=fd;wire->src_size=size;
    wire->dst_capacity=4096;
    wire->command=destroy?ZFS_IOC_DESTROY_BOOKMARKS:ZFS_IOC_BOOKMARK;
    zfs_cmd_t *command=(void *)(wire+1);
    strlcpy(command->zc_name,"public_mirror",sizeof(command->zc_name));
    command->zc_nvlist_src_size=size;
    command->zc_nvlist_dst_size=4096;
    memcpy(command+1,packed,size);
    int error=oz_ioctl_request(OZ_ADMISSION_PORT,wire,length);
    if(!error)error=wire->engine_error;
    oz_ioctl_wire_clear(wire,length);free(wire);
    fnvlist_pack_free(packed,size);nvlist_free(input);return error;
}

static int bookmark_permission(const char *bookmark,const char *snapshot,int allowed)
{
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    if(!secBase)return EACCES;
    struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
    struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    struct TagItem root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    int error=0,opened=0;LONG fd=-1;
    struct oz_ticket ticket={0};
    secLogoutA(quiet);
    if(secLoginA(reader)!=((77UL<<16)|88))error=EACCES;
    /* Keep the existing root libzfs connection separate: this reader must
     * own a new ticket, not exercise rejection of the stale root ticket. */
    if(!error){error=oz_session_open(OZ_ADMISSION_PORT,&ticket);opened=!error;}
    if(!error)error=oz_control_open(OZ_ADMISSION_PORT,&ticket,&fd);
    if(!error) {
        int create=bookmark_request(&ticket,fd,0,"public_mirror/files#denied",snapshot);
        int destroy=bookmark_request(&ticket,fd,1,allowed?"public_mirror/files#denied":bookmark,NULL);
        if(allowed?(create||destroy):
           ((create!=EPERM&&create!=EACCES)||(destroy!=EPERM&&destroy!=EACCES))) {
            bug("Public bookmark authorization FAIL create=%d destroy=%d\n",create,destroy);
            error=EIO;
        }
    }
    if(fd>=0){int closed=oz_control_close(OZ_ADMISSION_PORT,&ticket,fd);if(!error)error=closed;}
    if(opened){int closed=oz_session_close(OZ_ADMISSION_PORT,&ticket);if(!error)error=closed;}
    secLogoutA(quiet);
    if(secLoginA(root)!=secOWNER_SYSTEM)error=EACCES;
    CloseLibrary(secBase);return error;
}

static int bookmark_grant(libzfs_handle_t *hdl,int revoke)
{
    zfs_handle_t *dataset=zfs_open(hdl,"public_mirror/files",ZFS_TYPE_FILESYSTEM);
    if(!dataset)return EIO;
    nvlist_t *acl=fnvlist_alloc(),*permissions=fnvlist_alloc(),*actual=NULL;
    fnvlist_add_boolean(permissions,"bookmark");
    fnvlist_add_boolean(permissions,"destroy");
    fnvlist_add_nvlist(acl,"ul$77",permissions);
    int error=zfs_set_fsacl(dataset,revoke,acl);
    if(!error)error=zfs_get_fsacl(dataset,&actual);
    nvlist_t *local=NULL,*user=NULL;
    if(!error) {
        int found=!nvlist_lookup_nvlist(actual,"public_mirror/files",&local)&&
            !nvlist_lookup_nvlist(local,"ul$77",&user);
        int bookmark=found&&nvlist_exists(user,"bookmark");
        int destroy=found&&nvlist_exists(user,"destroy");
        if(revoke?(bookmark||destroy):(!bookmark||!destroy))error=EIO;
    }
    if(error)bug("Public bookmark delegation FAIL revoke=%d error=%d %s\n",revoke,error,libzfs_error_description(hdl));
    nvlist_free(actual);nvlist_free(permissions);nvlist_free(acl);
    zfs_close(dataset);return error;
}

static int bookmark_query(const char *name,uint64_t *guid)
{
    nvlist_t *props=NULL,*value=NULL;
    int error=lzc_get_bookmark_props(name,&props);
    if(!error)error=nvlist_lookup_nvlist(props,"guid",&value);
    if(!error)error=nvlist_lookup_uint64(value,"value",guid);
    nvlist_free(props);return error;
}

static int snapshot_lifecycle_test(libzfs_handle_t *hdl)
{
    const char *name="public_mirror/files@cli-mounted";
    const char *bookmark="public_mirror/files#cli-mounted";
    zpool_handle_t *pool=zpool_open(hdl,"public_mirror");
    if(!pool)return EIO;
    int enabled=zpool_set_prop(pool,"feature@bookmarks","enabled");
    if(enabled)bug("Public bookmark feature FAIL %s\n",libzfs_error_description(hdl));
    zpool_close(pool);
    if(enabled)return EIO;
    nvlist_t *snaps=fnvlist_alloc(),*errors=NULL;
    fnvlist_add_boolean(snaps,name);
    int error=lzc_snapshot(snaps,NULL,&errors);
    nvlist_free(errors);
    if(error){nvlist_free(snaps);return error;}
    /* Use real client ioctls, not a direct in-engine bookmark call. */
    nvlist_t *marks=fnvlist_alloc();errors=NULL;
    fnvlist_add_string(marks,bookmark,name);
    error=lzc_bookmark(marks,&errors);nvlist_free(errors);errors=NULL;
    uint64_t guid=0,after=0;
    if(!error)error=bookmark_query(bookmark,&guid);
    zfs_handle_t *snapshot=error?NULL:zfs_open(hdl,name,ZFS_TYPE_SNAPSHOT);
    if(!error&&(!snapshot||guid!=zfs_prop_get_int(snapshot,ZFS_PROP_GUID)))error=EIO;
    if(snapshot)zfs_close(snapshot);
    if(!error) {
        int duplicate=lzc_bookmark(marks,&errors);
        if(duplicate!=EEXIST)error=EIO;
        nvlist_free(errors);errors=NULL;
    }
    nvlist_free(marks);
    if(error){bug("Public bookmark create FAIL error=%d\n",error);nvlist_free(snaps);return error;}
    error=bookmark_permission(bookmark,name,0);
    if(!error)error=bookmark_query(bookmark,&after);
    if(!error&&after!=guid)error=EIO;
    if(!error&&bookmark_query("public_mirror/files#denied",&after)!=ESRCH)error=EIO;
    if(error){bug("Public bookmark authorization FAIL error=%d\n",error);nvlist_free(snaps);return error;}
    bug("Public upstream bookmark authorization PASS own-session=1 create-denied=1 destroy-denied=1 unchanged=1\n");
    error=bookmark_grant(hdl,0);
    if(!error)error=bookmark_permission(bookmark,name,1);
    if(!error&&bookmark_query("public_mirror/files#denied",&after)!=ESRCH)error=EIO;
    int revoked=bookmark_grant(hdl,1);
    if(!error)error=revoked;
    if(!error)error=bookmark_permission(bookmark,name,0);
    if(!error)error=bookmark_query(bookmark,&after);
    if(!error&&after!=guid)error=EIO;
    if(error){nvlist_free(snaps);return error;}
    bug("Public upstream bookmark delegation PASS grant=1 fresh-acl=1 delegated-create=1 delegated-destroy=1 revoke=1 denied-again=1\n");
    const char *stage="attach snapshot";
    error=oz_mount_control(OZ_ADMISSION_PORT,name,"OZCLISNAPSHOT",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY);
    if(!error) {
        stage="busy snapshot";
        BPTR lock=Lock((CONST_STRPTR)"OZCLISNAPSHOT:",SHARED_LOCK);
        if(!lock)error=EIO;
        else {
            int destroyed=zfs_destroy_snaps_nvl(hdl,snaps,B_FALSE);
            int still_exists=lzc_exists(name);
            ULONG supported=0,enabled=0;
            int still_mounted=oz_mount_policy_query(OZ_ADMISSION_PORT,name,&supported,&enabled);
            UnLock(lock);
            if(destroyed!=-1||!still_exists||still_mounted||!(enabled&OZ_POLICY_READONLY))error=EIO;
        }
    }
    if(!error){stage="destroy snapshot";error=zfs_destroy_snaps_nvl(hdl,snaps,B_FALSE);}
    if(!error) {
        stage="snapshot absent";
        ULONG supported=0,enabled=0;
        if(lzc_exists(name)||oz_mount_policy_query(OZ_ADMISSION_PORT,name,&supported,&enabled)!=ENOENT)error=EIO;
    }
    nvlist_free(snaps);
    if(!error){stage="surviving bookmark";error=bookmark_query(bookmark,&after);}
    if(!error&&guid!=after)error=EIO;
    nvlist_t *props=fnvlist_alloc(),*listed=NULL;
    fnvlist_add_boolean(props,"guid");
    if(!error){stage="list bookmark";error=lzc_get_bookmarks("public_mirror/files",props,&listed);}
    if(!error&&!nvlist_exists(listed,"cli-mounted"))error=EIO;
    nvlist_free(listed);nvlist_free(props);
    marks=fnvlist_alloc();fnvlist_add_boolean(marks,bookmark);
    if(!error){stage="destroy bookmark";error=lzc_destroy_bookmarks(marks,&errors);nvlist_free(errors);errors=NULL;}
    if(!error){stage="bookmark absent";int absent=bookmark_query(bookmark,&after);
        /* Upstream distinguishes an absent bookmark (ESRCH) from an absent
         * containing dataset (ENOENT). Do not translate the engine result. */
        if(absent!=ESRCH){bug("Public bookmark absent actual=%d expected=%d\n",absent,ESRCH);error=EIO;}}
    props=fnvlist_alloc();listed=NULL;
    if(!error){stage="empty bookmark listing";error=lzc_get_bookmarks("public_mirror/files",props,&listed);}
    if(!error&&nvlist_exists(listed,"cli-mounted"))error=EIO;
    nvlist_free(listed);nvlist_free(props);
    nvlist_free(marks);
    if(!error)bug("Public upstream bookmark lifecycle PASS create=1 duplicate=1 guid=1 list=1 survives-snapshot=1 destroy=1 absent=1\n");
    else bug("Public bookmark lifecycle FAIL stage=%s error=%d\n",stage,error);
    if(!error)bug("Public upstream snapshot lifecycle PASS busy-preserved=1 detach=1 destroy=1 absent=1\n");
    return error;
}

static int promotion_denied(const char *name)
{
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    if(!secBase)return EACCES;
    struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
    struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    struct TagItem root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    struct oz_ticket ticket={0};LONG fd=-1;int opened=0,error=0;
    secLogoutA(quiet);
    if(secLoginA(reader)!=((77UL<<16)|88))error=EACCES;
    if(!error){error=oz_session_open(OZ_ADMISSION_PORT,&ticket);opened=!error;}
    if(!error)error=oz_control_open(OZ_ADMISSION_PORT,&ticket,&fd);
    size_t length=sizeof(struct oz_ioctl_wire)+sizeof(zfs_cmd_t);
    struct oz_ioctl_wire *wire=calloc(1,length);
    if(!wire&&!error)error=ENOMEM;
    if(!error) {
        wire->session=ticket;wire->magic=OZ_IOCTL_WIRE_MAGIC;
        wire->version=OZ_IOCTL_WIRE_VERSION;wire->command_size=sizeof(zfs_cmd_t);
        wire->descriptor=fd;wire->command=ZFS_IOC_PROMOTE;
        zfs_cmd_t *command=(void *)(wire+1);
        strlcpy(command->zc_name,name,sizeof(command->zc_name));
        error=oz_ioctl_request(OZ_ADMISSION_PORT,wire,length);
        if(!error)error=wire->engine_error;
        error=(error==EACCES||error==EPERM)?0:EIO;
    }
    if(wire){oz_ioctl_wire_clear(wire,length);free(wire);}
    if(fd>=0){int closed=oz_control_close(OZ_ADMISSION_PORT,&ticket,fd);if(!error)error=closed;}
    if(opened){int closed=oz_session_close(OZ_ADMISSION_PORT,&ticket);if(!error)error=closed;}
    secLogoutA(quiet);
    if(secLoginA(root)!=secOWNER_SYSTEM)error=EACCES;
    CloseLibrary(secBase);return error;
}

static int native_origin(const char *name,const char *expected)
{
    struct oz_client client;
    struct oz_message *message=calloc(1,sizeof(*message));
    if(!message)return ENOMEM;
    if(!oz_client_open(&client)){free(message);return ENOMEM;}
    message->operation=40;
    strlcpy(message->target,name,sizeof(message->target));
    strcpy(message->argument,"origin");
    int error=oz_submit(&client,message)==OZ_OK?0:EIO;
    if(!error) {
        struct oz_message *reply;
        while(!(reply=oz_poll(&client)))Delay(1);
        if(reply->status!=OZ_OK||reply->row_count!=1||
           strcmp(reply->rows[0].value,expected?expected:"-")) {
            bug("Public native origin FAIL dataset=%s status=%u rows=%u value=%s detail=%s\n",
                name,reply->status,reply->row_count,reply->rows[0].value,reply->detail);
            error=EIO;
        }
        oz_message_free(reply);
    }
    oz_client_close(&client);free(message);return error;
}

static int promotion_origin(libzfs_handle_t *hdl,const char *name,const char *expected)
{
    zfs_handle_t *dataset=zfs_open(hdl,name,ZFS_TYPE_FILESYSTEM);
    if(!dataset)return EIO;
    if(!expected) {
        /* An independent dataset has no origin property string; inspect
         * fresh upstream objset metadata rather than expecting UI text. */
        int error=dataset->zfs_dmustats.dds_origin[0]?EIO:0;
        zfs_close(dataset);return error?error:native_origin(name,expected);
    }
    char origin[ZFS_MAX_DATASET_NAME_LEN];
    int error=zfs_prop_get(dataset,ZFS_PROP_ORIGIN,origin,sizeof(origin),NULL,NULL,0,B_TRUE);
    if(!error&&strcmp(origin,expected))error=EIO;
    zfs_close(dataset);return error?error:native_origin(name,expected);
}

static int promotion_lifecycle_test(libzfs_handle_t *hdl)
{
    const char *source="public_mirror/promote-source",*clone="public_mirror/promote-clone";
    const char *origin="public_mirror/promote-source@seed",*moved="public_mirror/promote-clone@seed";
    const char *stage="create";char conflict[ZFS_MAX_DATASET_NAME_LEN]={0};
    int error=lzc_create(source,LZC_DATSET_TYPE_ZFS,NULL,NULL,0);
    nvlist_t *initial=fnvlist_alloc(),*snapshot_errors=NULL;
    fnvlist_add_boolean(initial,origin);
    if(!error){stage="snapshot";error=lzc_snapshot(initial,NULL,&snapshot_errors);}
    nvlist_free(snapshot_errors);nvlist_free(initial);
    if(!error){stage="clone";error=lzc_clone(clone,origin,NULL);}
    if(!error){stage="initial origin";error=promotion_origin(hdl,clone,origin);}
    if(!error){stage="permission";error=promotion_denied(clone);}
    if(!error){stage="denied unchanged";error=promotion_origin(hdl,clone,origin);}
    if(!error) {
        stage="mounted snapshot protection";
        error=oz_mount_control(OZ_ADMISSION_PORT,origin,"OZPROMOTEBUSY",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY);
        if(!error) {
            BPTR lock=Lock((CONST_STRPTR)"OZPROMOTEBUSY:",SHARED_LOCK);
            int promoted=lzc_promote(clone,conflict,sizeof(conflict));
            BPTR still=Lock((CONST_STRPTR)"OZPROMOTEBUSY:",SHARED_LOCK);
            if(promoted!=EBUSY||!lock||!still) {
                bug("Public mounted promotion actual=%d lock=%d still=%d\n",promoted,lock!=0,still!=0);
                error=EIO;
            }
            if(still)UnLock(still);
            if(lock)UnLock(lock);
            int detached=oz_mount_control(OZ_ADMISSION_PORT,origin,"",OZ_MOUNT_DETACH,0);
            if(!error)error=detached;
        }
    }
    if(!error){stage="mounted refusal unchanged";error=promotion_origin(hdl,clone,origin);}
    if(!error)bug("Public upstream promotion mount guard PASS busy=1 endpoint-retained=1 origin-unchanged=1\n");
    initial=fnvlist_alloc();snapshot_errors=NULL;fnvlist_add_boolean(initial,moved);
    if(!error){stage="conflict snapshot";error=lzc_snapshot(initial,NULL,&snapshot_errors);}
    nvlist_free(snapshot_errors);nvlist_free(initial);
    if(!error){stage="conflict rejection";int result=lzc_promote(clone,conflict,sizeof(conflict));
        if(result!=EEXIST||!strstr(conflict,"seed"))error=EIO;}
    nvlist_t *snaps=fnvlist_alloc(),*errors=NULL;
    fnvlist_add_boolean(snaps,moved);
    if(!error){stage="remove conflict";error=lzc_destroy_snaps(snaps,B_FALSE,&errors);}
    nvlist_free(errors);nvlist_free(snaps);
    if(!error){stage="promote";error=lzc_promote(clone,conflict,sizeof(conflict));}
    if(!error){stage="new origin";error=promotion_origin(hdl,source,moved);}
    if(!error){stage="clone independent";error=promotion_origin(hdl,clone,NULL);}
    if(!error){stage="second promotion invalid";if(lzc_promote(clone,conflict,sizeof(conflict))!=EINVAL)error=EIO;}
    if(!error){stage="destroy old origin";error=lzc_destroy(source);}
    snaps=fnvlist_alloc();errors=NULL;fnvlist_add_boolean(snaps,moved);
    if(!error){stage="destroy transferred snapshot";error=lzc_destroy_snaps(snaps,B_FALSE,&errors);}
    nvlist_free(errors);nvlist_free(snaps);
    if(!error){stage="destroy promoted";error=lzc_destroy(clone);}
    if(!error&&(zfs_dataset_exists(hdl,source,ZFS_TYPE_FILESYSTEM)||
       zfs_dataset_exists(hdl,clone,ZFS_TYPE_FILESYSTEM)))error=EIO;
    if(error)bug("Public promotion FAIL stage=%s error=%d conflict=%s\n",stage,error,conflict);
    else bug("Public upstream promotion PASS denied=1 unchanged=1 conflict=1 promote=1 origins=1 repeat-invalid=1 cleanup=1\n");
    return error;
}

int oz_upstream_promotion_readonly_test(int readonly)
{
    const char *source="public_mirror/promote-ro-source";
    const char *clone="public_mirror/promote-ro-clone";
    const char *snapshot="public_mirror/promote-ro-source@seed";
    static uint64_t saved_guid;
    libzfs_handle_t *hdl=libzfs_init();
    if(!hdl)return errno?errno:EIO;
    int error=0;
    if(!readonly) {
        error=lzc_create(source,LZC_DATSET_TYPE_ZFS,NULL,NULL,0);
        nvlist_t *snaps=fnvlist_alloc(),*errors=NULL;
        fnvlist_add_boolean(snaps,snapshot);
        if(!error)error=lzc_snapshot(snaps,NULL,&errors);
        nvlist_free(errors);nvlist_free(snaps);
        if(!error)error=lzc_clone(clone,snapshot,NULL);
    } else {
        char conflict[ZFS_MAX_DATASET_NAME_LEN]={0};
        int denied=lzc_promote(clone,conflict,sizeof(conflict));
        if(denied!=EROFS) {
            bug("Public readonly promotion unexpected error=%d\n",denied);
            error=EIO;
        }
    }
    if(!error)error=promotion_origin(hdl,clone,snapshot);
    if(!error)error=promotion_origin(hdl,source,NULL);
    zfs_handle_t *snap=error?NULL:zfs_open(hdl,snapshot,ZFS_TYPE_SNAPSHOT);
    if(!error&&!snap)error=EIO;
    if(snap) {
        uint64_t guid=zfs_prop_get_int(snap,ZFS_PROP_GUID);
        if(readonly){if(!saved_guid||saved_guid!=guid)error=EIO;}
        else saved_guid=guid;
        zfs_close(snap);
    }
    if(!error&&zfs_dataset_exists(hdl,"public_mirror/promote-ro-clone@seed",ZFS_TYPE_SNAPSHOT))error=EIO;
    libzfs_fini(hdl);
    if(error)bug("Public readonly promotion FAIL phase=%d error=%d\n",readonly,error);
    else if(readonly)bug("Public upstream readonly promotion PASS reimport=1 denied=1 origins-unchanged=1 snapshot-guid=1\n");
    return error;
}

int oz_upstream_mount_policy_test(void)
{
    libzfs_handle_t *hdl=libzfs_init();
    if(!hdl)return errno?errno:EIO;
    zfs_handle_t *dataset=zfs_open(hdl,"public_mirror/files",ZFS_TYPE_FILESYSTEM);
    int error=dataset?zfs_mount_setattr(dataset,ZFS_MNT_PROP_READONLY|
        ZFS_MNT_PROP_EXEC|ZFS_MNT_PROP_ATIME|ZFS_MNT_PROP_RELATIME):EIO;
    if(!error) {
        errno=0;
        if(zfs_mount_setattr(dataset,ZFS_MNT_PROP_DEVICES)!=-1||errno!=ENOTSUP||
           libzfs_errno(hdl)!=EZFS_MOUNTFAILED)error=EIO;
    }
    if(dataset)zfs_close(dataset);
    libzfs_fini(hdl);return error;
}

int oz_upstream_mount_test(void)
{
    ULONG engine_hostid=0;
    int identity_error=oz_hostid_query(OZ_ADMISSION_PORT,&engine_hostid);
    if(identity_error)return identity_error;
    const char *previous=getenv("ZFS_HOSTID");
    char *saved=previous?strdup(previous):NULL;
    if(previous&&!saved)return ENOMEM;
    if(setenv("ZFS_HOSTID","0x12345678",1)){free(saved);return errno;}
    unsigned long client_hostid=get_system_hostid();
    int restored=saved?setenv("ZFS_HOSTID",saved,1):unsetenv("ZFS_HOSTID");
    free(saved);
    if(restored)return errno;
    if(client_hostid!=engine_hostid)return EIO;
    bug("Public upstream hostid PASS service-identity=1 client-environment-ignored=1\n");
    int property_error=oz_upstream_property_ioctl_test();
    if(property_error)return property_error;
    libzfs_handle_t *hdl=libzfs_init();
    if(!hdl)return errno?errno:EIO;
    zfs_handle_t *dataset=zfs_open(hdl,"public_mirror/files",ZFS_TYPE_FILESYSTEM);
    if(!dataset){bug("Upstream mount zfs_open failed: %s\n",libzfs_error_description(hdl));libzfs_fini(hdl);return EIO;}
    int error=do_mount(dataset,"OZCLIMOUNT:","noexec",0);
    if(error!=ENOTSUP)error=EIO;
    else error=zfs_mount_at(dataset,"rw",0,"OZCLIMOUNT:");
    if(!error)error=oz_upstream_mount_policy_test();
    if(!error) {
        differ_info_t info={.zhp=dataset,.ds="public_mirror/files",.shares=UINT64_MAX};
        error=find_shares_object(&info);
        if(!error&&info.shares!=0)error=EIO;
        if(!error)bug("Public upstream shares object PASS native-metadata=1 absent=1\n");
    }
    if(!error) {
        int duplicate=do_mount(dataset,"OZCLIDUP:","rw",0);
        int removed=do_unmount(dataset,"ozclimount:",0);
        if(duplicate!=EEXIST||removed)error=EIO;
    }
    if(!error)error=snapshot_lifecycle_test(hdl);
    if(!error)error=promotion_lifecycle_test(hdl);
    zfs_close(dataset);libzfs_fini(hdl);
    if(!error)bug("Public upstream mount adapter PASS real-handle=1 mount=1 unmount=1 unsupported=1 duplicate=1\n");
    return error;
}
