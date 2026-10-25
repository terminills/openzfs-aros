/* SPDX-License-Identifier: CDDL-1.0 */
#include "errno-check.h"
#include <stdio.h>

int main(void)
{
    int values[] = {0, EINVAL, EACCES};
    for(unsigned i=0;i<sizeof(values)/sizeof(values[0]);i++) {
        errno=values[i];
        if(!oz_errno_check() || errno!=values[i]) return 1;
    }
    puts("PASS errno probe and saved-errno restoration");
    return 0;
}
