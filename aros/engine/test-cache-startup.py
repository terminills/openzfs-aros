#!/usr/bin/env python3
"""Compile the actual loader with observable file/namespace boundaries."""
import pathlib, subprocess, tempfile
repo = pathlib.Path(__file__).resolve().parents[2]
source = (repo/'lib/libzpool/kernel.c').read_text()
start = source.index('static void\nspa_config_load(void)')
end = source.index('\nvoid\nkernel_init(', start)
function = source[start:end]
prefix = r'''
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
typedef int nvlist_t; typedef int nvpair_t; typedef int zfs_file_t;
typedef struct { uint64_t zfa_size; } zfs_file_attr_t;
#define MAXPATHLEN 1024
#define KM_SLEEP 0
#define DATA_TYPE_NVLIST 1
#define ZPOOL_CACHE_BOOT "/boot/zfs/zpool.cache"
#define FTAG NULL
#define kcred NULL
static const char *spa_config_path;
static unsigned opens, boot_opens, reads, adds;
static int configured_present;
static void *kmem_alloc(size_t n,int flag){(void)flag;return malloc(n);}
static void kmem_free(void *p,size_t n){(void)n;free(p);}
static int zfs_file_open(const char *p,int f,int m,void *c,zfs_file_t **o){
    (void)f;(void)m;(void)c;++opens;
    if(!strcmp(p,ZPOOL_CACHE_BOOT))++boot_opens;
    static int file;
    if(!strcmp(p,ZPOOL_CACHE_BOOT)||configured_present){*o=&file;return 0;}
    return ENOENT;
}
static int zfs_file_getattr(zfs_file_t *f,zfs_file_attr_t *a){(void)f;a->zfa_size=8;return 0;}
static int zfs_file_read(zfs_file_t *f,void *b,uint64_t n,void *r){(void)f;(void)b;(void)n;(void)r;++reads;return 0;}
static void zfs_file_close(zfs_file_t *f){(void)f;}
static int nvlist_unpack(void *b,uint64_t n,nvlist_t **o,int f){(void)b;(void)n;(void)f;static int list;*o=&list;return 0;}
static nvpair_t *nvlist_next_nvpair(nvlist_t *l,nvpair_t *p){(void)l;static int pair;return p?NULL:&pair;}
static int nvpair_type(nvpair_t *p){(void)p;return DATA_TYPE_NVLIST;}
static nvlist_t *fnvpair_value_nvlist(nvpair_t *p){return p;}
static char *nvpair_name(nvpair_t *p){(void)p;return "unadmitted";}
static void *spa_lookup(const char *p){(void)p;return NULL;}
static int spa_add(const char *n,nvlist_t *l,void *p){(void)n;(void)l;(void)p;++adds;return 0;}
static void spa_namespace_enter(void *p){(void)p;}
static void spa_namespace_exit(void *p){(void)p;}
static void nvlist_free(nvlist_t *l){(void)l;}
'''
suffix = r'''
int main(void){
    const char *paths[]={"", "/explicit/missing.cache", "/explicit/present.cache"};
    for(unsigned i=0;i<3;i++){
        opens=boot_opens=reads=adds=0;configured_present=i==2;spa_config_path=paths[i];
        spa_config_load();
#ifdef __AROS__
        if(opens!=(i==0?0:1)||boot_opens||reads!=(i==2)||adds!=(i==2))return 1;
#else
        if(opens!=(i==2?1:2)||boot_opens!=(i!=2)||reads!=1||adds!=1)return 1;
#endif
    }
#ifdef __AROS__
    spa_config_path=NULL;opens=0;spa_config_load();if(opens)return 1;
#endif
    puts("PASS cache startup paths and namespace boundary");return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='openzfs-cache-startup-') as temp:
    work=pathlib.Path(temp)
    (work/'test.c').write_text(prefix+function+suffix)
    for mode in ('aros','upstream'):
        flags=['-D__AROS__'] if mode=='aros' else []
        subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',*flags,str(work/'test.c'),'-o',str(work/mode)],check=True)
        subprocess.run([str(work/mode)],check=True)
