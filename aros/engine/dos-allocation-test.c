/* SPDX-License-Identifier: CDDL-1.0 */
/* Linked only into private diagnostics; only dos-handler.c calls this shim. */
#include <proto/exec.h>
#include <stdlib.h>
#include <stddef.h>
#include <errno.h>
#include "zpl-fixture.h"

static struct Task *owner;
static unsigned remaining,hits,fail_create,fail_publish;

void oz_dos_test_fail_allocation(unsigned nth)
{
    owner=FindTask(NULL);remaining=nth;hits=0;fail_create=0;fail_publish=0;
}

void oz_dos_test_fail_creation(void)
{owner=FindTask(NULL);remaining=0;hits=0;fail_create=1;fail_publish=0;}

void oz_dos_test_fail_publication(void)
{owner=FindTask(NULL);remaining=0;hits=0;fail_create=0;fail_publish=1;}

void oz_dos_test_fail_after_publication(void)
{owner=FindTask(NULL);remaining=0;hits=0;fail_create=0;fail_publish=2;}

unsigned oz_dos_test_allocation_hits(void)
{
    remaining=0;fail_create=0;fail_publish=0;owner=NULL;return hits;
}

void *oz_dos_test_calloc(size_t count,size_t size)
{
    if(owner==FindTask(NULL)&&remaining&&!--remaining){hits++;return NULL;}
    return calloc(count,size);
}

int oz_dos_test_create_as(struct oz_zpl *fs,uint64_t parent,const char *name,
    uint32_t protection,struct cred *cr,struct oz_zpl_info *info)
{
    if(owner==FindTask(NULL)&&fail_create){fail_create=0;hits++;return ENOSPC;}
    return oz_zpl_create_as(fs,parent,name,protection,cr,info);
}

struct publication_test {int (*check)(void *);void *context;};
static int fail_publication_check(void *context)
{
    struct publication_test *test=context;
    int rc=test->check(test->context);
    if(rc)return rc;
    hits++;return ENOSPC;
}
int oz_dos_test_replace_as(struct oz_zpl *fs,uint64_t parent,const char *source,
    const char *target,const struct oz_zpl_info *fresh,const struct oz_zpl_info *old,
    struct cred *cr,int (*check)(void *),void *context)
{
    if(owner==FindTask(NULL)&&fail_publish) {
        unsigned mode=fail_publish;
        fail_publish=0;
        if(mode==2) {
            int rc=oz_zpl_replace_as(fs,parent,source,target,fresh,old,cr,check,context);
            if(rc)return rc;
            /* Model the caller-visible error boundary after publication.
             * This does not simulate a real device flush failure. */
            hits++;return ENOSPC;
        }
        struct publication_test test={check,context};
        /* Fail inside real rename, after locking and identity validation. */
        return oz_zpl_replace_as(fs,parent,source,target,fresh,old,cr,
            fail_publication_check,&test);
    }
    return oz_zpl_replace_as(fs,parent,source,target,fresh,old,cr,check,context);
}
