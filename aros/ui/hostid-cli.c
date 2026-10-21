/* SPDX-License-Identifier: CDDL-1.0 */
#include "hostid-settings.h"
#include <proto/dos.h>
#include <dos/rdargs.h>
#include <stdint.h>
#include <string.h>
int main(void)
{
    SIPTR args[2]={0};
    struct RDArgs *parsed=ReadArgs((CONST_STRPTR)"HOSTID/A,SAVE/S",args,NULL);
    if(!parsed){PrintFault(IoErr(),(CONST_STRPTR)"OpenZFSHostIdentity");return RETURN_FAIL;}
    const char *text=(const char *)args[0];uint32_t identity=0;
    int valid=text&&strlen(text)==8;
    for(unsigned i=0;valid&&i<8;i++){
        unsigned char c=text[i];unsigned value;
        if(c>='0'&&c<='9')value=c-'0';
        else if(c>='a'&&c<='f')value=c-'a'+10;
        else if(c>='A'&&c<='F')value=c-'A'+10;
        else {valid=0;break;}
        identity=(identity<<4)|value;
    }
    int result=RETURN_FAIL;
    if(!valid||!identity||!args[1]){
        PutStr((CONST_STRPTR)"Usage: OpenZFSHostIdentity <8 nonzero hexadecimal digits> SAVE\n"
            "Choose a unique ID for this host. Run as SYSTEM before starting OpenZFS.\n");
    }else{
        unsigned published=0;int error=oz_hostid_settings_apply(identity,&published);
        if(error)Printf((CONST_STRPTR)"Identity save failed: error=%ld; ENVARC saved=%ld; ENV saved=%ld\n",
            (LONG)error,(LONG)!!(published&OZ_HOSTID_SAVED),(LONG)!!(published&OZ_HOSTID_CURRENT));
        else {Printf((CONST_STRPTR)"Host identity %08lx saved to ENVARC and ENV. Start OpenZFS to use it.\n",(ULONG)identity);result=RETURN_OK;}
    }
    FreeArgs(parsed);return result;
}
