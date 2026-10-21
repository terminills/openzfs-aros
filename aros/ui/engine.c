#include "api.h"
#include <stdio.h>
#include <string.h>
int oz_engine_start(int fixture_writes) { (void)fixture_writes; return 0; }
int oz_engine_stop(void) { return 1; }
ULONG oz_engine_signals(void) { return 0; }
void oz_engine_pump(void) { }
/* This adapter is the ONLY place that will call the upstream AROS engine.
 * No private-format handler fallback, CLI execution or simulated pools. */
void oz_engine_dispatch(struct oz_message *m) {
    const struct oz_operation *op=oz_find(m->operation);
    m->engine_available=0;m->row_count=0;m->job_id=0;m->next_cursor=0;
    memset(m->capability_words,0,sizeof(m->capability_words));
    m->capability_words[0]=(1UL<<1)|(1UL<<2); /* hello + capabilities only */
    if(!strcmp(op->key,"service.hello")) {
        m->status=OZ_OK;
        snprintf(m->detail,sizeof(m->detail),"OpenZFS management service\nAPI: %u\nTransport: native Exec messages\nEngine: not integrated\nNo pools or devices have been opened.",(unsigned)OZ_VERSION);
    } else if(!strcmp(op->key,"service.capabilities")) {
        m->status=OZ_OK;
        snprintf(m->detail,sizeof(m->detail),"api.version=%u\nengine.available=false\nengine.operations=none\nservice.operations=hello,capabilities\nMutations are not available. Catalogue visibility is not capability support.",(unsigned)OZ_VERSION);
    } else {
        m->status=OZ_NO_ENGINE;
        snprintf(m->detail,sizeof(m->detail),"Received and validated: %s\nThe upstream OpenZFS AROS engine is not integrated.\nOperation was NOT executed. No storage was changed.",op->key);
    }
}
