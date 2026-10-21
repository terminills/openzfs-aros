/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_vnops.h>
#include <sys/zfs_dir.h>
#include <sys/zap.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include "native-protection.h"
#include "xattr.h"

int oz_native_protection_test(zfsvfs_t *fs, boolean_t write)
{
    znode_t *root=NULL,*file=NULL;
    int error=zfs_zget(fs,fs->z_root,&root);
    const char *names[]={"native-denied","native-allowed","native-no-search"};
    const uint32_t masks[]={FIBF_READ|FIBF_WRITE|FIBF_EXECUTE|FIBF_DELETE,
        FIBF_READ|FIBF_OTR_READ,0};
    boolean_t old_sa=fs->z_xattr_sa;
    if(write)fs->z_xattr_sa=B_TRUE;
    for(unsigned i=0;i<3&&!error;i++) {
        if(write) {
            vattr_t va={.va_mask=ATTR_MODE,.va_mode=(i==2?S_IFDIR:S_IFREG)|0777};
            if(i==2)error=zfs_mkdir(root,(char *)names[i],&va,&file,kcred,0,NULL,NULL);
            else error=zfs_create(root,(char *)names[i],&va,1,S_IWUSR,&file,kcred,0,NULL,NULL);
            uint32_t bits=123;boolean_t present=B_TRUE;
            if(!error)error=oz_native_protection_get(file,&bits,&present);
            if(!error&&(present||bits))error=EIO;
            if(!error)error=oz_xattr_set(file,OZ_PROTECTION_XATTR,"bad",3,0,kcred);
            if(!error&&oz_native_protection_get(file,&bits,&present)!=EIO)error=EIO;
            if(!error)error=oz_native_protection_store(file,masks[i],kcred);
            if(!error&&oz_native_protection_store(file,0,NULL)!=EACCES)error=EIO;
            if(!error&&i==2) {
                znode_t *child=NULL;
                va.va_mode=S_IFREG|0666;
                error=zfs_create(file,"child",&va,1,S_IWUSR,&child,kcred,0,NULL,NULL);
                if(child)oz_znode_rele(child);
            }
        } else {
            uint64_t entry;
            error=zap_lookup(fs->z_os,fs->z_root,names[i],8,1,&entry);
            if(!error)error=zfs_zget(fs,ZFS_DIRENT_OBJ(entry),&file);
            if(!error&&oz_native_protection_store(file,0,kcred)!=EROFS)error=EIO;
        }
        uint32_t bits;boolean_t present;
        if(!error)error=oz_native_protection_get(file,&bits,&present);
        if(!error&&(!present||bits!=masks[i]))error=EIO;
        if(file)oz_znode_rele(file);
        file=NULL;
    }
    fs->z_xattr_sa=old_sa;
    if(!error&&write)error=zil_commit(fs->z_log,0);
    if(root)oz_znode_rele(root);
    char text[100];snprintf(text,sizeof(text),"Native protection xattr persistence write=%d result=%d\n",write,error);
    for(char *p=text;*p;p++)RawPutChar(*p);
    return error;
}
