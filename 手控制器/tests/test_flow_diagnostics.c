#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tx_control.h"
#include "flow_diag_log.h"
#include "../../shared/hand_protocol.h"
static unsigned checks;
#define CHECK(x) do {assert(x);++checks;} while(0)
static hc_packet_t page(unsigned i,unsigned id,unsigned seq) {
    hc_packet_t p={.version=3,.command=HC_FLOW_HEALTH+i,.session=123,.sequence=seq,
        .assist=1,.height=40,.channels={id,65530,0,0,0,0,0,0}};
    if(i==0) {p.channels[2]=(80<<8)|31;p.channels[3]=12;p.channels[4]=1500;p.channels[5]=1500;p.channels[6]=7;p.channels[7]=63;}
    if(i==1) {p.channels[2]=(uint16_t)-123;p.channels[3]=456;}
    if(i==3) {p.channels[2]=(uint16_t)-1234;p.channels[3]=567;p.channels[4]=500;p.channels[5]=501;p.channels[6]=502;p.channels[7]=503;}
    return p;
}
static void receive(tx_control_t *s,hc_packet_t p,uint32_t now) {
    uint8_t b[32];s->sequence=p.sequence;hc_encode(&p,b);tx_feedback(s,b,now);
}
int main(void) {
    tx_control_t s={0};CHECK(tx_start_v3(&s,100,123));
    /* A diagnostic cannot count as a control acknowledgement or arm gate. */
    for(unsigned i=0;i<FD_PAGES;i++) {
        tx_control_t before=s;hc_packet_t p=page(i,65535,i+1),decoded;uint8_t b[32];
        hc_encode(&p,b);CHECK(hc_decode(b,&decoded));CHECK(decoded.command==p.command);
        receive(&s,p,200+i*100);CHECK(s.flow.mask==((1U<<(i+1))-1));
        before.sequence=s.sequence;before.flow=s.flow;
        CHECK(memcmp(&s,&before,sizeof s)==0);CHECK(!tx_can_arm(&s,1000));
    }
    CHECK(fd_complete_fresh(&s.flow,601));CHECK(fd_age(&s.flow,601)==41);
    CHECK(!fd_complete_fresh(&s.flow,1560));
    CHECK((int16_t)s.flow.page[1][0]==-123);
    flow_diag_log(&s.flow,s.session,601);
    /* All pages of one sample must have identical time and must not mix. */
    flow_diagnostics_t saved=s.flow;hc_packet_t p=page(0,65535,6);
    CHECK(!fd_receive(&s.flow,p.command,p.channels,p.height,p.sequence,700));
    CHECK(!memcmp(&s.flow,&saved,sizeof saved));
    p=page(0,0,7);p.channels[1]=20;receive(&s,p,0xfffffff0);
    CHECK(s.flow.id==0 && s.flow.mask==1);CHECK(fd_age(&s.flow,0x10)==72);
    p=page(1,0,8);p.channels[1]=21;receive(&s,p,0x20);CHECK(s.flow.mask==1);
    p.channels[1]=20;receive(&s,p,0x30);CHECK(s.flow.mask==3);
    p=page(0,1,9);receive(&s,p,0x40);CHECK(s.flow.mask==1 && s.flow.incomplete==1);
    p=page(2,0,10);receive(&s,p,0x50);CHECK(s.flow.id==1 && s.flow.mask==1);
    /* Invalid session/CRC/reserved fields/values do not alter the cache. */
    saved=s.flow;p=page(1,1,11);p.session=555;receive(&s,p,100);CHECK(!memcmp(&saved,&s.flow,sizeof saved));
    for(unsigned i=0;i<7;i++) {
        p=page(0,2,12);uint8_t b[32];hc_packet_t decoded;
        if(i==0)p.assist=2;
        if(i==1)p.state=1;
        if(i==2)p.channels[4]=999;
        if(i==3)p.channels[2]=0;
        if(i==4)p.channels[7]=512;
        if(i==5){p=page(3,2,12);p.channels[4]=1001;}
        hc_encode(&p,b);if(i==6)b[18]^=1;
        CHECK(!hc_decode(b,&decoded));tx_feedback(&s,b,110);CHECK(!memcmp(&saved,&s.flow,sizeof saved));
    }
    /* Legacy replies remain valid, diagnostic loss does not suppress DISARM. */
    p=(hc_packet_t){.version=3,.command=HC_STATUS,.session=123,.sequence=13,
        .channels={0,0,1000,HC_DISARM,10,0,0,0}};
    receive(&s,p,120);CHECK(s.feedback && s.feedback_ms==120 && s.phase==TX_READY);
    tx_estop(&s,130);uint16_t out[8],preview[8]={1700,1700,1800,1700,2000,1000,1000,1000};
    CHECK(tx_step(&s,140,false,false,preview,out));uint8_t raw[32];hc_packet_t command;
    tx_encode(&s,out,raw);CHECK(hc_decode(raw,&command));CHECK(command.command==HC_DISARM);
    CHECK(tx_estop_remaining(&s,140)==9990);
    s.phase=TX_OFF;CHECK(tx_start_v3(&s,200,456));CHECK(!s.flow.seen && s.flow.mask==0);
    /* Current control output is identical with/without an assembled diagnostic. */
    tx_control_t a={0},b={0};tx_start_v3(&a,1000,123);a.phase=TX_ALT_LIVE;a.heartbeat=1000;
    a.telemetry=true;a.telemetry_ms=1000;a.air_distance_valid=true;a.air_distance_cm=720;b=a;
    for(unsigned i=0;i<FD_PAGES;i++)receive(&b,page(i,1,i+1),1000);
    uint16_t x[8],y[8];CHECK(tx_step(&a,1010,true,true,preview,x));CHECK(tx_step(&b,1010,true,true,preview,y));CHECK(!memcmp(x,y,sizeof x));
    printf("flow diagnostics: %u checks passed\n",checks);
    return 0;
}
