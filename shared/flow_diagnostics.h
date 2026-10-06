#ifndef FLOW_DIAGNOSTICS_H
#define FLOW_DIAGNOSTICS_H
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Diagnostic-only HC3 replies. No command/status fields are overlaid here.
 * channels: sample id, aircraft sample ms modulo 65536, six page words.
 * assist=1 (schema), state=0, height=sample age at transmission (ms).
 * Signed internal flow quantities use x10; angles use signed 0.01 degree.
 * INT16_MIN is unavailable; clipping uses +/-32767 and FD_CLIPPED. */
enum { HC_FLOW_HEALTH=0x84, HC_FLOW_ESTIMATE=0x85, HC_FLOW_CONTROL=0x86,
       HC_FLOW_MOTORS=0x87, HC_FLOW_SOURCE=0x88, FD_PAGES=5, FD_ALL=31 };
enum { FD_SOURCE_FRESH=1, FD_POSITION_READY=2, FD_UNLOCKED=4,
       FD_HEIGHT_LOCK=8, FD_FLOW_REQUEST=16, FD_LOOP_ACTIVE=32,
       FD_CLIPPED=64, FD_NONFINITE=128, FD_ATTITUDE_VALID=256 };
static inline bool fd_command(unsigned c) { return c>=HC_FLOW_HEALTH && c<=HC_FLOW_SOURCE; }
static inline bool fd_payload_valid(unsigned command, const uint16_t c[8]) {
    if(command==HC_FLOW_HEALTH) {
        unsigned state=c[2]&255;
        return (state==1 || state==2 || state==31 || state==255) &&
               c[4]>=1000 && c[4]<=2000 && c[5]>=1000 && c[5]<=2000 && !(c[7]&~511U);
    }
    if(command==HC_FLOW_MOTORS) {
        for(unsigned i=2;i<4;i++)
            if(c[i]!=0x8000 && ((int16_t)c[i]<-18000 || (int16_t)c[i]>18000)) return false;
        for(unsigned i=4;i<8;i++) if(c[i]>1000) return false;
    }
    if(command==HC_FLOW_SOURCE && (c[7]&~255U)) return false;
    return fd_command(command);
}

typedef struct {
    bool seen;
    uint8_t mask;
    uint16_t id, sample_ms16, sample_age_ms, sequence;
    uint16_t page[FD_PAGES][6];
    uint32_t first_rx_ms, rx_ms, updates, incomplete;
} flow_diagnostics_t;

static inline bool fd_newer(uint16_t a,uint16_t b) { uint16_t d=(uint16_t)(a-b);return d && d<0x8000; }
/* Caller checks the HC session and radio sequence window first. Never updates
 * flight phases, link-health timers, alarm telemetry, or outgoing commands. */
static inline bool fd_receive(flow_diagnostics_t *d,unsigned command,const uint16_t c[8],
                              uint16_t age,uint16_t sequence,uint32_t now) {
    unsigned page=command-HC_FLOW_HEALTH;
    if(!fd_payload_valid(command,c)) return false;
    if(d->seen && !fd_newer(sequence,d->sequence)) return false;
    if(d->seen && c[0]==d->id) {
        if(c[1]!=d->sample_ms16 || (d->mask&(1U<<page))) return false;
    } else {
        if(d->seen && !fd_newer(c[0],d->id)) return false;
        if(d->seen && d->mask!=FD_ALL) ++d->incomplete;
        d->mask=0;memset(d->page,0,sizeof d->page);
        d->id=c[0];d->sample_ms16=c[1];d->first_rx_ms=now;
    }
    memcpy(d->page[page],c+2,sizeof d->page[page]);
    d->seen=true;d->mask|=(uint8_t)(1U<<page);d->sequence=sequence;
    d->sample_age_ms=age;d->rx_ms=now;++d->updates;
    return true;
}
static inline uint32_t fd_age(const flow_diagnostics_t *d,uint32_t now) {
    uint32_t elapsed=now-d->rx_ms;
    if(!d->seen || elapsed>UINT32_MAX-d->sample_age_ms) return UINT32_MAX;
    return elapsed+d->sample_age_ms;
}
static inline bool fd_complete_fresh(const flow_diagnostics_t *d,uint32_t now) {
    return d->seen && d->mask==FD_ALL && fd_age(d,now)<1000;
}
#endif
