/* SPDX-License-Identifier: CDDL-1.0 */
#include "efi-uuid.h"
#include <proto/exec.h>
#include <proto/uuid.h>
#include <errno.h>
#include <string.h>

int oz_efi_uuid_generate(unsigned char out[16])
{
    if(!out)return EINVAL;
    struct Library *UUIDBase=OpenLibrary((CONST_STRPTR)"uuid.library",1);
    if(!UUIDBase)return ENOSYS;
    uuid_t native;
    unsigned char bytes[16];
    /* Use the library's semaphore-protected time/node generator, not its
     * documented weak random generator. GPT identifiers are not secrets. */
    UUID_Generate(UUID_TYPE_DCE_TIME,&native);
    UUID_Pack(&native,bytes);
    CloseLibrary(UUIDBase);
    if((bytes[6]>>4)!=1||(bytes[8]&0xc0)!=0x80)return EIO;
    memcpy(out,bytes,sizeof(bytes));return 0;
}
