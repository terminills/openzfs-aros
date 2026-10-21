/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_IOSTAT_REFRESH_H
#define OZ_IOSTAT_REFRESH_H
#include "api.h"
#include <string.h>
#define OZ_REFRESH_US 2000000ULL
struct oz_refresh { UQUAD last,due;int armed;char pool[256]; };
/* Replay only a successfully displayed read-only page, not a guessed query.
 * Pages are live samples and do not constitute an atomic pool-wide snapshot. */
struct oz_io_view { char pool[256],options[1024];UQUAD cursor; };
static int oz_io_view_matches(const struct oz_io_view *view,unsigned operation,
    const char *target,const char *argument,const char *options)
{
    return operation==35&&view->pool[0]&&!strcmp(view->pool,target)&&
        !argument[0]&&!strcmp(view->options,options);
}
static void oz_io_view_request(const struct oz_io_view *view,struct oz_message *request)
{
    memset(request,0,sizeof(*request));request->operation=35;
    strcpy(request->target,view->pool);strcpy(request->options,view->options);
    request->cursor=view->cursor;
}
/* Automatic requests currently support only complete ordinary-counter queries.
 * Do not silently discard edits when constructing the next request. */
static inline int oz_refresh_editor_matches(unsigned operation,const char *sampled,
    const char *target,const char *argument,const char *options)
{
    return operation==35&&sampled[0]&&!strcmp(sampled,target)&&
        !argument[0]&&!options[0];
}
static void oz_refresh_stop(struct oz_refresh *s){memset(s,0,sizeof(*s));}
static int oz_refresh_arm(struct oz_refresh *s,const char *pool,UQUAD now)
{
    oz_refresh_stop(s);
    if(!pool[0]||strlen(pool)>=sizeof(s->pool)||now>~(UQUAD)0-OZ_REFRESH_US)return 0;
    strcpy(s->pool,pool);s->last=now;s->due=now+OZ_REFRESH_US;s->armed=1;return 1;
}
/* Clock rollback rearms; missed periods are never replayed as a request burst. */
static int oz_refresh_due(struct oz_refresh *s,UQUAD now,int busy)
{
    if(!s->armed)return 0;
    if(now<s->last){s->last=now;s->due=now+OZ_REFRESH_US;return 0;}
    s->last=now;
    if(busy||now<s->due)return 0;
    if(now>~(UQUAD)0-OZ_REFRESH_US){oz_refresh_stop(s);return 0;}
    s->due=now+OZ_REFRESH_US;return 1;
}
#endif
