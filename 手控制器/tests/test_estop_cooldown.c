#include <assert.h>
#include <stdio.h>
#include "flight_keys.h"
#include "../../shared/hand_protocol.h"
static uint16_t moving[8]={1700,1700,1800,1300,2000,1000,1000,1000};
static void reply(tx_control_t *t,uint32_t now)
{
    hc_packet_t p={.version=3,.command=HC_STATUS,.session=t->session,.sequence=++t->sequence,
        .state=HC_STAGE_LOCKED,.channels={0,0,1000,HC_DISARM,1,0,0,0}};
    uint8_t b[32];hc_encode(&p,b);tx_feedback(t,b,now);
}
static void locked_packet(tx_control_t *t,uint32_t now)
{
    uint16_t ch[8];uint8_t b[32];hc_packet_t p;
    assert(tx_step(t,now,false,false,moving,ch));tx_encode(t,ch,b);
    assert(hc_decode(b,&p)&&p.command==HC_DISARM);
    assert(ch[0]==1500&&ch[1]==1500&&ch[2]==1000&&ch[3]==1500);
}
int main(void)
{
    tx_control_t t={0};flight_keys_t k;assert(tx_start_v3(&t,0,123));
    flight_keys_init(&k,&t,false,false,0);t.phase=TX_ALT_LIVE;k.pending=true;
    flight_keys_palm(&k,&t,true,1000);
    flight_keys_edge(&k,&t,1,true,1000);
    assert(t.estop_active&&t.phase==TX_READY&&!k.pending&&k.stop_latched);
    t.feedback=true;t.feedback_ms=1000;t.air_command=HC_DISARM;t.air_flags=1;
    tx_estop_refresh_session(&t,1000,456);assert(t.session==123); // Never change session while armed.
    t.air_flags=0;tx_estop_refresh_session(&t,1400,456);assert(t.session==123); // Stale ack forbidden.
    reply(&t,1000);tx_estop_refresh_session(&t,1000,456);
    assert(t.session==456&&t.estop_ms==1000&&!t.feedback&&!t.estop_new_session);
    for(uint32_t now=1000;now<=12000;now+=50) {
        flight_keys_autoconnect(&k,&t,now,999,true,false);
        flight_keys_tick(&k,&t,now,true,false,false,false,false);
        locked_packet(&t,now);reply(&t,now);
        assert(t.estop_ms==1000&&!k.pending); // Held STOP never restarts timer.
        assert(t.session==456); // Session refresh happens once, not on every polling tick.
        if(now<11000) assert(!tx_arm(&t,now,true));
    }
    flight_keys_edge(&k,&t,0,true,12000);flight_keys_edge(&k,&t,0,false,12050);
    assert(!k.launch_requested); // Still holding STOP, even after expiry.
    flight_keys_edge(&k,&t,1,false,12100);
    flight_keys_edge(&k,&t,1,true,12200);assert(tx_estop_remaining(&t,12200)==10000);
    flight_keys_edge(&k,&t,1,false,12250);
    // Reconnecting and changing sessions cannot erase cooldown.
    tx_fault(&t);assert(tx_start_v3(&t,13000,321));assert(t.estop_ms==12200);
    for(uint32_t now=13000;now<=22200;now+=50) {
        reply(&t,now);flight_keys_autoconnect(&k,&t,now,321,true,true);
        if(now==22000) flight_keys_edge(&k,&t,0,true,now);
        locked_packet(&t,now);
        if(now<22200)assert(!tx_arm(&t,now,true));
    }
    flight_keys_edge(&k,&t,0,false,22250);assert(!k.launch_requested);
    assert(!flight_keys_begin(&k,&t,22250,5,true,true,false)); // No queued launch.
    assert(t.estop_active&&t.phase==TX_READY);
    // Require fresh locked reply, then a completely new press/release.
    assert(!tx_can_arm(&t,22600));reply(&t,22700);reply(&t,23200);t.link_warning=0;
    flight_keys_edge(&k,&t,0,true,23200);flight_keys_edge(&k,&t,0,false,23250);
    flight_keys_autoconnect(&k,&t,23250,5,true,true);
    assert(flight_keys_begin(&k,&t,23250,5,true,true,false));
    flight_keys_tick(&k,&t,23250,true,true,true,false,false);
    assert(t.phase==TX_ARM_REQUEST&&!t.estop_active); // Simulator only; no hardware.
    tx_estop(&t,24000);assert(!tx_can_takeoff(&t,24000,true)&&!tx_can_resume(&t,24000,true));
    uint32_t wrap=UINT32_MAX-5000;tx_estop(&t,wrap);
    assert(tx_estop_remaining(&t,wrap+9999)==1&&tx_estop_remaining(&t,wrap+10000)==0);
    puts("PASS: estop retains DISARM communication; 10s gate, held/repeated keys, no queued launch, reconnect persistence, stale reply and timer wrap");
}
