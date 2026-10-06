#ifndef FLOW_DIAG_LOG_H
#define FLOW_DIAG_LOG_H
#include <stdio.h>
#include "../../shared/flow_diagnostics.h"
/* One bounded line per newly assembled page set; partial sets carry a mask.
 * Raw u16 words preserve exact wire values; the read-only PC logger decodes them. */
static inline void flow_diag_log(const flow_diagnostics_t *d,uint32_t session,uint32_t now) {
    static uint32_t last_session,last_updates;
    if(!d->seen || (session==last_session && d->updates==last_updates)) return;
    /* Single publisher task; keep formatting storage out of its 4 KiB stack. */
    static char line[768];
    int n=snprintf(line,sizeof line,
        "@FD:{\"v\":1,\"ms\":%lu,\"session\":%lu,\"id\":%u,\"t16\":%u,"
        "\"age_ms\":%lu,\"mask\":%u,\"fresh\":%u,\"incomplete\":%lu,\"pages\":[",
        (unsigned long)now,(unsigned long)session,d->id,d->sample_ms16,
        (unsigned long)fd_age(d,now),d->mask,fd_complete_fresh(d,now),(unsigned long)d->incomplete);
    if(n<0 || (unsigned)n>=sizeof line)return;
    for(unsigned page=0;page<FD_PAGES;page++) {
        int added;
        const uint16_t *p=d->page[page];
        if(d->mask&(1U<<page)) added=snprintf(line+n,sizeof line-(unsigned)n,
            "%s[%u,%u,%u,%u,%u,%u]",page?",":"",p[0],p[1],p[2],p[3],p[4],p[5]);
        else added=snprintf(line+n,sizeof line-(unsigned)n,"%snull",page?",":"");
        if(added<0 || (unsigned)added>=sizeof line-(unsigned)n)return;
        n+=added;
    }
    if(sizeof line-(unsigned)n<4)return;
    line[n++]=']';line[n++]='}';line[n]=0;
    puts(line);last_session=session;last_updates=d->updates;
}
#endif
