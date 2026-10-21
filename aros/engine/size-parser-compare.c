/* SPDX-License-Identifier: CDDL-1.0 */
/* Host differential test only. The original algorithm is extracted from the
 * pinned upstream commit by the runner, not reimplemented in this test. */
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libzutil.h>
typedef struct { char diagnostic[256]; } libzfs_handle_t;
#define TEXT_DOMAIN "test"
#define dgettext(domain, message) (message)
static void zfs_error_aux(libzfs_handle_t *hdl,const char *format,...)
{
    va_list args;va_start(args,format);
    vsnprintf(hdl->diagnostic,sizeof(hdl->diagnostic),format,args);
    va_end(args);
}
#define zfs_nicestrtonum original_nicestrtonum
#include "size-parser-reference.inc"
#undef zfs_nicestrtonum
#include "size-parser-wrapper.inc"
static unsigned cases;
static int compare(const char *value)
{
    for(int with_handle=0;with_handle<2;with_handle++) {
        uint64_t original=123,current=456;
        libzfs_handle_t a={{0}},b={{0}};
        int ra=original_nicestrtonum(with_handle?&a:NULL,value,&original);
        int rb=zfs_nicestrtonum(with_handle?&b:NULL,value,&current);
        ++cases;
        if(ra!=rb||original!=current||strcmp(a.diagnostic,b.diagnostic)) {
            fprintf(stderr,"FAIL [%s] rc %d/%d value %llu/%llu diag [%s]/[%s]\n",
                value,ra,rb,(unsigned long long)original,(unsigned long long)current,
                a.diagnostic,b.diagnostic);return 1;
        }
    }
    return 0;
}
int main(void)
{
    const char *numbers[]={"","0","1","16","1024",".5","1.5","0.001",
        "18446744073709551615","18446744073709551616","18446744073709551616.0",
        "18446744073709551615.0","-1","+1"," 1","1 ",".","1.","1e3","1.0e3",
        "nan","inf","0x10","1,5","99999999999999999999999999999999999999999999"};
    const char *suffixes[]={"","B","b","K","kB","KiB","m","MB","miB","G",
        "GiB","T","TiB","P","PiB","E","EiB","Z","ZiB","BB","BiB","MiBB",
        "MM","garbage"," ","/s"};
    char value[256];
    for(unsigned n=0;n<sizeof(numbers)/sizeof(*numbers);n++)
        for(unsigned s=0;s<sizeof(suffixes)/sizeof(*suffixes);s++) {
            snprintf(value,sizeof(value),"%s%s",numbers[n],suffixes[s]);
            if(compare(value))return 1;
        }
    /* Deterministic values around each suffix's integer overflow boundary. */
    for(unsigned shift=0;shift<=60;shift+=10)
        for(int offset=-2;offset<=2;offset++) {
            uint64_t value64=(UINT64_MAX>>shift)+offset;
            snprintf(value,sizeof(value),"%llu%c",(unsigned long long)value64,"BKMGTPE"[shift/10]);
            if(compare(value))return 1;
        }
    printf("PASS: %u upstream/refactored size conversions, values and diagnostics\n",cases);
    return 0;
}
