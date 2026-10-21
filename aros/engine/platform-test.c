/* Tests real libzpool kernel-service adapters inside AROS, without spa_init
 * or pool creation. Only a newly created regular file is ever written. */
#include <sys/zfs_context.h>
#include <sys/zfs_file.h>
#include <stdio.h>
#include <string.h>
static kmutex_t lock;
static kcondvar_t cv;
static zfs_file_t *file;
static int done, errors;
static int worker_write[2],worker_read[2],worker_compare[2];
static loff_t worker_base = UINT64_C(0x100000000);
static void worker(void *arg)
{
    uintptr_t id=(uintptr_t)arg;
    unsigned char out[512],in[512];
    memset(out,0x41+id,sizeof(out));
    loff_t offset=worker_base+id*4096;
    int failed=0;
    for(unsigned i=0;i<100;i++){
        worker_write[id]=zfs_file_pwrite(file,out,sizeof(out),offset,9,NULL);
        worker_read[id]=zfs_file_pread(file,in,sizeof(in),offset,NULL);
        worker_compare[id]=worker_read[id]?0:memcmp(out,in,sizeof(out));
        if(worker_write[id] || worker_read[id] || worker_compare[id]){failed=1;break;}
    }
    mutex_enter(&lock);errors+=failed;done++;cv_signal(&cv);mutex_exit(&lock);
}
int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "small") == 0)
        worker_base = 4096;
    char path[128];unsigned char entropy[512];
    kmutex_t identity_lock;
    krwlock_t identity_rw;
    mutex_init(&identity_lock,NULL,MUTEX_DEFAULT,NULL);
    errors += MUTEX_HELD(&identity_lock) != 0 || curthread == NULL;
    mutex_enter(&identity_lock);errors += !MUTEX_HELD(&identity_lock);
    mutex_exit(&identity_lock);errors += MUTEX_HELD(&identity_lock) != 0;
    mutex_destroy(&identity_lock);
    rw_init(&identity_rw,NULL,RW_DEFAULT,NULL);
    errors += RW_WRITE_HELD(&identity_rw) != 0;
    rw_enter(&identity_rw,RW_WRITER);errors += !RW_WRITE_HELD(&identity_rw);
    rw_exit(&identity_rw);errors += RW_WRITE_HELD(&identity_rw) != 0;
    rw_destroy(&identity_rw);
    printf("thread identity: raw=%u encoded=%llu ownership errors=%d\n",
        (unsigned)pthread_self(),(unsigned long long)(uintptr_t)curthread,errors);
    printf("AROS upstream platform test: off_t=%zu pagesize=%zu cpus=%d\n",
        sizeof(off_t),PAGESIZE,aros_zfs_ncpus());fflush(stdout);
    int rc=random_get_bytes(entropy,sizeof(entropy));
    printf("entropy result=%d\n",rc);fflush(stdout);if(rc)return 20;
    zfs_file_t *missing = (zfs_file_t *)(uintptr_t)1;
    rc=zfs_file_open("SYS:openzfs-intentionally-missing/cache",O_RDONLY,0,NULL,&missing);
    printf("missing file error=%d output_null=%d\n",rc,missing==NULL);fflush(stdout);
    if (!rc || missing != NULL) return 20;
    hrtime_t before=gethrtime();delay(2);
    printf("delay elapsed_ns=%llu\n",(unsigned long long)(gethrtime()-before));
    snprintf(path,sizeof(path),"SYS:engine-io-%02x%02x%02x%02x.bin",entropy[0],entropy[1],entropy[2],entropy[3]);
    rc=zfs_file_open(path,O_CREAT|O_EXCL|O_RDWR,0600,NULL,&file);
    printf("file=%s open=%d\n",path,rc);fflush(stdout);if(rc)return 20;
    unsigned char probe[512]={0};
    int parent_write=zfs_file_pwrite(file,probe,sizeof(probe),0,9,NULL);
    int parent_read=zfs_file_pread(file,probe,sizeof(probe),0,NULL);
    printf("parent write@0=%d read@0=%d\n",parent_write,parent_read);
    errors += parent_write != 0 || parent_read != 0;
    int high_write=zfs_file_pwrite(file,probe,sizeof(probe),worker_base,9,NULL);
    printf("parent write@%llu=%d\n",(unsigned long long)worker_base,high_write);fflush(stdout);
    errors += high_write != 0;
    mutex_init(&lock,NULL,MUTEX_DEFAULT,NULL);cv_init(&cv,NULL,CV_DEFAULT,NULL);
    kthread_t *a=zk_thread_create("zfs-io-a",worker,(void*)0,262144,TS_JOINABLE);
    kthread_t *b=zk_thread_create("zfs-io-b",worker,(void*)1,262144,TS_JOINABLE);
    mutex_enter(&lock);
    clock_t deadline=ddi_get_lbolt()+30*hz;
    while(done<2)if(cv_timedwait(&cv,&lock,deadline)<0){printf("FAIL worker timeout\n");fflush(stdout);abort();}
    mutex_exit(&lock);thread_join(a);thread_join(b);
    printf("workers: write=%d/%d read=%d/%d compare=%d/%d\n",worker_write[0],worker_write[1],worker_read[0],worker_read[1],worker_compare[0],worker_compare[1]);
    rc=zfs_file_fsync(file,O_SYNC);printf("fsync=%d\n",rc);errors+=rc!=0;
    zfs_file_attr_t attr;rc=zfs_file_getattr(file,&attr);
    printf("getattr=%d\n",rc);
    errors+=rc!=0 || attr.zfa_size!=worker_base+4096+512;
    printf("file size=%llu fsync/getattr tested; workers=%d errors=%d\n",
        (unsigned long long)(rc?0:attr.zfa_size),done,errors);
    zfs_file_close(file);cv_destroy(&cv);mutex_destroy(&lock);
    rc=zfs_file_unlink(path);
    printf("unlink existing=%d\n",rc);errors+=rc!=0;
    rc=zfs_file_unlink(path);
    printf("unlink missing=%d expected=%d\n",rc,ENOENT);errors+=rc!=ENOENT;
    printf("RESULT upstream platform failures=%d\n",errors);fflush(stdout);
    return errors?20:0;
}
