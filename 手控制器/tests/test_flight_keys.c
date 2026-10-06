#include <assert.h>
#include <stdio.h>
#include "flight_keys.h"
#include "../../shared/hand_protocol.h"

static const uint16_t preview[8]={1500,1500,1500,1500,2000,1000,1000,1000};
static void click(flight_keys_t *k,tx_control_t *tx,uint32_t now)
{
    flight_keys_edge(k,tx,0,true,now);
    flight_keys_edge(k,tx,0,false,now+50);
}
static void reply(tx_control_t *tx,uint32_t now,unsigned stage,unsigned command,unsigned flags)
{
    hc_packet_t p={.version=3,.command=HC_STATUS,.session=tx->session,.sequence=++tx->sequence,
        .state=stage,.channels={flags,HC_OK,1500,command,1,0,0,0}};
    uint8_t b[32];hc_encode(&p,b);tx_feedback(tx,b,now);
}
static void check_packet(tx_control_t *tx,uint32_t now,unsigned command)
{
    uint16_t out[8];uint8_t b[32];hc_packet_t p;
    assert(tx_step(tx,now,true,true,preview,out));tx_encode(tx,out,b);
    assert(hc_decode(b,&p) && p.command==command);
    if(command==HC_DISARM || command==HC_ARM) assert(p.channels[2]==1000);
}

static void test_auto_connection_never_launches(void)
{
    flight_keys_t k={0};tx_control_t tx={0};
    flight_keys_autoconnect(&k,&tx,0,21,true,true);
    assert(tx.phase==TX_OFF); // GPIO initialization must finish first.
    flight_keys_init(&k,&tx,false,false,0);
    assert(k.auto_connect&&!k.pending&&!k.launch_requested);
    flight_keys_autoconnect(&k,&tx,50,21,false,true);assert(tx.phase==TX_OFF);
    flight_keys_autoconnect(&k,&tx,100,21,true,false);assert(tx.phase==TX_OFF);
    // No host PING, no calibration and no launch click for two minutes.
    // A late aircraft, missing replies and changing poses cannot trigger launch.
    for(uint32_t now=150;now<=120000;now+=50) {
        if(now>=6000&&!(now>=15000&&now<18000))
            reply(&tx,now,HC_STAGE_LOCKED,HC_DISARM,0);
        flight_keys_autoconnect(&k,&tx,now,21,true,true);
        flight_keys_tick(&k,&tx,now,true,true,now%200==0,now%200==50,now%200==100);
        check_packet(&tx,now,HC_DISARM);
        assert(tx.phase==TX_READY&&k.auto_connect&&k.local_owner&&!k.pending);
        assert(!k.launch_requested);
    }
    assert(k.status==KEY_LINK_READY);
    // A local radio fault can recover, but recovery also sends DISARM only.
    tx_fault(&tx);
    flight_keys_autoconnect(&k,&tx,120050,22,false,true);
    assert(tx.phase==TX_FAULT&&k.auto_connect);
    flight_keys_autoconnect(&k,&tx,120100,22,true,true);
    check_packet(&tx,120100,HC_DISARM);assert(tx.session==22&&tx.phase==TX_READY);
    // Restoring an invalid IMU may reconnect only after the lock-stop completes.
    flight_keys_autoconnect(&k,&tx,120150,23,true,false);
    assert(tx.phase==TX_STOPPING&&k.auto_connect&&!k.local_owner);
    for(unsigned i=0;i<15;++i)tx_sent(&tx);
    uint16_t out[8];assert(!tx_step(&tx,120650,true,true,preview,out));
    flight_keys_autoconnect(&k,&tx,120650,23,true,false);assert(tx.phase==TX_OFF);
    flight_keys_autoconnect(&k,&tx,120700,23,true,true);
    check_packet(&tx,120700,HC_DISARM);
    // Explicit STOP keeps lock-only communication, but never resumes flight.
    flight_keys_stop(&k,&tx,120750);
    for(unsigned i=0;i<15;++i)tx_sent(&tx);
    assert(tx_step(&tx,121250,true,true,preview,out));
    flight_keys_autoconnect(&k,&tx,122000,24,true,true);
    assert(tx.phase==TX_READY&&k.auto_connect&&!k.pending&&k.stop_latched&&tx.estop_active);
}

static void test_auto_connection_launch_and_handoffs(void)
{
    flight_keys_t k;tx_control_t tx={0};
    flight_keys_init(&k,&tx,true,false,0);
    flight_keys_autoconnect(&k,&tx,50,31,true,true);
    flight_keys_edge(&k,&tx,0,false,100);
    assert(!k.launch_requested&&!k.pending); // Boot-held release is never launch.
    for(uint32_t now=100;now<=650;now+=50) {
        reply(&tx,now,HC_STAGE_LOCKED,HC_DISARM,0);
        flight_keys_autoconnect(&k,&tx,now,31,true,true);
        check_packet(&tx,now,HC_DISARM);
    }
    assert(k.status==KEY_LINK_READY);
    click(&k,&tx,700);
    assert(flight_keys_begin(&k,&tx,750,32,true,true,false));
    assert(!k.auto_connect&&k.pending&&tx.session==31); // Reuse the locked session.
    reply(&tx,750,HC_STAGE_LOCKED,HC_DISARM,0);
    flight_keys_tick(&k,&tx,750,true,true,true,false,false);
    check_packet(&tx,750,HC_ARM);
    reply(&tx,800,HC_STAGE_IDLE,HC_ARM,1);
    flight_keys_tick(&k,&tx,800,true,true,true,false,false);
    check_packet(&tx,800,HC_TAKEOFF);
    tx_fault(&tx);
    flight_keys_autoconnect(&k,&tx,900,33,true,true);
    assert(tx.phase==TX_FAULT&&!k.auto_connect); // A flight fault never auto-restarts.
    // Boot-held STOP may communicate only locked; release cannot launch.
    tx=(tx_control_t){0};flight_keys_init(&k,&tx,false,true,0);
    flight_keys_edge(&k,&tx,1,false,100);
    flight_keys_autoconnect(&k,&tx,150,34,true,true);
    assert(tx.phase==TX_READY&&k.auto_connect&&k.stop_latched&&tx.estop_active);
    // Explicit host control returns to the USB heartbeat requirement.
    tx=(tx_control_t){0};flight_keys_init(&k,&tx,false,false,0);
    flight_keys_autoconnect(&k,&tx,100,35,true,true);
    flight_keys_host_start(&k);
    flight_keys_autoconnect(&k,&tx,900,36,true,true);
    uint16_t out[8];assert(tx_step(&tx,900,true,true,preview,out));
    assert(tx.phase==TX_STOPPING&&tx.reason==TX_REASON_HEARTBEAT&&!k.auto_connect);
}
int main(void)
{
    test_auto_connection_never_launches();test_auto_connection_launch_and_handoffs();
    flight_keys_t k;tx_control_t tx={0};
    flight_keys_init(&k,&tx,true,false,0);
    flight_keys_edge(&k,&tx,0,false,100);
    assert(!k.launch_requested && !flight_keys_begin(&k,&tx,100,1,true,true,false));
    click(&k,&tx,200);
    assert(!flight_keys_begin(&k,&tx,250,1,false,true,false));
    assert(k.reason==KEY_NO_RADIO && tx.phase==TX_OFF);
    flight_keys_tick(&k,&tx,300,true,true,true,false,false);
    assert(tx.phase==TX_OFF); // Restoring radio never launches a rejected click.
    click(&k,&tx,400);
    assert(flight_keys_begin(&k,&tx,450,9,true,true,false));
    flight_keys_tick(&k,&tx,450,true,true,false,true,false);
    check_packet(&tx,450,HC_DISARM);
    reply(&tx,600,HC_STAGE_LOCKED,HC_DISARM,0);
    flight_keys_tick(&k,&tx,600,true,true,true,false,false);
    assert(tx.phase==TX_READY); // Existing 500ms lock handshake remains mandatory.
    for(uint32_t now=650;now<=950;now+=50) reply(&tx,now,HC_STAGE_LOCKED,HC_DISARM,0);
    flight_keys_tick(&k,&tx,950,true,true,false,true,false);
    assert(tx.phase==TX_READY); // Need the actual one-second neutral confirmation.
    flight_keys_tick(&k,&tx,1000,true,true,true,false,false);
    assert(tx.phase==TX_ARM_REQUEST && k.status==KEY_ARMING);
    check_packet(&tx,1000,HC_ARM);
    flight_keys_tick(&k,&tx,1050,true,true,true,false,false);
    assert(tx.phase==TX_ARM_REQUEST); // Cannot infer arm success from local TX.
    reply(&tx,1100,HC_STAGE_IDLE,HC_ARM,1);
    flight_keys_tick(&k,&tx,1100,true,true,true,false,false);
    assert(tx.phase==TX_ALT_TAKEOFF && !k.pending && k.local_owner);
    check_packet(&tx,1100,HC_TAKEOFF);
    reply(&tx,1150,HC_STAGE_HOLD,HC_TAKEOFF,3);
    for(uint32_t now=1200;now<11000;now+=50) {
        flight_keys_tick(&k,&tx,now,true,true,true,false,false);
        check_packet(&tx,now,HC_CONTROL); // Physical ownership works without USB PING.
    }
    click(&k,&tx,11000);
    assert(!flight_keys_begin(&k,&tx,11050,10,true,true,false));
    assert(tx.phase==TX_ALT_LIVE && k.local_owner); // No mid-flight re-arm/reset.
    uint16_t out[8];
    assert(tx_step(&tx,11751,true,true,preview,out) && tx.phase==TX_STOPPING);
    assert(tx.reason==TX_REASON_HEARTBEAT); // Lost local control task still stops.

    // Emergency STOP dominates every active phase and doesn't wait for pose/reply.
    unsigned phases[]={TX_READY,TX_ARM_REQUEST,TX_ALT_IDLE,TX_ALT_TAKEOFF,TX_ALT_LIVE,TX_ALT_LAND,TX_ALT_MANUAL};
    for(unsigned i=0;i<sizeof(phases)/sizeof(*phases);++i) {
        tx=(tx_control_t){.v2=true,.v3=true,.phase=phases[i],.heartbeat=1000,.session=4};
        flight_keys_init(&k,&tx,false,false,1000);
        k.pending=true;k.local_owner=true;
        flight_keys_edge(&k,&tx,0,true,1010);
        flight_keys_palm(&k,&tx,true,1020);
        flight_keys_edge(&k,&tx,1,true,1020);
        flight_keys_edge(&k,&tx,0,false,1030);
        assert(!k.launch_requested && !k.pending && !k.local_owner && k.stop_latched);
        check_packet(&tx,1030,HC_DISARM);
        assert(!flight_keys_begin(&k,&tx,1040,11,true,true,false));
        flight_keys_edge(&k,&tx,1,false,1050);
        reply(&tx,1100,HC_STAGE_IDLE,HC_ARM,1); // Delayed ARM echo cannot resume launch.
        flight_keys_tick(&k,&tx,1100,true,false,false,false,false);
        check_packet(&tx,1100,HC_DISARM);
        flight_keys_stop(&k,&tx,1200);assert(tx.estop_ms==1200);
        for(unsigned n=0;n<15;++n)tx_sent(&tx);
        assert(tx_step(&tx,1600,true,true,preview,out) && tx.phase==TX_READY);
        flight_keys_tick(&k,&tx,1600,true,true,true,false,false);
        assert(k.status==KEY_STOPPED);
    }
    tx=(tx_control_t){0};flight_keys_init(&k,&tx,false,true,0);
    click(&k,&tx,50);flight_keys_edge(&k,&tx,1,false,150);
    assert(!k.launch_requested && tx.phase==TX_OFF);
    click(&k,&tx,200);assert(!flight_keys_begin(&k,&tx,250,12,true,true,false));
    /* A fresh explicit launch after cooldown is covered by test_estop_cooldown. */
    tx=(tx_control_t){0};flight_keys_init(&k,&tx,false,false,0);
    click(&k,&tx,200);assert(flight_keys_begin(&k,&tx,250,12,true,true,false));
    assert(!k.stop_latched);
    flight_keys_tick(&k,&tx,6251,true,true,true,false,false);
    assert(!k.pending && !k.local_owner && k.reason==KEY_TIMEOUT && tx.phase==TX_STOPPING);
    reply(&tx,6300,HC_STAGE_LOCKED,HC_DISARM,0);
    flight_keys_tick(&k,&tx,6300,true,true,true,false,false);
    assert(tx.phase==TX_STOPPING);

    // Uncentered hand, fist, stale IMU, and a radio failure cancel without retries.
    for(unsigned failure=0;failure<4;++failure) {
        tx=(tx_control_t){0};flight_keys_init(&k,&tx,false,false,0);click(&k,&tx,10);
        assert(flight_keys_begin(&k,&tx,60,13,true,true,false));
        flight_keys_tick(&k,&tx,100,failure!=3,failure!=2,false,false,failure==1);
        assert(!k.pending && !k.local_owner && tx.phase==TX_STOPPING);
        flight_keys_tick(&k,&tx,200,true,true,true,false,false);
        assert(tx.phase==TX_STOPPING);
    }
    // Existing host-started mode still requires its own heartbeat.
    tx=(tx_control_t){0};assert(tx_start_v3(&tx,100,14));
    flight_keys_host_start(&k);flight_keys_tick(&k,&tx,851,true,true,true,false,false);
    assert(tx_step(&tx,851,true,true,preview,out) && tx.reason==TX_REASON_HEARTBEAT);
    puts("PASS: boot lock-only autoconnect/recovery, explicit launch, boot-held guard, USB handoff, watchdogs, stop priority");
}
