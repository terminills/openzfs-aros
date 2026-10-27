#!/usr/bin/env python3
"""Exercise actual uninitialized SPA authorization with mocked SPA fields."""
import pathlib, subprocess, tempfile
source=(pathlib.Path(__file__).resolve().parent/'pool-import.c').read_text()
def function(marker):
    start=source.index(marker);brace=source.index('{',start);depth=1;end=brace+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
code=r'''
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#define SPA_MODE_WRITE 2
#define POOL_STATE_UNINITIALIZED 0
#define ZPOOL_CONFIG_POOL_GUID "pool_guid"
typedef struct { uint64_t loaded; unsigned spa_state; uint64_t *spa_config; } spa_t;
struct imported_files { struct imported_files *next; const char *name;
    unsigned count;int readonly;uint64_t guid;void *policy; };
static struct imported_files *imports;
static unsigned spa_mode_global;
static uint64_t spa_guid(spa_t *s){return s->loaded;}
static const char *spa_name(spa_t *s){(void)s;return "pool";}
static int nvlist_lookup_uint64(uint64_t *p,const char *k,uint64_t *v){(void)k;*v=*p;return 0;}
'''
code+=function('static uint64_t admitted_spa_guid(')
code+=function('int oz_pool_import_uninitialized_rewind_check(')
code+=r'''
#define CHECK(e) do { if(!(e)){fprintf(stderr,"FAIL line=%d\n",__LINE__);return 1;} }while(0)
int main(void){
    uint64_t cfg=42;spa_t s={0,0,&cfg};
    struct imported_files p={NULL,"pool",2,0,42,&cfg};
    imports=&p;spa_mode_global=2;
    CHECK(admitted_spa_guid(&s)==42);
    CHECK(oz_pool_import_uninitialized_rewind_check(&s)==0);
    s.loaded=43;CHECK(oz_pool_import_uninitialized_rewind_check(&s)==ESTALE);s.loaded=0;
    s.spa_state=1;CHECK(admitted_spa_guid(&s)==0);s.spa_state=0;
    s.spa_config=NULL;CHECK(oz_pool_import_uninitialized_rewind_check(&s)==ESTALE);s.spa_config=&cfg;
    p.readonly=1;CHECK(oz_pool_import_uninitialized_rewind_check(&s)==EROFS);p.readonly=0;
    spa_mode_global=0;CHECK(oz_pool_import_uninitialized_rewind_check(&s)==EROFS);spa_mode_global=2;
    p.policy=NULL;CHECK(oz_pool_import_uninitialized_rewind_check(&s)==EACCES);p.policy=&cfg;
    p.count=0;CHECK(oz_pool_import_uninitialized_rewind_check(&s)==EACCES);p.count=2;
    imports=NULL;CHECK(oz_pool_import_uninitialized_rewind_check(&s)==EACCES);imports=&p;
    p.guid=0;CHECK(oz_pool_import_uninitialized_rewind_check(&s)==ESTALE);
    puts("PASS uninitialized admission: identity, writable mode, retained policy, member count and absence");
}
'''
with tempfile.TemporaryDirectory(prefix='openzfs-uninitialized-admission-') as temp:
    root=pathlib.Path(temp);(root/'test.c').write_text(code)
    subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',str(root/'test.c'),'-o',str(root/'test')],check=True)
    subprocess.run([str(root/'test')],check=True)
