#include <assert.h>
#include <stdio.h>
#include "../../shared/flight_feedback.h"
#include "../../shared/haptic_policy.h"
#include "../../shared/hand_protocol.h"
#include "tx_control.h"

static void sample(flight_feedback_t *s,uint32_t t,bool valid,unsigned cm,float pitch,float roll)
{ flight_feedback_update(s,t,true,valid,cm,t,true,pitch,roll); }

int main(void)
{
    flight_feedback_t s={0};
    sample(&s,0,true,101,0,0);assert(!s.hold);
    sample(&s,1,true,100,0,0);assert(s.hold && !flight_feedback_clear(&s));
    sample(&s,100,true,121,0,0);sample(&s,599,true,121,0,0);assert(!s.ready);
    sample(&s,600,true,121,0,0);assert(s.ready && s.hold);
    sample(&s,601,false,65535,0,0);assert(!s.ready && s.hold);
    sample(&s,700,true,121,0,0);sample(&s,1200,true,120,0,0);assert(!s.ready);
    sample(&s,1300,true,121,0,0);
    flight_feedback_update(&s,1800,true,true,121,1300,true,0,0);assert(!s.ready);
    sample(&s,1800,true,121,0,0);assert(s.ready);
    assert(flight_feedback_clear(&s) && !s.hold && !s.ready);
    sample(&s,1801,true,90,0,0);assert(s.hold && !s.ready);
    s=(flight_feedback_t){0};
    sample(&s,0,true,200,30,30);sample(&s,500,true,200,30,30);assert(!s.tilt);
    sample(&s,501,true,200,31,0);sample(&s,700,true,200,31,0);assert(!s.tilt);
    sample(&s,701,true,200,31,0);assert(s.tilt);
    sample(&s,702,true,200,24,0);sample(&s,1201,true,200,24,0);assert(s.tilt);
    sample(&s,1202,true,200,24,0);assert(!s.tilt);
    s=(flight_feedback_t){0};uint32_t start=UINT32_MAX-100;
    sample(&s,start,true,90,-31,0);sample(&s,start+200,true,121,-31,0);assert(s.tilt);
    sample(&s,start+700,true,121,24,0);assert(s.ready);
    flight_feedback_update(&s,0,false,false,0,0,false,0,0);assert(!s.hold&&!s.tilt);

    haptic_policy_t h={0};
    assert(haptic_update(&h,100,true,true)==2);
    assert(haptic_update(&h,3099,true,true)==2);
    assert(haptic_update(&h,3100,true,true)==1);
    assert(haptic_update(&h,6099,true,true)==1);
    assert(haptic_update(&h,6100,true,true)==0);
    assert(haptic_update(&h,9999,true,true)==0); /* no retrigger while held */
    haptic_update(&h,10000,false,false);
    assert(haptic_update(&h,10001,true,false)==1);
    assert(haptic_update(&h,11001,true,true)==2);
    assert(h.remaining[0]==2000);
    assert(haptic_update(&h,14001,true,true)==1);
    assert(haptic_update(&h,16001,true,true)==0);

    tx_control_t tx={.phase=TX_ALT_LIVE,.v2=true,.v3=true,.session=1,
        .feedback=true,.feedback_ms=100,.heartbeat=100,.air_flags=1|HC_FLAG_FEEDBACK_SUPPORTED|
        HC_FLAG_OBSTACLE|HC_FLAG_OBSTACLE_READY};
    uint16_t channels[8]={1300,1700,1100,1700,2000,1000,1000,1000},out[8];
    uint8_t wire[32];hc_packet_t packet;
    assert(tx_clear_obstacle(&tx,100));
    /* No flat requirement; airborne flight stays live, CLEAR transmits neutral. */
    assert(tx_step(&tx,100,true,false,channels,out));tx_encode(&tx,out,wire);
    assert(hc_decode(wire,&packet)&&packet.command==HC_CLEAR_OBSTACLE);
    for(unsigned i=0;i<4;++i) assert(packet.channels[i]==1500);
    packet.command=HC_STATUS;packet.state=HC_STAGE_HOLD;
    packet.channels[0]=3|HC_FLAG_FEEDBACK_SUPPORTED;packet.channels[1]=HC_OK;
    packet.channels[3]=HC_CLEAR_OBSTACLE;hc_encode(&packet,wire);tx_feedback(&tx,wire,101);
    assert(!tx.obstacle_clear_pending && tx.phase==TX_ALT_LIVE);
    tx.air_flags|=HC_FLAG_OBSTACLE;assert(!tx_clear_obstacle(&tx,102));
    tx.air_flags|=HC_FLAG_OBSTACLE_READY;
    assert(!tx_clear_obstacle(&tx,401)); /* stale readiness is not permission */
    tx.feedback_ms=500;tx.heartbeat=500;assert(tx_clear_obstacle(&tx,500));
    tx_step(&tx,800,true,false,channels,out);assert(!tx.obstacle_clear_pending);
    tx.feedback_ms=801;tx.link_warning=0;assert(tx_clear_obstacle(&tx,801));
    tx_stop(&tx,801,TX_REASON_STOP);tx_encode(&tx,out,wire);
    assert(hc_decode(wire,&packet)&&packet.command==HC_DISARM&&!tx.obstacle_clear_pending);
    puts("flight feedback, button recovery and mutually exclusive haptics: PASS");
    return 0;
}
