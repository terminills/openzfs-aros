/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_ioctl_impl.h>
#include <sys/spa.h>
#include "ioctl-request.h"
#include "ioctl-copy.h"
#include "file-scope.h"
#include "../cli/session-cleanup.h"
#include "../ui/ioctl-api.h"
#include "import-platform.h"
#include "packet-cred.h"
#include "cache-policy.h"
int oz_cli_pool_export_check(const char *,cred_t *);
void oz_cli_pool_export_done(const char *);
int oz_cli_dataset_rename_check(const char *,const char *,int);
int oz_cli_history_check(const char *,uint64_t,cred_t *);
extern uint_t zfs_allow_log_key;

size_t oz_ioctl_request_limit(void)
{
    /* One aggregate allocation cap, not an independent cap for each buffer. */
    return zfs_max_nvlist_src_size_os();
}
static int execute(void *storage,size_t length,const struct oz_packet_security *security,struct oz_session_cleanup *session,int streamfd)
{
    cred_t *cred=security?oz_packet_security_cred(security):NULL;
    if(!storage||!cred||!session)return EACCES;
    if(length<sizeof(struct oz_ioctl_wire)||length>oz_ioctl_request_limit())return EINVAL;
    struct oz_ioctl_wire *w=storage;
    if(w->magic!=OZ_IOCTL_WIRE_MAGIC||w->version!=OZ_IOCTL_WIRE_VERSION||
       w->command_size!=sizeof(zfs_cmd_t)||w->reserved||w->history_descriptor>INT_MAX||w->engine_error||w->descriptor<=0)return EINVAL;
    size_t base=sizeof(*w)+sizeof(zfs_cmd_t);
    if(base>length||w->src_size>length-base)return EINVAL;
    size_t conf=base+(size_t)w->src_size;
    if(w->conf_size>length-conf)return EINVAL;
    size_t history=conf+(size_t)w->conf_size;
    if(w->history_size>OZ_IOCTL_HISTORY_LIMIT||w->history_size>length-history)return EINVAL;
    size_t dst=history+(size_t)w->history_size;
    if(w->dst_capacity!=length-dst)return EINVAL;
    /* Transport expansion is deliberate: never execute an unaudited command
     * just because it shares a numeric ioctl namespace. */
    boolean_t accounting_one=w->command==ZFS_IOC_USERSPACE_ONE;
    boolean_t history_read=w->command==ZFS_IOC_POOL_GET_HISTORY;
    boolean_t history_log=w->command==ZFS_IOC_LOG_HISTORY;
    boolean_t vdevprops_get=w->command==ZFS_IOC_VDEV_GET_PROPS;
    boolean_t vdevprops_set=w->command==ZFS_IOC_VDEV_SET_PROPS;
    boolean_t vdevprops=vdevprops_get||vdevprops_set;
    boolean_t accounting_many=w->command==ZFS_IOC_USERSPACE_MANY;
    boolean_t received_props=w->command==ZFS_IOC_OBJSET_RECVD_PROPS;
    boolean_t error_log=w->command==ZFS_IOC_ERROR_LOG;
    boolean_t object_path=w->command==ZFS_IOC_DSOBJ_TO_DSNAME||w->command==ZFS_IOC_OBJ_TO_PATH;
    boolean_t query=error_log||object_path||received_props||history_read||accounting_one||accounting_many||w->command==ZFS_IOC_POOL_STATS||w->command==ZFS_IOC_POOL_CONFIGS||
        w->command==ZFS_IOC_OBJSET_STATS||w->command==ZFS_IOC_OBJSET_ZPLPROPS||
        w->command==ZFS_IOC_DATASET_LIST_NEXT||
        w->command==ZFS_IOC_GET_FSACL||w->command==ZFS_IOC_SPACE_WRITTEN||
        w->command==ZFS_IOC_OBJ_TO_STATS||w->command==ZFS_IOC_NEXT_OBJ;
    boolean_t diff=streamfd>0&&w->command==ZFS_IOC_DIFF;
    boolean_t sending=streamfd>0&&w->command==ZFS_IOC_SEND_NEW;
    boolean_t receiving=streamfd>0&&w->command==ZFS_IOC_RECV_NEW;
    boolean_t temporary=w->command==ZFS_IOC_TMP_SNAPSHOT;
    boolean_t prefetch=w->command==ZFS_IOC_POOL_PREFETCH;
    boolean_t pruning=w->command==ZFS_IOC_DDT_PRUNE;
    boolean_t initializing=w->command==ZFS_IOC_POOL_INITIALIZE;
    boolean_t trimming=w->command==ZFS_IOC_POOL_TRIM;
    boolean_t bootenv_set=w->command==ZFS_IOC_SET_BOOTENV;
    boolean_t bootenv=bootenv_set||w->command==ZFS_IOC_GET_BOOTENV;
    boolean_t program=w->command==ZFS_IOC_CHANNEL_PROGRAM;
    boolean_t redacting=w->command==ZFS_IOC_REDACT;
    boolean_t events_next=w->command==ZFS_IOC_EVENTS_NEXT;
    boolean_t events_seek=w->command==ZFS_IOC_EVENTS_SEEK;
    boolean_t events_clear=w->command==ZFS_IOC_EVENTS_CLEAR;
    boolean_t events=events_next||events_seek||events_clear;
    boolean_t waiting=streamfd==-2&&
        (w->command==ZFS_IOC_WAIT||w->command==ZFS_IOC_WAIT_FS);
    boolean_t modern=bootenv||vdevprops||history_log||initializing||trimming||redacting||program||pruning||waiting||prefetch||temporary||diff||sending||receiving||w->command==ZFS_IOC_SNAPSHOT||w->command==ZFS_IOC_DESTROY_SNAPS||
        w->command==ZFS_IOC_HOLD||w->command==ZFS_IOC_GET_HOLDS||w->command==ZFS_IOC_RELEASE||
        w->command==ZFS_IOC_CREATE||w->command==ZFS_IOC_CLONE||w->command==ZFS_IOC_ROLLBACK||
        w->command==ZFS_IOC_DESTROY||w->command==ZFS_IOC_SNAPSHOT_LIST_NEXT||
        w->command==ZFS_IOC_POOL_GET_PROPS||w->command==ZFS_IOC_BOOKMARK||
        w->command==ZFS_IOC_GET_BOOKMARKS||w->command==ZFS_IOC_GET_BOOKMARK_PROPS||
        w->command==ZFS_IOC_DESTROY_BOOKMARKS||w->command==ZFS_IOC_POOL_SYNC||
        w->command==ZFS_IOC_POOL_SCRUB||w->command==ZFS_IOC_POOL_REOPEN||
        w->command==ZFS_IOC_POOL_CHECKPOINT||w->command==ZFS_IOC_POOL_DISCARD_CHECKPOINT||
        w->command==ZFS_IOC_SPACE_SNAPS||w->command==ZFS_IOC_SEND_SPACE;
    boolean_t keyop=w->command==ZFS_IOC_LOAD_KEY||w->command==ZFS_IOC_UNLOAD_KEY||
        w->command==ZFS_IOC_CHANGE_KEY;
    boolean_t setprop=w->command==ZFS_IOC_SET_PROP;
    boolean_t poolprop=w->command==ZFS_IOC_POOL_SET_PROPS;
    boolean_t inherit=w->command==ZFS_IOC_INHERIT_PROP;
    boolean_t fsacl=w->command==ZFS_IOC_SET_FSACL;
    boolean_t promote=w->command==ZFS_IOC_PROMOTE;
    boolean_t exporting=w->command==ZFS_IOC_POOL_EXPORT;
    boolean_t destroying=w->command==ZFS_IOC_POOL_DESTROY;
    boolean_t removing=exporting||destroying;
    boolean_t trying=w->command==ZFS_IOC_POOL_TRYIMPORT;
    boolean_t importing=w->command==ZFS_IOC_POOL_IMPORT;
    boolean_t creating=w->command==ZFS_IOC_POOL_CREATE;
    boolean_t vdevstate=w->command==ZFS_IOC_VDEV_SET_STATE;
    boolean_t detaching=w->command==ZFS_IOC_VDEV_DETACH;
    boolean_t renaming=w->command==ZFS_IOC_RENAME;
    boolean_t reguid=w->command==ZFS_IOC_POOL_REGUID;
    boolean_t vdevremove=w->command==ZFS_IOC_VDEV_REMOVE;
    boolean_t clearing=w->command==ZFS_IOC_CLEAR;
    boolean_t adding=w->command==ZFS_IOC_VDEV_ADD;
    boolean_t attaching=w->command==ZFS_IOC_VDEV_ATTACH;
    boolean_t expanding=adding||attaching;
    boolean_t splitting=w->command==ZFS_IOC_VDEV_SPLIT;
    boolean_t upgrading=w->command==ZFS_IOC_POOL_UPGRADE;
    boolean_t accounting_upgrade=w->command==ZFS_IOC_USERSPACE_UPGRADE;
    if(!query&&!modern&&!events&&!keyop&&!setprop&&!poolprop&&!inherit&&!fsacl&&!promote&&!removing&&!trying&&!importing&&!creating&&!vdevstate&&!detaching&&!renaming&&!reguid&&!vdevremove&&!clearing&&!expanding&&!splitting&&!upgrading&&!accounting_upgrade)return ENOTSUP;
    zfs_cmd_t command;
    memcpy(&command,(char *)storage+sizeof(*w),sizeof(command));
    if(command.zc_nvlist_src||command.zc_nvlist_dst||command.zc_nvlist_conf||
       command.zc_history||(!history_read&&command.zc_history_len)||command.zc_iflags||
       command.zc_share.z_exportdata||command.zc_share.z_sharedata||
       command.zc_share.z_sharetype||command.zc_share.z_sharemax||
       command.zc_nvlist_src_size!=w->src_size||command.zc_nvlist_conf_size!=w->conf_size||
       command.zc_nvlist_dst_size!=w->dst_capacity||command.zc_nvlist_dst_filled||
       !memchr(command.zc_name,0,sizeof(command.zc_name)))return EINVAL;
    /* These read-only query commands accept no source/config nvlist. */
    /* Legacy accounting upgrade takes only the filesystem name. Upstream
     * retains version-property authorization and readonly/suspended guards. */
    if(accounting_upgrade&&(w->src_size||w->conf_size||w->dst_capacity||
        w->history_size||command.zc_cookie||command.zc_guid||command.zc_obj||
        command.zc_flags||command.zc_history_offset||command.zc_cleanup_fd||
        command.zc_objset_type||command.zc_value[0]||command.zc_string[0]))return EINVAL;
    if((query&&w->src_size)||(!trying&&!importing&&!creating&&!expanding&&!splitting&&w->conf_size))return EINVAL;
    if(error_log&&(w->dst_capacity%sizeof(zbookmark_phys_t)||w->history_size||
        command.zc_cookie||command.zc_guid||command.zc_obj||command.zc_flags||
        command.zc_history_offset||command.zc_cleanup_fd||
        command.zc_value[0]||command.zc_string[0]))return EINVAL;
    if(object_path&&(w->dst_capacity||w->history_size||command.zc_cookie||
        command.zc_guid||command.zc_flags||command.zc_history_offset||
        command.zc_cleanup_fd||command.zc_string[0]))return EINVAL;
    /* libzfs reuses zc_value from DSOBJ_TO_DSNAME for OBJ_TO_PATH.
     * It is output-only in both commands; discard rather than reject it. */
    if(object_path)memset(command.zc_value,0,sizeof(command.zc_value));
    /* Upstream separates received values from local overrides. This legacy
     * read takes only a dataset name and an output nvlist; retain upstream
     * dataset-read policy and ENOTSUP for datasets with no received values. */
    if(received_props&&(!w->dst_capacity||w->history_size||command.zc_cookie||
        command.zc_guid||command.zc_obj||command.zc_flags||
        command.zc_history_offset||command.zc_cleanup_fd||
        command.zc_value[0]||command.zc_string[0]))return EINVAL;
    if(history_read&&(!w->dst_capacity||command.zc_history_len!=w->dst_capacity||
        w->history_size||command.zc_cookie||command.zc_guid||command.zc_obj||
        command.zc_flags||command.zc_cleanup_fd||command.zc_value[0]||
        command.zc_string[0]))return EINVAL;
    /* Initialization names existing vdev GUIDs in a bounded nvlist. Upstream
     * validates command/schema, config privilege and per-member results.
     * No client pointers, new paths or alternative flags are accepted. */
    if(history_log&&(!w->src_size||
        w->history_size||w->dst_capacity||command.zc_name[0]||
        command.zc_cookie||command.zc_guid||command.zc_obj||command.zc_flags||
        command.zc_history_offset||command.zc_cleanup_fd||
        command.zc_value[0]||command.zc_string[0]))return EINVAL;
    if(vdevprops&&(!w->src_size||!w->dst_capacity||w->history_size||
        command.zc_cookie||command.zc_guid||command.zc_obj||command.zc_flags||
        command.zc_history_offset||command.zc_cleanup_fd||
        command.zc_value[0]||command.zc_string[0]))return EINVAL;
    if(vdevprops_set) {
        /* Path admission is transactional on the service owner. Do not allow
         * another property in the batch to commit before a path rejection. */
        nvlist_t *input=NULL,*props=NULL;
        int e=nvlist_unpack((char *)storage+base,w->src_size,&input,0);
        if(!e&&!nvlist_lookup_nvlist(input,ZPOOL_VDEV_PROPS_SET_PROPS,&props)&&
            nvlist_exists(props,"path")) {
            nvpair_t *first=nvlist_next_nvpair(props,NULL);
            if(first&&nvlist_next_nvpair(props,first))e=EINVAL;
        }
        nvlist_free(input);if(e)return e;
    }
    if(initializing&&(!w->src_size||w->history_size||command.zc_cookie||
        command.zc_guid||command.zc_obj||command.zc_flags||
        command.zc_value[0]||command.zc_string[0]||command.zc_cleanup_fd))return EINVAL;
    /* TRIM names existing GUIDs only. The upstream schema, config privilege,
     * read-only guard and per-vdev capability errors remain authoritative.
     * Admitting this ioctl does not advertise physical discard support. */
    if(trimming&&(!w->src_size||w->history_size||command.zc_cookie||
        command.zc_guid||command.zc_obj||command.zc_flags||
        command.zc_history_offset||command.zc_cleanup_fd||
        command.zc_value[0]||command.zc_string[0]))return EINVAL;
    /* Pool boot-environment nvlists use upstream label storage, schema and
     * config privilege checks. No bootloader installation or raw paths are
     * accepted here. SET retains upstream readonly/suspended-pool guards. */
    if(bootenv&&((bootenv_set&&!w->src_size)||w->history_size||
        command.zc_cookie||command.zc_guid||command.zc_obj||command.zc_flags||
        command.zc_history_offset||command.zc_cleanup_fd||
        command.zc_value[0]||command.zc_string[0]))return EINVAL;
    /* Crypto operations take a bounded packed nvlist, with secret material
     * inside upstream hidden_args. No extra pointers, history or inline
     * operation flags are admitted. Upstream owns schema, delegated load-key/
     * change-key policy, mounted key references and read-only restrictions.
     * Load/unload change memory and remain legal on a read-only pool. */
    if(keyop&&(command.zc_cookie||command.zc_guid||command.zc_obj||
        command.zc_value[0]||command.zc_string[0]||command.zc_flags||w->history_size))return EINVAL;
    /* SEND_SPACE is an upstream dry-run estimate, not stream output. Its
     * optional packed fd is only a progress cookie; no client descriptor is
     * imported. The dispatcher validates nvlist schema and read policy. */
    if((sending||receiving||w->command==ZFS_IOC_SEND_SPACE)&&(command.zc_cookie||command.zc_guid||
        command.zc_obj||command.zc_value[0]||command.zc_string[0]||
        command.zc_flags||w->history_size))return EINVAL;
    /* Accounting uses only inline identity/metric/cursor fields and, for
     * enumeration, a bounded array of upstream zfs_useracct_t records.
     * Keep the upstream per-metric delegated security policy authoritative. */
    if((accounting_one||accounting_many)&&
        (command.zc_objset_type>=ZFS_NUM_USERQUOTA_PROPS||w->history_size||
         !memchr(command.zc_value,0,sizeof(command.zc_value))))return EINVAL;
    if(accounting_one&&(w->dst_capacity||command.zc_cookie))return EINVAL;
    if(accounting_many&&command.zc_value[0])return EINVAL;
    if(splitting&&(!w->conf_size||w->src_size||w->dst_capacity||command.zc_guid||
        command.zc_obj||command.zc_value[0]||command.zc_flags||
        !memchr(command.zc_string,0,sizeof(command.zc_string))))return EINVAL;
    /* Destination remains exported until separately admitted. Automatic
     * import/properties need explicit backing-ownership transfer support. */
    if(splitting&&command.zc_cookie!=ZPOOL_EXPORT_AFTER_SPLIT)return ENOTSUP;
    if(adding&&(!w->conf_size||w->src_size||w->dst_capacity||command.zc_cookie||
        command.zc_guid||command.zc_obj||command.zc_value[0]||command.zc_flags>1))return EINVAL;
    /* Attach/replace supplies one existing GUID and a bounded new-vdev tree.
     * Reuse reserved blank-member admission; upstream validates topology and
     * implements the resilver/rebuild. Successful submission is not completion. */
    if(attaching&&(!w->conf_size||w->src_size||w->dst_capacity||!command.zc_guid||
        command.zc_cookie>1||command.zc_simple>1||command.zc_obj||
        command.zc_value[0]||command.zc_flags))return EINVAL;
    /* Written-space query uses two inline names; byte counts return in the
     * legacy command fields. No nvlist output or caller pointer is involved. */
    if(w->command==ZFS_IOC_SPACE_WRITTEN&&(w->dst_capacity||
        !memchr(command.zc_value,0,sizeof(command.zc_value))))return EINVAL;
    if(creating&&(!w->conf_size||command.zc_cookie||command.zc_guid||command.zc_value[0]||w->dst_capacity))return EINVAL;
    if(importing&&(!w->conf_size||!command.zc_guid||command.zc_value[0]))return EINVAL;
    /* ANY_HOST is zpool import -f's historical-host override, not a
     * bypass of SPA's MMP activity/claim checks. Native admission still
     * requires config privilege and reserves every member below. */
    /* Checkpoint rewind uses the same explicit member reservations and
     * readonly property admission. Upstream distinguishes readonly preview
     * from committing the rewind; do not strip the requested load policy. */
    if(importing&&(command.zc_cookie&~(ZFS_IMPORT_ONLY|ZFS_IMPORT_ANY_HOST|
        ZFS_IMPORT_CHECKPOINT)))return ENOTSUP;
    if(trying&&(!w->conf_size||w->src_size||!w->dst_capacity||
        command.zc_cookie||command.zc_guid||command.zc_value[0]))return EINVAL;
    if(w->history_size&&(!removing||
        memchr((char *)storage+history,0,w->history_size)!=(char *)storage+dst-1))return EINVAL;
    /* Native force/hardforce semantics are not implemented: never bypass a
     * busy DOS handler or silently downgrade the requested operation. */
    if(removing&&(command.zc_cookie||command.zc_guid))return ENOTSUP;
    if(removing&&(w->src_size||w->dst_capacity||command.zc_value[0]))return EINVAL;
    if(renaming&&(w->src_size||w->dst_capacity||command.zc_guid||
        !memchr(command.zc_value,0,sizeof(command.zc_value))))return EINVAL;
    if(renaming&&(command.zc_cookie&~3ULL))return ENOTSUP;
    /* Optional packed GUID property is decoded and authorized upstream. */
    if(reguid&&(w->dst_capacity||command.zc_guid||command.zc_cookie||
        command.zc_obj||command.zc_value[0]))return EINVAL;
    /* Match native Manager clear: admitted pools only, no rewind/recovery
     * policy. Upstream owns configuration permissions and read-only checks. */
    if(clearing&&command.zc_cookie!=ZPOOL_NO_REWIND)return ENOTSUP;
    if(clearing&&(command.zc_obj||command.zc_value[0]))return EINVAL;
    /* Detach cannot admit new paths. Keep the pool's backing reservations
     * until export/destroy, as the native management detach path does. */
    if(detaching&&(w->src_size||w->dst_capacity||!command.zc_guid||
        command.zc_cookie||command.zc_obj||command.zc_value[0]))return EINVAL;
    /* Evacuate an existing GUID or cancel the active evacuation. Retain all
     * backing reservations until export: acceptance is not completion. */
    if(vdevremove&&(w->src_size||w->dst_capacity||command.zc_obj||
        command.zc_value[0]||command.zc_cookie>1||
        (command.zc_cookie?command.zc_guid!=0:command.zc_guid==0)))return EINVAL;
    /* Existing admitted members only, matching Manager online expand=on.
     * Native bindings still bound the accessible extent; this does not admit
     * a replacement path or bypass partition capacity. Spare activation and
     * fault-injection flags remain unsupported. */
    if(vdevstate) {
        if(w->src_size||w->dst_capacity||!command.zc_guid||command.zc_value[0])return EINVAL;
        if(command.zc_cookie==VDEV_STATE_ONLINE) {
            if(command.zc_obj&~ZFS_ONLINE_EXPAND)return ENOTSUP;
        } else if(command.zc_cookie==VDEV_STATE_OFFLINE) {
            if(command.zc_obj&~ZFS_OFFLINE_TEMPORARY)return ENOTSUP;
        } else return ENOTSUP;
    }
    /* Legacy property requests use an inline name and packed nvlist, never
     * additional user pointers. Upstream owns type, property and permission
     * validation; both local and received sources use the real dispatcher. */
    if((setprop||inherit)&&(!memchr(command.zc_value,0,sizeof(command.zc_value))||
       command.zc_cookie>1))return EINVAL;
    if(setprop&&!w->src_size)return EINVAL;
    /* Pool properties are an inline pool name plus one packed nvlist.
     * Preserve upstream pool-modify security and property validation; no
     * caller pointers, received-property cookie or config payload applies. */
    if(poolprop&&(!w->src_size||command.zc_cookie||command.zc_value[0]))return EINVAL;
    if(inherit&&(w->src_size||!command.zc_value[0]))return EINVAL;
    /* Delegation uses a packed upstream ACL and a boolean grant/revoke flag.
     * Upstream verifies the ACL schema and the caller's right to delegate. */
    if(fsacl&&(!w->src_size||command.zc_perm_action>1||command.zc_cookie||
       command.zc_value[0]))return EINVAL;
    /* Legacy promotion takes only an inline dataset name. Upstream returns
     * the conflicting snapshot in zc_string and owns permission checks. */
    if(promote&&(w->src_size||w->dst_capacity||command.zc_cookie||
        command.zc_value[0]||command.zc_string[0]))return EINVAL;
    /* Legacy pool version upgrade takes only name and version cookie.
     * Upstream rejects downgrades/unsupported versions and enforces root. */
    if(upgrading&&(w->src_size||w->dst_capacity||w->history_size||
        command.zc_obj||command.zc_guid||command.zc_flags||command.zc_cleanup_fd||
        command.zc_value[0]||command.zc_string[0]))return EINVAL;
    uint64_t client_cookie=command.zc_cookie;
    /* Event cursors are native control descriptors in the authenticated
     * session, never client POSIX descriptors or pointers. Never block the
     * admission task waiting for a future event; follow polls at the client. */
    if(events){
        if(w->src_size||w->conf_size||w->history_size||command.zc_name[0]||
            command.zc_value[0]||command.zc_string[0]||command.zc_cookie||
            command.zc_obj||command.zc_flags)return EINVAL;
        if(events_next&&(command.zc_guid!=ZEVENT_NONBLOCK||!w->dst_capacity||
            command.zc_cleanup_fd<=0))return EINVAL;
        if(events_seek&&(w->dst_capacity||command.zc_cleanup_fd<=0))return EINVAL;
        if(events_clear&&(w->dst_capacity||command.zc_cleanup_fd||command.zc_guid))return EINVAL;
    }
    /* Upstream validates typed administrative nvlists and permission policy. */
    if((redacting||program||pruning||prefetch||waiting)&&(!w->src_size||w->history_size||command.zc_cookie||
        command.zc_obj||command.zc_guid||command.zc_flags||
        command.zc_value[0]||command.zc_string[0]||command.zc_cleanup_fd))return EINVAL;
    /* Resolve cleanup handles only through this authenticated session's file
     * scope. Upstream validates the control handle and permissions and binds
     * deferred destruction to its on-exit state. */
    if(temporary&&(w->src_size||w->conf_size||w->dst_capacity||w->history_size||
        command.zc_cleanup_fd<=0||command.zc_cookie||command.zc_obj||
        command.zc_guid||command.zc_flags||command.zc_string[0]||
        !command.zc_value[0]||!memchr(command.zc_value,0,sizeof(command.zc_value))))return EINVAL;
    if(diff) {
        if(w->src_size||w->conf_size||w->dst_capacity||w->history_size||
            command.zc_cookie>INT_MAX||command.zc_obj||command.zc_guid||command.zc_flags||
            command.zc_string[0]||!memchr(command.zc_value,0,sizeof(command.zc_value)))return EINVAL;
        command.zc_cookie=streamfd;
    }
    if((w->command==ZFS_IOC_OBJ_TO_STATS||w->command==ZFS_IOC_NEXT_OBJ)&&(w->dst_capacity||w->history_size||
        command.zc_cookie||command.zc_guid||command.zc_flags||command.zc_value[0]||command.zc_string[0]))return EINVAL;
    if(sending||receiving) {
        nvlist_t *input=NULL;char *packed=NULL;size_t packed_size=0;int32_t clientfd;
        const char *fdkey=receiving?"input_fd":"fd";
        int e=nvlist_unpack((char *)storage+base,w->src_size,&input,0);
        if(!e)e=nvlist_lookup_int32(input,fdkey,&clientfd);
        if(!e&&clientfd<0)e=EBADF;
        if(!e)e=nvlist_remove_all(input,fdkey);
        if(!e)e=nvlist_add_int32(input,fdkey,streamfd);
        if(!e)e=nvlist_pack(input,&packed,&packed_size,NV_ENCODE_NATIVE,0);
        if(!e&&packed_size!=w->src_size)e=EPROTO;
        if(!e)memcpy((char *)storage+base,packed,packed_size);
        free(packed);nvlist_free(input);
        if(e)return e;
    }
    zfs_file_t *control=NULL;
    int error=oz_session_file_get(session,w->descriptor,&control);
    if(error)return error;
    minor_t minor;
    error=zfsdev_getminor(control,&minor);
    struct oz_cred_scope *cs=NULL;
    struct oz_caller_scope *caller=NULL;
    struct oz_file_scope *files=NULL;
    struct oz_ioctl_copy_scope *copy=NULL;
    void *import_scope=NULL;
    if(!error)error=oz_cred_push(cred,&cs);
    if(!error)error=oz_caller_enter(oz_session_cleanup_caller(session),&caller);
    if(!error)error=oz_session_file_scope(session,&files);
    struct oz_ioctl_range output={(char *)storage+dst,(size_t)w->dst_capacity,OZ_IOCTL_OUTPUT};
    struct oz_ioctl_range ranges[4];size_t count=0;
    if(w->src_size)ranges[count++]=(struct oz_ioctl_range){(char *)storage+base,(size_t)w->src_size,OZ_IOCTL_INPUT};
    if(w->conf_size)ranges[count++]=(struct oz_ioctl_range){(char *)storage+conf,(size_t)w->conf_size,OZ_IOCTL_INPUT};
    if(w->dst_capacity)ranges[count++]=output;
    if(w->history_size)ranges[count++]=(struct oz_ioctl_range){(char *)storage+history,(size_t)w->history_size,OZ_IOCTL_INPUT};
    if(!error)error=oz_ioctl_copy_enter(ranges,count,&copy);
    if(!error) {
        /* Wipe caller-supplied output bytes before exposing any result. */
        memset(output.base,0,output.size);
        command.zc_nvlist_src=w->src_size?(uintptr_t)((char *)storage+base):0;
        command.zc_nvlist_dst=w->dst_capacity?(uintptr_t)output.base:0;
        command.zc_nvlist_conf=w->conf_size?(uintptr_t)((char *)storage+conf):0;
        command.zc_history=w->history_size?(uintptr_t)((char *)storage+history):0;
        if(history_read){command.zc_history=(uintptr_t)output.base;command.zc_nvlist_dst=0;}
        if(error_log)command.zc_nvlist_dst_size=w->dst_capacity/sizeof(zbookmark_phys_t);
        /* Destroy requires the same native authorization, mounted-subtree
         * exclusion and successful-removal binding cleanup as export. */
        w->engine_error=removing?oz_cli_pool_export_check(command.zc_name,cred):0;
        if(history_read)w->engine_error=oz_cli_history_check(command.zc_name,command.zc_history_offset,cred);
        if(poolprop) {
            nvlist_t *props=NULL;const char *cache=NULL;
            w->engine_error=nvlist_unpack((char *)storage+base,w->src_size,&props,0);
            if(!w->engine_error&&!nvlist_lookup_string(props,"cachefile",&cache)&&!cache[0]) {
                w->engine_error=secpolicy_zfs(cred);
                if(!w->engine_error)w->engine_error=oz_cache_default_check();
            }
            nvlist_free(props);
        }
        if(renaming)w->engine_error=oz_cli_dataset_rename_check(command.zc_name,command.zc_value,command.zc_cookie&3);
        if(destroying&&!w->engine_error) {
            /* Match native management destroy: a read-only SPA can unload
             * successfully without persisting destroyed labels. Do not report
             * that as a successful destructive operation. Export stays legal. */
            spa_t *spa=NULL;
            w->engine_error=spa_open(command.zc_name,&spa,FTAG);
            if(!w->engine_error) {
                if(!spa_writeable(spa))w->engine_error=EROFS;
                spa_close(spa,FTAG);
            }
        }
        if(trying)w->engine_error=oz_cli_tryimport_begin((char *)storage+conf,w->conf_size,cred,security,&import_scope);
        if(creating){
            size_t clean_size=w->conf_size;
            w->engine_error=oz_cli_create_request(command.zc_name,(char *)storage+conf,&clean_size,
                (char *)storage+base,w->src_size,cred,security,&import_scope);
            if(!w->engine_error)command.zc_nvlist_conf_size=clean_size;
        }
        if(adding){
            size_t clean_size=w->conf_size;
            w->engine_error=oz_cli_add_request(command.zc_name,(char *)storage+conf,&clean_size,cred,security,&import_scope);
            if(!w->engine_error)command.zc_nvlist_conf_size=clean_size;
        }
        if(attaching){
            size_t clean_size=w->conf_size;
            w->engine_error=oz_cli_attach_request(command.zc_name,command.zc_guid,command.zc_cookie,
                (char *)storage+conf,&clean_size,cred,security,&import_scope);
            if(!w->engine_error)command.zc_nvlist_conf_size=clean_size;
        }
        if(splitting)w->engine_error=oz_cli_split_begin(command.zc_name,command.zc_string,
            (char *)storage+conf,w->conf_size,cred,&import_scope);
        if(importing) {
            size_t clean_size=w->conf_size;
            w->engine_error=oz_cli_import_begin(command.zc_name,command.zc_guid,
                (char *)storage+conf,&clean_size,(char *)storage+base,w->src_size,cred,security,&import_scope);
            if(!w->engine_error)command.zc_nvlist_conf_size=clean_size;
        }
        if(!w->engine_error) {
            char *pool=NULL;
            int e=w->history_descriptor?oz_session_history_take(session,w->history_descriptor,&pool):0;
            if(e)w->engine_error=e;
            else {
                /* Never inherit a Manager/previous client's TLS. Restore the
                 * service thread's own context even when upstream fails. */
                void *saved=tsd_get(zfs_allow_log_key);
                VERIFY0(tsd_set(zfs_allow_log_key,pool));
                w->engine_error=zfsdev_ioctl_common(w->command-ZFS_IOC_FIRST,&command,FKIOCTL);
                pool=tsd_get(zfs_allow_log_key);
                VERIFY0(tsd_set(zfs_allow_log_key,saved));
                if(w->history_descriptor)oz_session_history_put(session,w->history_descriptor,pool);
                else if(pool)kmem_strfree(pool);
            }
        }
        if(history_read&&!w->engine_error&&command.zc_history_len>w->dst_capacity)
            w->engine_error=EOVERFLOW;
        if(accounting_many&&!w->engine_error){
            if(command.zc_nvlist_dst_size>w->dst_capacity||
                command.zc_nvlist_dst_size%sizeof(zfs_useracct_t))w->engine_error=EOVERFLOW;
            else command.zc_nvlist_dst_filled=B_TRUE; /* raw array, not nvlist */
        }
        if(error_log) {
            if(command.zc_nvlist_dst_size>w->dst_capacity/sizeof(zbookmark_phys_t))
                w->engine_error=EOVERFLOW;
            /* Bytes were bounded by the checked copy scope. On success the
             * whole zero-initialized array is returned, with unused count. */
            command.zc_nvlist_dst_filled=w->engine_error?B_FALSE:B_TRUE;
        }
        if(expanding)oz_cli_add_end(import_scope,w->engine_error);
        else if(splitting)w->engine_error=oz_cli_split_end(import_scope,w->engine_error);
        else if(importing||creating)w->engine_error=oz_cli_import_end(import_scope,w->engine_error);
        else if(import_scope){int denied=oz_cli_tryimport_end(import_scope);if(!w->engine_error)w->engine_error=denied;}
        if(removing&&!w->engine_error)oz_cli_pool_export_done(command.zc_name);
        command.zc_nvlist_src=command.zc_nvlist_dst=command.zc_nvlist_conf=0;
        command.zc_history=command.zc_iflags=0;
        command.zc_nvlist_conf_size=w->conf_size;
        if(diff)command.zc_cookie=client_cookie;
        memcpy((char *)storage+sizeof(*w),&command,sizeof(command));
    }
    if(copy)VERIFY0(oz_ioctl_copy_leave(copy));
    if(files)VERIFY0(oz_file_scope_leave(files));
    if(caller)VERIFY0(oz_caller_leave(caller));
    if(cs)VERIFY0(oz_cred_pop(cs));
    zfs_file_close(control);
    return error;
}
int oz_ioctl_execute(void *storage,size_t length,const struct oz_packet_security *security,struct oz_session_cleanup *session)
{return execute(storage,length,security,session,-1);}
int oz_ioctl_execute_stream(void *storage,size_t length,const struct oz_packet_security *security,struct oz_session_cleanup *session,int fd)
{return fd>0?execute(storage,length,security,session,fd):EINVAL;}
int oz_ioctl_execute_wait(void *storage,size_t length,const struct oz_packet_security *security,struct oz_session_cleanup *session)
{return execute(storage,length,security,session,-2);}
