/* SPDX-License-Identifier: CDDL-1.0 */
#include <stdio.h>
int main(void)
{
    for(unsigned i=0;i<131072;++i)if(putchar('A'+i%26)==EOF)return 20;
    if(fputs("\nreport-tail\n",stdout)<0||fflush(stdout))return 20;
    return 23; /* Failed commands must retain their diagnostics too. */
}
