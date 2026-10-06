#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tx_control.h"
#include "hand_indicator.h"
#include "../../shared/hand_protocol.h"
#include "../../shared/haptic_policy.h"

static const uint16_t moving[8]={1300,1700,1100,1700,2000,1000,1000,1000};
static uint16_t out[8];
static void sample(hand_feedback_t *s,uint32_t t,bool rv,unsigned cm,bool av,int p,int r)
{ hand_feedback_sample(s,t,rv,(uint16_t)cm,av,(int16_t)p,(int16_t)r,true); }

static void test_policy(void)
{
    hand_feedback_t s={0};
    sample(&s,0,true,101,true,2000,-2000);assert(!s.hold&&!s.tilt);
    sample(&s,300,true,100,true,2001,0);assert(s.hold&&!s.ready&&!s.tilt);
    sample(&s,600,true,121,true,0,-2001);assert(s.tilt&&!s.ready);
    hand_feedback_expire(&s,1100);assert(!s.ready); /* a timer alone is not a new frame */
    sample(&s,1100,true,121,true,1499,-1499);assert(s.ready&&s.tilt);
    sample(&s,1400,true,120,true,1500,0);assert(!s.ready&&s.tilt); /* exact limits do not clear */
    sample(&s,1700,true,121,true,1499,0);
    sample(&s,2000,false,0,true,1499,0);assert(!s.ready&&s.hold&&s.tilt);
    sample(&s,2300,true,121,true,1499,0);assert(!s.tilt&&!s.ready);
    sample(&s,2600,true,121,true,0,0);
    sample(&s,2900,true,121,true,0,0);assert(s.ready);
    hand_feedback_expire(&s,3500);assert(s.hold&&!s.ready);
    sample(&s,3500,true,121,true,2100,0);assert(!s.ready&&!s.tilt);
    sample(&s,3800,true,121,false,2100,0);assert(!s.tilt);
    sample(&s,4100,true,121,true,2100,0);assert(!s.tilt);
    sample(&s,4700,true,121,true,2100,0);assert(!s.tilt&&!s.ready); /* 600 ms breaks continuity */
    sample(&s,5000,true,121,true,2100,0);assert(s.tilt);
    sample(&s,5300,true,121,true,0,0);
    sample(&s,5600,true,121,false,0,0);
    sample(&s,5900,true,121,true,0,0);assert(s.tilt);
    sample(&s,6200,true,121,true,0,0);assert(s.tilt);
    sample(&s,6500,true,121,true,0,0);assert(!s.tilt);
    s=(hand_feedback_t){0};uint32_t t=UINT32_MAX-400;
    sample(&s,t,true,90,true,-2100,0);
    sample(&s,t+300,true,121,true,-2100,0);assert(s.tilt);
    sample(&s,t+600,true,121,true,0,0);
    sample(&s,t+900,true,121,true,0,0);assert(s.ready&&s.tilt);
    sample(&s,t+1200,true,121,true,0,0);assert(!s.tilt);
}

static tx_control_t live(void)
{
    tx_control_t s={.phase=TX_ALT_LIVE,.v2=true,.v3=true,.session=42,
        .feedback=true,.air_flags=19,.air_stage=HC_STAGE_HOLD};
    return s;
}
static hc_packet_t reply(tx_control_t *s,uint32_t t,unsigned type,unsigned validity,
                          unsigned cm,int pitch,int roll)
{
    hc_packet_t p={.version=3,.session=s->session,.sequence=++s->sequence,.command=(uint8_t)type,
        .state=HC_STAGE_HOLD|HC_STATE_HEIGHT_FRESH,.assist=(uint8_t)validity,
        .channels={19,HC_OK,1500,HC_CONTROL,1,(uint16_t)pitch,(uint16_t)roll,(uint16_t)cm}};
    if(s->phase==TX_ALT_TAKEOFF) p.state=HC_STAGE_TAKEOFF;
    if(s->phase==TX_ALT_MANUAL) {p.state=HC_STAGE_MANUAL;p.channels[0]=1;}
    if(s->phase==TX_ALT_LAND) p.state=HC_STAGE_LAND;
    uint8_t b[32];hc_encode(&p,b);tx_feedback(s,b,t);s->heartbeat=t;
    return p;
}
/* Ordinary 50 ms replies keep the radio healthy, without creating sensor samples. */
static void status_until(tx_control_t *s,uint32_t now)
{
    while((uint32_t)(now-s->feedback_ms)>50)
        reply(s,s->feedback_ms+50,HC_STATUS,0,0,0,0);
}
static hc_packet_t telem(tx_control_t *s,uint32_t t,unsigned v,unsigned cm,int p,int r)
{ status_until(s,t);return reply(s,t,HC_TELEMETRY,v,cm,p,r); }
static void step(tx_control_t *s,uint32_t t)
{ s->heartbeat=t;assert(tx_step(s,t,true,false,moving,out)); }
static void neutral_packet(tx_control_t *s,unsigned command)
{
    uint8_t b[32];hc_packet_t p;tx_encode(s,out,b);
    assert(hc_decode(b,&p)&&p.command==command&&p.assist==3);
    for(unsigned i=0;i<4;++i) assert(p.channels[i]==1500);
}

static void test_wire_and_recovery(void)
{
    tx_control_t s=live();
    step(&s,0);assert(out[1]==1500&&out[2]==1100); /* no range: only forward inhibited */
    telem(&s,50,3,101,0,0);step(&s,50);assert(!memcmp(out,moving,sizeof out));
    hc_packet_t p=telem(&s,350,3,100,2100,0);
    assert(s.guard.hold&&!s.guard.tilt);step(&s,350);neutral_packet(&s,HC_CONTROL);
    assert(!tx_clear_obstacle(&s,350));
    uint8_t b[32];hc_encode(&p,b);tx_feedback(&s,b,351);assert(!s.guard.tilt); /* duplicate */
    p.sequence=++s.sequence;p.session++;hc_encode(&p,b);tx_feedback(&s,b,352);assert(!s.guard.tilt);
    p.session=s.session;hc_encode(&p,b);b[30]^=1;tx_feedback(&s,b,353);assert(!s.guard.tilt);
    telem(&s,650,3,121,-2100,0);assert(s.guard.tilt&&!s.guard.ready);
    telem(&s,950,3,121,0,0);assert(!s.guard.ready);
    status_until(&s,1199);tx_guard_refresh(&s,1199);assert(!s.guard.ready); /* no sample */
    telem(&s,1250,3,121,0,0);assert(s.guard.ready&&s.guard.hold);
    assert(tx_clear_obstacle(&s,1250));assert(!s.guard.hold);
    step(&s,1250);assert(!memcmp(out,moving,sizeof out));
    tx_encode(&s,out,b);assert(hc_decode(b,&p)&&p.command==HC_CONTROL); /* no command 6 */
    telem(&s,1550,3,90,0,0);assert(s.guard.hold);
    telem(&s,1850,3,121,0,0);telem(&s,2150,3,121,0,0);telem(&s,2450,3,121,0,0);
    assert(s.guard.ready);
    status_until(&s,3050);assert(!tx_clear_obstacle(&s,3050)); /* fresh STATUS cannot revive old range */
    step(&s,3050);neutral_packet(&s,HC_CONTROL);assert(s.guard.hold);
    telem(&s,3100,3,121,0,0);assert(!s.guard.ready); /* needs a new clear dwell */
    s.guard.ready=true; /* even ready state cannot bypass communication or disarm checks */
    s.link_warning=1;assert(!tx_clear_obstacle(&s,3100));s.link_warning=0;
    s.air_flags=0;assert(!tx_clear_obstacle(&s,3100));s.air_flags=19;
    assert(tx_land(&s,3100)&&!s.guard.hold);step(&s,3100);neutral_packet(&s,HC_LAND);
    telem(&s,3400,3,50,2100,0);assert(!s.guard.hold);
    tx_stop(&s,3400,TX_REASON_STOP);step(&s,3400);tx_encode(&s,out,b);
    assert(hc_decode(b,&p)&&p.command==HC_DISARM&&p.channels[2]==1000&&!s.guard.tilt);
    s=live();s.phase=TX_ALT_MANUAL;
    telem(&s,50,3,50,0,0);step(&s,50);
    assert(out[0]==1500&&out[1]==1500&&out[3]==1500&&out[2]==1100);
    tx_fault(&s);assert(!s.guard.hold&&!s.guard.tilt);
    s=live();s.phase=TX_ALT_TAKEOFF;
    telem(&s,50,3,50,0,0);assert(s.guard.hold);step(&s,50);neutral_packet(&s,HC_TAKEOFF);
    s.phase=TX_ALT_LIVE;step(&s,51);neutral_packet(&s,HC_CONTROL);
    assert(tx_start_v3(&(tx_control_t){0},0,77));
}

static void test_invalid_and_session(void)
{
    tx_control_t s=live();telem(&s,50,3,200,0,0);step(&s,50);assert(out[1]==1700);
    status_until(&s,650);step(&s,650);assert(out[1]==1500&&out[0]==1300&&out[2]==1100);
    telem(&s,700,1,HC_DISTANCE_UNKNOWN,0,0);step(&s,700);assert(out[1]==1500);
    telem(&s,1000,3,100,0,0);assert(s.guard.hold);
    telem(&s,1300,3,121,0,0);telem(&s,1600,3,121,0,0);
    telem(&s,1900,1,HC_DISTANCE_UNKNOWN,0,0);assert(s.guard.hold&&!s.guard.ready);
    tx_fault(&s);assert(tx_start_v3(&s,2000,55)&&!s.guard.hold&&!s.guard.sampled);
    /* Locked preflight reports tilt, but must never latch obstacle control. */
    reply(&s,2050,HC_TELEMETRY,3,50,3000,0);
    reply(&s,2100,HC_TELEMETRY,3,50,3000,0);assert(!s.guard.hold&&s.guard.tilt);
}

static void test_ground_tilt_indicators(void)
{
    tx_control_t s={0};assert(tx_start_v3(&s,1000,42));
    uint8_t b[32];hc_packet_t p={.version=3,.command=HC_TELEMETRY,.session=42,
        .assist=3,.channels={0,HC_OK,1000,HC_DISARM,0,0,(uint16_t)-2001,50}};
    for(unsigned i=0;i<2;++i) {
        p.sequence=++s.sequence;hc_encode(&p,b);tx_feedback(&s,b,1100+i*300);
        assert(s.phase==TX_READY&&!s.guard.hold&&!s.guard.ready);
        assert(s.guard.tilt==(i==1));
    }
    assert(hand_feedback_roll_indicator(&s.guard,1400)==-1);
    assert(hand_indicator_tilt(1|2|8|16,-1,true)==(1|2|4|16));
    assert(hand_indicator_tilt(1|2|8|16,-1,false)==(1|2|16));
    assert(hand_indicator_tilt(1|2|4|16,1,true)==(1|2|8|16));
    assert(hand_indicator_tilt(1|2|4|16,1,false)==(1|2|16));
    assert(hand_indicator_tilt(31,0,false)==31);
    assert(hand_feedback_roll_indicator(&s.guard,1999)==-1);
    assert(hand_feedback_roll_indicator(&s.guard,2000)==0);
    tx_guard_refresh(&s,2000);assert(s.guard.tilt&&!s.guard.hold);
    /* Old status traffic cannot refresh attitude or manufacture another pulse. */
    p.command=HC_STATUS;p.sequence=++s.sequence;hc_encode(&p,b);tx_feedback(&s,b,2001);
    assert(hand_feedback_roll_indicator(&s.guard,2001)==0);
    p.command=HC_TELEMETRY;p.channels[6]=0;
    for(unsigned i=0;i<3;++i) {
        p.sequence=++s.sequence;hc_encode(&p,b);tx_feedback(&s,b,2100+i*300);
    }
    assert(!s.guard.tilt&&hand_feedback_roll_indicator(&s.guard,2700)==0);
    p.channels[6]=2001;
    p.sequence=++s.sequence;hc_encode(&p,b);tx_feedback(&s,b,3000);
    assert(!s.guard.tilt&&hand_feedback_roll_indicator(&s.guard,3000)==0);
    /* Duplicate, CRC error and wrong session cannot count as a second sample. */
    tx_feedback(&s,b,3001);assert(!s.guard.tilt);
    b[20]^=1;tx_feedback(&s,b,3002);assert(!s.guard.tilt);
    p.session=99;p.sequence=++s.sequence;hc_encode(&p,b);tx_feedback(&s,b,3003);
    assert(!s.guard.tilt);
    p.session=42;p.sequence=++s.sequence;hc_encode(&p,b);tx_feedback(&s,b,3300);
    assert(s.guard.tilt&&hand_feedback_roll_indicator(&s.guard,3300)==1);
    p.assist=2;p.channels[5]=p.channels[6]=HC_ATTITUDE_UNKNOWN;
    p.sequence=++s.sequence;hc_encode(&p,b);tx_feedback(&s,b,3600);
    assert(hand_feedback_roll_indicator(&s.guard,3600)==0);
    /* Ground alarm is purely an indication: transmitted command remains DISARM. */
    s.heartbeat=3600;assert(tx_step(&s,3600,true,true,moving,out));tx_encode(&s,out,b);
    assert(hc_decode(b,&p)&&p.command==HC_DISARM&&p.channels[2]==1000);
    tx_fault(&s);assert(!s.guard.tilt&&!s.guard.roll_direction);
}

static void test_haptics(void)
{
    haptic_policy_t h={0};
    assert(haptic_update(&h,100,true,true)==2);
    assert(haptic_update(&h,3099,true,true)==2);
    assert(haptic_update(&h,3100,true,true)==1);
    assert(haptic_update(&h,6100,true,true)==0);
    assert(haptic_update(&h,9999,true,true)==0);
    haptic_update(&h,10000,false,false);
    assert(haptic_update(&h,10001,true,false)==1);
    assert(haptic_update(&h,11001,true,true)==2&&h.remaining[0]==2000);
    assert(haptic_update(&h,14001,true,true)==1);
    assert(haptic_update(&h,16001,true,true)==0);
}
static void test_ground_obstacle(void)
{
    tx_control_t s={0};assert(tx_start_v3(&s,1000,42));
    hc_packet_t p={.version=3,.command=HC_TELEMETRY,.session=42,.assist=3,
        .channels={0,HC_OK,1000,HC_DISARM,0,0,0,101}};
    uint8_t b[32];haptic_policy_t h={0};
    p.sequence=++s.sequence;hc_encode(&p,b);tx_feedback(&s,b,1100);
    assert(!tx_obstacle_alarm(&s)&&!tx_obstacle_indicator(&s,1100));
    p.channels[7]=100;p.sequence=++s.sequence;hc_encode(&p,b);tx_feedback(&s,b,1400);
    assert(tx_obstacle_alarm(&s)&&tx_obstacle_indicator(&s,1400)==1);
    assert(!s.guard.hold&&!s.guard.ready&&s.phase==TX_READY);
    assert(hand_indicator_obstacle(1,true)&&hand_indicator_obstacle(1,false));
    assert(!hand_indicator_obstacle(0,true)&&!hand_indicator_obstacle(0,false));
    assert(hand_indicator_obstacle(2,true)&&!hand_indicator_obstacle(2,false));
    assert(haptic_update(&h,1400,tx_obstacle_alarm(&s),false)==1);
    assert(haptic_update(&h,4399,tx_obstacle_alarm(&s),false)==1);
    assert(haptic_update(&h,4400,tx_obstacle_alarm(&s),false)==0);
    assert(haptic_update(&h,8000,tx_obstacle_alarm(&s),false)==0);
    assert(!tx_obstacle_indicator(&s,2000)); /* stale data cannot light GPIO5 */
    p.channels[7]=50;p.sequence=++s.sequence;hc_encode(&p,b);tx_feedback(&s,b,8100);
    assert(tx_obstacle_indicator(&s,8100)==1);
    assert(haptic_update(&h,8100,tx_obstacle_alarm(&s),false)==0); /* no repeated stale edge */
    p.assist=1;p.channels[7]=HC_DISTANCE_UNKNOWN;p.sequence=++s.sequence;
    hc_encode(&p,b);tx_feedback(&s,b,8400);
    assert(!tx_obstacle_indicator(&s,8400)&&tx_obstacle_alarm(&s));
    p.assist=3;p.channels[7]=101;
    for(unsigned i=0;i<3;++i) {
        p.sequence=++s.sequence;hc_encode(&p,b);tx_feedback(&s,b,8700+i*300);
        assert(!tx_obstacle_indicator(&s,8700+i*300));
    }
    assert(!tx_obstacle_alarm(&s));haptic_update(&h,9300,false,false);
    p.channels[7]=99;p.sequence=++s.sequence;hc_encode(&p,b);tx_feedback(&s,b,9600);
    assert(haptic_update(&h,9600,tx_obstacle_alarm(&s),false)==1);
    assert(!s.guard.hold&&!tx_clear_obstacle(&s,9600));
    s.heartbeat=9600;assert(tx_step(&s,9600,true,true,moving,out));tx_encode(&s,out,b);
    assert(hc_decode(b,&p)&&p.command==HC_DISARM&&p.channels[2]==1000);
    s.phase=TX_ALT_IDLE;assert(tx_obstacle_alarm(&s));
    s.phase=TX_ALT_LAND;tx_guard_refresh(&s,9601);
    assert(!tx_obstacle_alarm(&s)&&!tx_obstacle_indicator(&s,9601));
    s.phase=TX_OFF;tx_guard_refresh(&s,9602);assert(!s.guard.near_latched);
    s=live();telem(&s,50,3,100,0,0);
    assert(s.guard.hold&&tx_obstacle_indicator(&s,50)==2);
    /* A short invalid interval cannot rearm the ground event. */
    hand_feedback_t g={0};uint32_t t=UINT32_MAX-400;
    hand_feedback_sample(&g,t,true,100,true,0,0,false);
    hand_feedback_sample(&g,t+300,true,101,true,0,0,false);
    hand_feedback_sample(&g,t+600,false,0,true,0,0,false);assert(g.near_latched);
    hand_feedback_sample(&g,t+900,true,101,true,0,0,false);
    hand_feedback_sample(&g,t+1200,true,101,true,0,0,false);assert(g.near_latched);
    hand_feedback_sample(&g,t+1500,true,101,true,0,0,false);assert(!g.near_latched);
}
int main(void)
{
    test_policy();test_wire_and_recovery();test_invalid_and_session();test_haptics();
    test_ground_tilt_indicators();
    test_ground_obstacle();
    puts("hand-only guard: thresholds, fresh telemetry, old protocol, priority, recovery and haptics PASS");
    return 0;
}
