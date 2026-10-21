/* Private disposable FAT backing-volume probe, before OpenZFS cold tests. */
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <string.h>
#include <stdio.h>
#include <proto/security.h>
#include <libraries/security.h>
static void note(const char *s){while(*s)RawPutChar(*s++);RawPutChar('\n');}
/* Characterize native file identity before implementing vdev relocation.
 * A successful rename must keep the open handle attached to the same data.
 * Unsupported lock duplication/rename is reported, never emulated by inode
 * numbers, path strings or matching contents. Only this new fixture is used. */
static int identity_probe(void)
{
    const char *old="OZFILES:identity-probe",*moved="OZFILES:identity-moved";
    const char payload[]="OpenZFS native identity";
    char data[sizeof(payload)]={0},line[240];
    BPTR fh=Open((CONST_STRPTR)old,MODE_NEWFILE);
    if(!fh)return 0;
    int ok=Write(fh,payload,sizeof(payload))==sizeof(payload)&&Flush(fh);
    if(!Close(fh))ok=0;
    if(!ok)return 0;
    fh=Open((CONST_STRPTR)old,MODE_READWRITE);
    if(!fh)return 0;
    BPTR identity=DupLockFromFH(fh);LONG lock_error=identity?0:IoErr();
    LONG renamed=Rename((CONST_STRPTR)old,(CONST_STRPTR)moved);
    LONG rename_error=renamed?0:IoErr();
    BPTR after=identity?DupLockFromFH(fh):0;
    LONG same=identity&&after?SameLock(identity,after):LOCK_DIFFERENT;
    if(identity&&(!after||same!=LOCK_SAME))ok=0;
    if(after)UnLock(after);
    if(identity)UnLock(identity);
    char resolved[256]={0};
    LONG named=NameFromFH(fh,(STRPTR)resolved,sizeof(resolved));
    LONG name_error=named?0:IoErr();
    snprintf(line,sizeof(line),"File identity: writable NameFromFH=%ld error=%ld path=%.160s",
        (long)(named!=0),(long)name_error,resolved);note(line);
    if(Seek(fh,0,OFFSET_BEGINNING)==-1||
        Read(fh,data,sizeof(data))!=sizeof(data)||memcmp(data,payload,sizeof(data)))ok=0;
    if(!Close(fh))ok=0;
    snprintf(line,sizeof(line),"File identity: writable DupLockFromFH=%ld error=%ld rename=%ld error=%ld same=%ld",
        (long)(identity!=0),(long)lock_error,(long)(renamed!=0),(long)rename_error,(long)same);
    note(line);
    const char *current=renamed?moved:old;
    fh=Open((CONST_STRPTR)current,MODE_OLDFILE);
    if(!fh)return 0;
    identity=DupLockFromFH(fh);lock_error=identity?0:IoErr();
    BPTR byname=Lock((CONST_STRPTR)current,SHARED_LOCK);
    same=identity&&byname?SameLock(identity,byname):LOCK_DIFFERENT;
    if(identity&&(!byname||same!=LOCK_SAME))ok=0;
    if(byname)UnLock(byname);
    /* Unlike path strings, a retained native lock must distinguish a new
     * file at an old name from the object just renamed away from that name. */
    const char *other=renamed?old:moved;
    BPTR copy=Open((CONST_STRPTR)other,MODE_NEWFILE);
    if(!copy)ok=0;
    else {
        if(Write(copy,payload,sizeof(payload))!=sizeof(payload)||!Flush(copy))ok=0;
        if(!Close(copy))ok=0;
    }
    BPTR distinct=copy?Lock((CONST_STRPTR)other,SHARED_LOCK):0;
    LONG different=identity&&distinct?SameLock(identity,distinct):LOCK_SAME;
    if(!identity||!distinct||different==LOCK_SAME)ok=0;
    if(distinct)UnLock(distinct);
    if(identity)UnLock(identity);
    if(copy&&!DeleteFile((CONST_STRPTR)other))ok=0;
    snprintf(line,sizeof(line),"File identity: identical-content different-file SameLock=%ld",(long)different);note(line);
    if(Read(fh,data,sizeof(data))!=sizeof(data)||memcmp(data,payload,sizeof(data)))ok=0;
    if(!Close(fh))ok=0;
    snprintf(line,sizeof(line),"File identity: readonly DupLockFromFH=%ld error=%ld same-path-lock=%ld",
        (long)(identity!=0),(long)lock_error,(long)same);note(line);
    if(!DeleteFile((CONST_STRPTR)current))ok=0;
    note(ok?"RESULT backing identity probe PASS":"RESULT backing identity probe FAIL");
    return ok;
}
int main(int argc,char **argv)
{
    /* A missing mount is a bounded test failure, not an unattended requester. */
    ((struct Process *)FindTask(NULL))->pr_WindowPtr=(APTR)-1;
    BPTR marker=Open((CONST_STRPTR)"SYS:Security/openzfs-test-only",MODE_OLDFILE);
    if(!marker)return 20;
    Close(marker);
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    if(!secBase||!secIsConfigured())return 20;
    struct TagItem login[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    if(secLoginA(login)!=secOWNER_SYSTEM)return 20;
    static const char seed[]="OpenZFS private backing fixture\n";
    static const char proof[]="Native AROS persistent backing IO\n";
    char buf[128]={0};BPTR file;
    int verify=argc==2&&!strcmp(argv[1],"READ");
    if(argc!=1&&!verify)return 20;
    file=Open((CONST_STRPTR)"OZFILES:fixture.txt",MODE_OLDFILE);
    if(!file){note("RESULT backing FAIL open fixture");return 20;}
    LONG got=Read(file,buf,sizeof(buf));Close(file);
    if(got!=sizeof(seed)-1||memcmp(buf,seed,sizeof(seed)-1)) {
        note("RESULT backing FAIL fixture contents");return 20;
    }
    file=Open((CONST_STRPTR)"OZFILES:proof.txt",MODE_OLDFILE);
    if(file) {
        got=Read(file,buf,sizeof(buf));Close(file);
        if(got!=sizeof(proof)-1||memcmp(buf,proof,sizeof(proof)-1)) {
            note("RESULT backing FAIL persisted contents");return 20;
        }
        note("RESULT backing cold read PASS");return 0;
    }
    if(verify){note("RESULT backing FAIL missing persisted file");return 20;}
    if(IoErr()!=ERROR_OBJECT_NOT_FOUND){note("RESULT backing FAIL lookup");return 20;}
    if(!identity_probe()){note("RESULT backing FAIL identity probe");return 20;}
    file=Open((CONST_STRPTR)"OZFILES:proof.txt",MODE_NEWFILE);
    if(!file){note("RESULT backing FAIL create");return 20;}
    int ok=Write(file,proof,sizeof(proof)-1)==sizeof(proof)-1&&Flush(file);
    if(!Close(file))ok=0;
    /* Flush() drains DOS buffering, not the FAT volume cache. This probe
     * tests a clean detach, not fsync/power-loss durability. */
    if(ok&&!Inhibit((CONST_STRPTR)"OZBACK:",DOSTRUE)) {
        char line[80];snprintf(line,sizeof(line),"Backing inhibit failed IoErr=%ld",(long)IoErr());note(line);ok=0;
    }
    note(ok?"RESULT backing write PASS":"RESULT backing FAIL write");return ok?0:20;
}
