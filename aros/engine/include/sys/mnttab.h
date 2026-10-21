#ifndef OPENZFS_AROS_MNTTAB_H
#define OPENZFS_AROS_MNTTAB_H
/* Native metadata and non-mutating option lookup. Mount records come from
 * the resident service snapshot, not a Linux /proc or getmntent facade. */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
/* Bounded CLI option buffer; not a fabricated mount-table file. */
#define MNT_LINE_MAX 4096
struct mnttab {
    char *mnt_special, *mnt_mountp, *mnt_fstype, *mnt_mntopts;
};
/* Return a pointer into the original option string, without modifying it.
 * Match complete option names, not misleading prefixes such as ro/rollback. */
static inline char *hasmntopt(struct mnttab *mnt,const char *option)
{
    if(!mnt||!mnt->mnt_mntopts||!option||!*option)return NULL;
    size_t n=strlen(option);
    for(char *p=mnt->mnt_mntopts;*p;) {
        while(isspace((unsigned char)*p))++p;
        char *end=strchr(p,',');if(!end)end=p+strlen(p);
        size_t size=(size_t)(end-p);
        if(size>=n&&!memcmp(p,option,n)&&(size==n||p[n]=='='))return p;
        p=*end?end+1:end;
    }
    return NULL;
}
#endif
