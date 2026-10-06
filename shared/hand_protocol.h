#ifndef HAND_PROTOCOL_H
#define HAND_PROTOCOL_H
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "flow_diagnostics.h"

/* HC v2/v3: 32 bytes, big endian, CRC-16/CCITT-FALSE over bytes 0..29.
 * Version 3 is explicit altitude control; it never changes v2 throttle semantics. */
enum { HC_CONTROL=0, HC_ARM=1, HC_DISARM=2, HC_TAKEOFF=3, HC_LAND=4, HC_RESUME=5,
       HC_STATUS=0x80, HC_DETAIL=0x81, HC_DIAGNOSTICS=0x82, HC_TELEMETRY=0x83 };
/* TELEMETRY only: channels[5..7] = pitch/roll in signed 0.01 deg, distance cm;
 * byte 26 is validity bits, not the uplink assist switches. */
enum { HC_ATTITUDE_VALID=1, HC_DISTANCE_VALID=2, HC_ATTITUDE_UNKNOWN=0x8000,
       HC_DISTANCE_UNKNOWN=0xffff, HC_DISTANCE_MIN_CM=20, HC_DISTANCE_MAX_CM=720,
       HC_TELEMETRY_INTERVAL_MS=300 };
enum { HC_OK=0, HC_NEED_LOCK=1, HC_NOT_LEVEL=2, HC_BAD_INPUT=3,
       HC_TIMEOUT=4, HC_LOCKED=5, HC_IMU=6, HC_GROUND=7, HC_TILT=8, HC_RADIO=9,
       HC_HEIGHT=10, HC_TAKEOFF_TIMEOUT=11, HC_NOT_READY=12 };
enum { HC_STAGE_LOCKED=0, HC_STAGE_IDLE=1, HC_STAGE_TAKEOFF=2, HC_STAGE_HOLD=3,
       HC_STAGE_LAND=4, HC_STAGE_HEIGHT_LOST=5, HC_STAGE_FAULT=6, HC_STAGE_MANUAL=7 };
/* GROUND_READY now reports a captured ground reference only, not a launch gate. */
enum { HC_STATE_RESUME_READY=0x20, HC_STATE_GROUND_READY=0x40, HC_STATE_HEIGHT_FRESH=0x80,
       HC_HEIGHT_UNKNOWN=0x8000 };
typedef struct {
    uint32_t session;
    uint16_t sequence, channels[8];
    uint8_t command, assist;
    uint8_t version, state; /* version 0 encodes as v2 for existing callers. */
    uint16_t height; /* v3 signed relative mm; 0x8000 means unavailable. */
} hc_packet_t;
static inline uint16_t hc_get16(const uint8_t *p) { return (uint16_t)((p[0]<<8)|p[1]); }
static inline void hc_put16(uint8_t *p,uint16_t n) { p[0]=n>>8;p[1]=(uint8_t)n; }
static inline uint16_t hc_crc(const uint8_t *p,unsigned n) {
    uint16_t crc=0xffff;
    while(n--) { crc^=(uint16_t)*p++<<8;
        for(unsigned i=0;i<8;i++) crc=(uint16_t)((crc<<1)^((crc&0x8000)?0x1021:0));
    } return crc;
}
static inline void hc_encode(const hc_packet_t *v,uint8_t p[32]) {
    memset(p,0,32);p[0]='H';p[1]='C';p[2]=v->version?v->version:2;p[3]=v->command;
    hc_put16(p+4,(uint16_t)(v->session>>16));hc_put16(p+6,(uint16_t)v->session);
    hc_put16(p+8,v->sequence);
    for(unsigned i=0;i<8;i++) hc_put16(p+10+2*i,v->channels[i]);
    p[26]=v->assist;
    if(p[2]==3) { p[27]=v->state;hc_put16(p+28,v->height); }
    hc_put16(p+30,hc_crc(p,30));
}
static inline bool hc_decode(const uint8_t p[32],hc_packet_t *v) {
    if(p[0]!='H'||p[1]!='C'||(p[2]!=2&&p[2]!=3)||hc_get16(p+30)!=hc_crc(p,30)) return false;
    if(p[2]==2) {
        if(p[27]||p[28]||p[29]||p[26]>1||(p[3]>HC_DISARM&&p[3]!=HC_STATUS)) return false;
    } else {
        bool diagnostic=fd_command(p[3]);
        bool reply=p[3]==HC_STATUS||p[3]==HC_DETAIL||p[3]==HC_DIAGNOSTICS||p[3]==HC_TELEMETRY||diagnostic;
        if(p[26]>3||(!reply&&p[3]>HC_RESUME)) return false;
        if(!reply&&(p[27]||p[28]||p[29])) return false;
        if(diagnostic) {if(p[26]!=1 || p[27]!=0) return false;}
        else if(reply&&((p[27]&0x10)||(p[27]&0x0f)>HC_STAGE_MANUAL)) return false;
        if(p[3]==HC_TELEMETRY) {
            int16_t pitch=(int16_t)hc_get16(p+20),roll=(int16_t)hc_get16(p+22);
            uint16_t distance=hc_get16(p+24);
            if((p[26]&HC_ATTITUDE_VALID)&&(pitch < -18000||pitch > 18000||roll < -18000||roll > 18000)) return false;
            if((p[26]&HC_DISTANCE_VALID)&&(distance<HC_DISTANCE_MIN_CM||distance>HC_DISTANCE_MAX_CM)) return false;
        }
    }
    v->version=p[2];v->state=p[27];v->height=hc_get16(p+28);
    v->command=p[3];v->session=((uint32_t)hc_get16(p+4)<<16)|hc_get16(p+6);
    if(!v->session) return false;
    v->sequence=hc_get16(p+8);v->assist=p[26];
    for(unsigned i=0;i<8;i++) v->channels[i]=hc_get16(p+10+2*i);
    if(fd_command(v->command) && !fd_payload_valid(v->command,v->channels)) return false;
    return true;
}
static inline bool hc_newer(uint16_t a,uint16_t b) { uint16_t d=(uint16_t)(a-b);return d!=0&&d<0x8000; }
#endif
