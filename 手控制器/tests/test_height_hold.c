#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tx_control.h"
#include "../../shared/hand_protocol.h"

static const uint16_t moving[8]={1700,1300,1800,1700,2000,1000,1000,1000};
static tx_control_t flying(void)
{
    return (tx_control_t){.v2=true,.v3=true,.phase=TX_ALT_LIVE,.feedback=true,
        .feedback_ms=1000,.heartbeat=1000,.air_flags=3,.air_height_valid=true,
        .air_stage=HC_STAGE_HOLD,.session=123};
}
static void check_stop_and_land(void)
{
    uint16_t out[8];uint8_t raw[32];hc_packet_t packet;
    tx_control_t s=flying();assert(tx_toggle_height_hold(&s,1000));
    assert(tx_land(&s,1001));assert(!s.height_hold);
    assert(tx_step(&s,1002,true,false,moving,out));tx_encode(&s,out,raw);
    assert(hc_decode(raw,&packet)&&packet.command==HC_LAND);
    s=flying();assert(tx_toggle_height_hold(&s,1000));tx_stop(&s,1001,TX_REASON_STOP);
    assert(!s.height_hold);assert(tx_step(&s,1002,true,false,moving,out));tx_encode(&s,out,raw);
    assert(hc_decode(raw,&packet)&&packet.command==HC_DISARM);
}
int main(void)
{
    uint16_t out[8];uint8_t raw[32];hc_packet_t p;tx_control_t s=flying();
    assert(tx_toggle_height_hold(&s,1000)&&s.height_hold);
    assert(tx_step(&s,1050,true,false,moving,out));
    for(unsigned i=0;i<4;++i)assert(out[i]==1500);
    tx_encode(&s,out,raw);assert(hc_decode(raw,&p)&&p.command==HC_CONTROL);
    assert(tx_toggle_height_hold(&s,1100)&&!s.height_hold);
    assert(tx_step(&s,1100,true,false,moving,out)&&!memcmp(out,moving,sizeof out));
    s.guard.hold=true;assert(tx_toggle_height_hold(&s,1100));assert(tx_toggle_height_hold(&s,1100));
    assert(s.guard.hold);assert(tx_step(&s,1100,true,false,moving,out));
    for(unsigned i=0;i<4;++i)assert(out[i]==1500); /* Key cannot clear obstacle latch. */
    unsigned blocked[]={TX_OFF,TX_READY,TX_ARM_REQUEST,TX_ALT_IDLE,TX_ALT_TAKEOFF,TX_ALT_LAND,TX_ALT_MANUAL,TX_STOPPING,TX_FAULT};
    for(unsigned i=0;i<sizeof(blocked)/sizeof(blocked[0]);++i) {
        s=flying();s.phase=blocked[i];assert(!tx_toggle_height_hold(&s,1000));
        assert(!s.height_hold&&s.phase==blocked[i]); /* Never arm/take off on this key. */
    }
    s=flying();s.air_height_valid=false;assert(!tx_toggle_height_hold(&s,1000)&&s.hold_reason==HOLD_NO_HEIGHT);
    s=flying();assert(!tx_toggle_height_hold(&s,1300)&&s.hold_reason==HOLD_NO_LINK);
    s=flying();s.link_warning=1;assert(!tx_toggle_height_hold(&s,1000));
    s=flying();assert(tx_toggle_height_hold(&s,1000));s.air_height_valid=false;s.air_flags=1;s.phase=TX_ALT_MANUAL;
    assert(tx_step(&s,1050,true,false,moving,out));assert(!s.height_hold&&out[2]==1800);
    s=flying();assert(tx_toggle_height_hold(&s,1000));tx_fault(&s);assert(!s.height_hold);
    check_stop_and_land();puts("PASS: GPIO47 height hold toggles neutral control; stop/land override; invalid height/link/ground reject; obstacle independent");
}
