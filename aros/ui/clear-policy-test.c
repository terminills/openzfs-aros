/* SPDX-License-Identifier: CDDL-1.0 */
#include "clear-policy.h"
#include <stdio.h>
int main(void)
{
    const char *valid[]={"recovery=safe","recovery=extreme","recovery=safe;assessment=on","assessment=off;recovery=extreme"};
    const char *invalid[]={"","assessment=on","recovery=off","recovery=safe;","recovery=safe;recovery=extreme","recovery=safe;assessment=on;assessment=off","recovery=safe;force=on","recovery=safe;assessment=maybe"," recovery=safe"};
    for(unsigned i=0;i<sizeof(valid)/sizeof(*valid);i++) {
        struct oz_clear_policy p={9,9};
        if(!oz_parse_clear_policy(valid[i],&p)||p.extreme!=(i==1||i==3)||p.assessment!=(i==2))return 1;
    }
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++) {
        struct oz_clear_policy p={9,9};
        if(oz_parse_clear_policy(invalid[i],&p)||p.extreme!=9||p.assessment!=9)return 1;
    }
    puts("PASS clear recovery policy valid=4 invalid=9 unchanged-output=1");return 0;
}
