/* Disposable hosted authentication probe. No pool or device access. */
#include <proto/exec.h>
#include <proto/security.h>
#include <libraries/security.h>
#include <stdio.h>
int main(void)
{
    struct Library *secBase=OpenLibrary(SECURITYNAME,SECURITYVERSION);
    if(!secBase){puts("FAIL hosted security unavailable");return 20;}
    int configured=secIsConfigured();
    printf("hosted security configured=%d\n",configured);
    ULONG previous=secGetTaskOwner(NULL);
    struct TagItem tags[]={{secT_UserID,(IPTR)"ozreader"},
        {secT_Password,(IPTR)""},{TAG_DONE,0}};
    ULONG login=configured?secLoginA(tags):0;
    int ok=configured&&login==((77UL<<16)|88)&&secGetTaskOwner(NULL)==login;
    printf("hosted security authenticated=%d owner=%08lx\n",ok,(unsigned long)secGetTaskOwner(NULL));
    if(ok) {
        struct TagItem logout[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
        secLogoutA(logout);
        ok=secGetTaskOwner(NULL)==previous;
    }
    printf("RESULT hosted security login/logout %s configured=%d uid77gid88=%d restored=%d\n",
        ok?"PASS":"FAIL",configured,login==((77UL<<16)|88),secGetTaskOwner(NULL)==previous);
    CloseLibrary(secBase);
    return ok?0:20;
}
