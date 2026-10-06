#include <assert.h>
#include <stdio.h>
#include "tx_control.h"
#include "../../shared/hand_protocol.h"

static const uint16_t preview[8] = {2000,2000,1900,2000,2000,1000,1000,1000};
static uint16_t out[8];
static void altitude_reply(tx_control_t *s,uint32_t now,uint8_t stage,uint8_t command,
                           uint16_t flags,uint8_t reason)
{
    hc_packet_t p={.version=3,.command=HC_STATUS,.session=s->session,.sequence=++s->sequence,
        .state=stage|HC_STATE_HEIGHT_FRESH|HC_STATE_GROUND_READY,.height=200,
        .channels={flags,reason,1500,command,10,0,0,2}};
    uint8_t b[32];hc_encode(&p,b);tx_feedback(s,b,now);
}
static void test_altitude_mode(void)
{
    tx_control_t s={0};uint8_t b[32];hc_packet_t p;
    assert(tx_start_v3(&s,100,33));assert(s.v3&&s.v2);
    assert(!tx_can_arm(&s,600));
    assert(tx_step(&s,100,true,true,preview,out));tx_encode(&s,out,b);
    assert(hc_decode(b,&p)&&p.version==3&&p.command==HC_DISARM&&p.channels[2]==1000);
    // A v2 reply cannot validate the incompatible HC3 control semantics.
    p.version=2;p.assist=1;p.command=HC_STATUS;p.channels[3]=HC_DISARM;
    hc_encode(&p,b);tx_feedback(&s,b,600);assert(!s.feedback);
    for(unsigned now=100;now<=600;now+=50) altitude_reply(&s,now,HC_STAGE_LOCKED,HC_DISARM,0,HC_OK);
    assert(tx_arm(&s,600,true));
    s.heartbeat=600;assert(tx_step(&s,600,true,true,preview,out));
    assert(out[2]==1000);tx_encode(&s,out,b);
    assert(hc_decode(b,&p)&&p.command==HC_ARM);
    altitude_reply(&s,650,HC_STAGE_IDLE,HC_ARM,1,HC_OK);
    assert(s.phase==TX_ALT_IDLE);
    assert(tx_step(&s,650,true,true,preview,out));assert(out[2]==1500&&out[0]==1500);
    assert(!tx_takeoff(&s,650,false));
    s.air_height_valid=false;s.air_ground_ready=false;
    assert(tx_can_takeoff(&s,650,true)); // Ground/height availability never gates launch.
    assert(!tx_takeoff(&s,1250,true)); // Stale reply is not permission to take off.
    assert(tx_takeoff(&s,700,true));assert(!tx_takeoff(&s,700,true));
    assert(tx_step(&s,700,true,true,preview,out));assert(out[2]==1500&&out[0]==1500);
    tx_encode(&s,out,b);assert(hc_decode(b,&p)&&p.command==HC_TAKEOFF);
    // Delayed pre-takeoff/early ramp replies cannot enable live gestures.
    altitude_reply(&s,750,HC_STAGE_IDLE,HC_CONTROL,1,HC_OK);
    assert(s.phase==TX_ALT_TAKEOFF);
    altitude_reply(&s,800,HC_STAGE_TAKEOFF,HC_TAKEOFF,1,HC_OK);
    assert(s.phase==TX_ALT_TAKEOFF);
    altitude_reply(&s,850,HC_STAGE_HOLD,HC_TAKEOFF,1,HC_OK);
    assert(s.phase==TX_ALT_TAKEOFF); // Stage alone is insufficient without HEIGHT flag.
    altitude_reply(&s,900,HC_STAGE_HOLD,HC_TAKEOFF,3,HC_OK);
    assert(s.phase==TX_ALT_LIVE);
    s.heartbeat=900;assert(tx_step(&s,900,true,true,preview,out));assert(out[0]==preview[0]);
    // Detail replies refresh the watchdog and preserve STATUS error counts.
    p=(hc_packet_t){.version=3,.session=s.session,.sequence=++s.sequence,.command=HC_DETAIL,
        .state=HC_STAGE_HOLD|HC_STATE_HEIGHT_FRESH,.height=210,
        .channels={3,HC_OK,1500,HC_CONTROL,12,500,400,9}};
    hc_encode(&p,b);tx_feedback(&s,b,950);
    assert(s.detail&&s.detail_ms==950&&s.air_height_mm==500&&s.air_collective==400);
    assert(s.air_radio_faults==9&&s.air_bad==2&&s.air_relative_mm==210);
    tx_control_t auto_land=s;
    altitude_reply(&auto_land,975,HC_STAGE_LAND,HC_CONTROL,3,HC_OK);
    assert(auto_land.phase==TX_ALT_LAND);
    altitude_reply(&auto_land,980,HC_STAGE_HOLD,HC_CONTROL,3,HC_OK);
    assert(auto_land.phase==TX_ALT_LAND); // Landing is latched even if an older stage is echoed.
    assert(tx_step(&auto_land,980,true,true,preview,out)&&out[2]==1500&&out[0]==1500);
    assert(tx_land(&s,1000));assert(!tx_land(&s,1000));
    altitude_reply(&s,1000,HC_STAGE_HOLD,HC_CONTROL,3,HC_OK);
    assert(s.phase==TX_ALT_LAND); // An in-flight reply cannot undo manual landing.
    assert(tx_step(&s,1000,true,true,preview,out));assert(out[2]==1500&&out[1]==1500);
    tx_encode(&s,out,b);assert(hc_decode(b,&p)&&p.command==HC_LAND);
    altitude_reply(&s,1050,HC_STAGE_HEIGHT_LOST,HC_LAND,1,HC_HEIGHT);
    assert(s.phase==TX_ALT_LOST);
    assert(!tx_takeoff(&s,1050,true)&&!tx_arm(&s,1050,true));
    assert(tx_step(&s,1050,true,true,preview,out)&&out[2]==1500&&out[0]==1500);
    tx_stop(&s,1100,TX_REASON_STOP);assert(tx_step(&s,1100,true,true,preview,out));
    tx_encode(&s,out,b);assert(hc_decode(b,&p)&&p.command==HC_DISARM&&p.channels[2]==1000);
    for(unsigned stage=TX_ALT_IDLE;stage<=TX_ALT_LOST;++stage) {
        for(unsigned fail=0;fail<2;++fail) {
            s=(tx_control_t){.v2=true,.v3=true,.phase=stage,.heartbeat=1000,
                .feedback=true,.feedback_ms=1000,.since=1000};
            uint32_t now=fail==0?1751:fail==1?1000:1601;
            if(fail==2) s.heartbeat=now;
            assert(tx_step(&s,now,fail!=1,true,preview,out));
            assert(s.phase==TX_STOPPING&&out[2]==1000);
            tx_encode(&s,out,b);assert(b[3]==HC_DISARM);
        }
    }
    // Reserved uplink fields, undefined versions/commands and invalid stages reject.
    p=(hc_packet_t){.version=3,.session=1,.command=HC_CONTROL};
    for(unsigned i=0;i<6;++i) {
        hc_packet_t bad=p;
        if(i==0) bad.state=1;
        if(i==1) bad.height=1;
        if(i==2) bad.command=HC_RESUME+1;
        if(i==3) bad.assist=4;
        if(i==4) bad.version=4;
        if(i==5) {bad.command=HC_STATUS;bad.state=8;}
        hc_encode(&bad,b);assert(!hc_decode(b,&bad));
    }
}
static void test_feedback_diagnostics(void)
{
    tx_control_t s={0};
    assert(tx_start_v3(&s,100,0xabcdef01));
    s.sequence=30;
    hc_packet_t h={.version=3,.command=HC_STATUS,.session=s.session,.sequence=30,
                   .channels={0,HC_OK,1412,HC_DISARM,145,0,0,2}};
    uint8_t response[32];
    hc_encode(&h,response);tx_feedback(&s,response,UINT32_MAX-99);
    assert(s.air_frames==1&&s.air_gap_ms==0&&s.air_max_gap_ms==0);
    assert(s.air_rx_count==145&&s.air_bad==2&&s.air_thr==1412&&s.air_seq==30);

    // Invalid CRC, wrong session/command, future or stale sequence must not
    // inflate the count or refresh the last valid reply timestamp/data.
    for(unsigned invalid=0;invalid<6;++invalid) {
        hc_packet_t bad=h;
        bad.channels[2]=1900;bad.channels[4]=999;
        if(invalid==1) ++bad.session;
        if(invalid==2) bad.command=HC_CONTROL;
        if(invalid==3) bad.sequence=31;
        if(invalid==4) bad.sequence=29;
        if(invalid==5) bad.sequence=13;
        hc_encode(&bad,response);
        if(invalid==0) response[10]^=1;
        tx_feedback(&s,response,20);
        assert(s.air_frames==1&&s.feedback_ms==UINT32_MAX-99);
        assert(s.air_gap_ms==0&&s.air_max_gap_ms==0);
        assert(s.air_thr==1412&&s.air_rx_count==145&&s.air_seq==30);
    }
    hc_encode(&h,response);tx_feedback(&s,response,30); // Duplicate is also ignored.
    assert(s.air_frames==1&&s.feedback_ms==UINT32_MAX-99);

    s.sequence=31;h.sequence=31;h.channels[2]=1430;h.channels[4]=151;
    hc_encode(&h,response);tx_feedback(&s,response,50);
    assert(s.air_frames==2&&s.air_gap_ms==150&&s.air_max_gap_ms==150);
    assert(s.air_thr==1430&&s.air_rx_count==151&&s.air_seq==31);
    s.sequence=32;h.sequence=32;h.channels[7]=3;
    hc_encode(&h,response);tx_feedback(&s,response,75);
    assert(s.air_frames==3&&s.air_gap_ms==25&&s.air_max_gap_ms==150&&s.air_bad==3);

    // START3 starts an independent session, including all diagnostic counters.
    tx_fault(&s);assert(tx_start_v3(&s,100,0xabcdef02));
    assert(!s.feedback&&s.feedback_ms==0&&s.air_frames==0);
    assert(s.air_rx_count==0&&s.air_bad==0&&s.air_thr==0&&s.air_seq==0);
    assert(s.air_gap_ms==0&&s.air_max_gap_ms==0);
    s.sequence=32;tx_feedback(&s,response,110); // Previous session's valid packet.
    assert(!s.feedback&&s.air_frames==0);

    h.session=s.session;h.sequence=65535;s.sequence=65535;
    hc_encode(&h,response);tx_feedback(&s,response,120);
    assert(s.air_frames==1&&s.air_gap_ms==0&&s.air_seq==65535);
    h.sequence=0;s.sequence=0;
    hc_encode(&h,response);tx_feedback(&s,response,170);
    assert(s.air_frames==2&&s.air_gap_ms==50&&s.air_max_gap_ms==50&&s.air_seq==0);
}

static void test_loss_and_manual_recovery(void)
{
    tx_control_t s={.v2=true,.v3=true,.phase=TX_ALT_LIVE,.heartbeat=1000,
        .feedback=true,.feedback_ms=1000,.session=123,.since=1000,.sequence=50};
    uint8_t b[32];hc_packet_t p;
    /* Drop only downlink for 1 second: live control and sequence keep advancing. */
    for(unsigned now=1050;now<=2000;now+=50) {
        s.heartbeat=now;
        assert(tx_step(&s,now,true,true,preview,out));
        assert(s.phase==TX_ALT_LIVE&&out[2]==preview[2]);
        assert(s.link_warning==(now<1300?0:now<1600?1:2));
        tx_encode(&s,out,b);assert(hc_decode(b,&p)&&p.command==HC_CONTROL);
    }
    altitude_reply(&s,2050,HC_STAGE_HOLD,HC_CONTROL,3,HC_OK);
    for(unsigned now=2100;now<=2500;now+=50) {
        altitude_reply(&s,now,HC_STAGE_HOLD,HC_CONTROL,3,HC_OK);
        assert(s.link_warning==2);
    }
    altitude_reply(&s,2550,HC_STAGE_HOLD,HC_CONTROL,3,HC_OK);
    assert(s.link_warning==0&&s.phase==TX_ALT_LIVE);
    /* Height degradation accepts live gestures; RESUME requires explicit ready feedback. */
    altitude_reply(&s,2600,HC_STAGE_MANUAL,HC_CONTROL,1,HC_HEIGHT);
    s.heartbeat=2600;
    assert(s.phase==TX_ALT_MANUAL&&!tx_can_resume(&s,2600,true));
    assert(tx_step(&s,2600,true,true,preview,out)&&out[2]==preview[2]);
    altitude_reply(&s,2650,HC_STAGE_MANUAL|HC_STATE_RESUME_READY,HC_CONTROL,1,HC_HEIGHT);
    assert(!tx_can_resume(&s,2650,false));assert(tx_can_resume(&s,2650,true));
    assert(tx_resume(&s,2650,true));assert(!tx_resume(&s,2650,true));
    assert(tx_step(&s,2650,true,true,preview,out)&&out[2]==1500&&out[0]==1500);
    tx_encode(&s,out,b);assert(hc_decode(b,&p)&&p.command==HC_RESUME);
    altitude_reply(&s,2700,HC_STAGE_HOLD,HC_RESUME,3,HC_OK);
    assert(s.phase==TX_ALT_LIVE);
    /* Diagnostic replies count as reception but never overwrite height details. */
    p=(hc_packet_t){.version=3,.command=HC_DIAGNOSTICS,.session=s.session,.sequence=++s.sequence,
        .state=HC_STAGE_HOLD,.channels={3,0,1500,HC_CONTROL,100,49,121,HC_HEIGHT}};
    hc_encode(&p,b);tx_feedback(&s,b,2750);
    assert(s.diagnostics&&s.air_rx_gap_ms==49&&s.air_rx_max_gap_ms==121&&s.air_first_event==HC_HEIGHT);
    /* A delayed/invalid packet cannot revive the recovery dwell. */
    tx_control_t idle=s;idle.phase=TX_ALT_IDLE;
    idle.heartbeat=3350;assert(tx_step(&idle,3350,true,true,preview,out));
    assert(!tx_takeoff(&idle,3350,true)&&idle.phase==TX_ALT_IDLE);
    /* Both new phases retain STOP, IMU and USB-heartbeat protection. */
    for(unsigned stage=TX_ALT_MANUAL;stage<=TX_ALT_RESUME;++stage) {
        for(unsigned fail=0;fail<3;++fail) {
            s=(tx_control_t){.v2=true,.v3=true,.phase=stage,.heartbeat=1000,.since=1000,.session=1};
            unsigned now=fail==1?1751:1000;
            if(fail==0) tx_stop(&s,now,TX_REASON_STOP);
            assert(tx_step(&s,now,fail!=2,true,preview,out)&&s.phase==TX_STOPPING);
            tx_encode(&s,out,b);assert(b[3]==HC_DISARM&&out[2]==1000);
            for(unsigned i=0;i<15;++i)tx_sent(&s);
            s.heartbeat=now+500;assert(!tx_step(&s,now+500,true,true,preview,out)&&s.phase==TX_OFF);
        }
    }
    /* Timeout on recovery cannot silently launch or stop: remain in manual. */
    s=(tx_control_t){.v2=true,.v3=true,.phase=TX_ALT_RESUME,.heartbeat=4001,.since=1000,.session=1};
    assert(tx_step(&s,4001,true,true,preview,out)&&s.phase==TX_ALT_MANUAL);
    assert(hc_crc((const uint8_t *)"123456789",9)==0x29b1);
    assert(hc_newer(0,65535)&&!hc_newer(65535,0)&&!hc_newer(1,1));
}
static void test_air_telemetry(void)
{
    tx_control_t s={0};uint8_t b[32];hc_packet_t decoded;
    assert(tx_start_v3(&s,1000,123));
    assert(!s.telemetry&&s.air_pitch_cdeg==INT16_MIN&&s.air_distance_cm==UINT16_MAX);
    hc_packet_t p={.version=3,.command=HC_TELEMETRY,.session=123,.sequence=++s.sequence,
        .assist=HC_ATTITUDE_VALID,.channels={0,0,1000,HC_DISARM,6,(uint16_t)-1234,5678,HC_DISTANCE_UNKNOWN}};
    s.air_bad=7;s.air_collective=42;s.air_first_event=11;
    hc_encode(&p,b);tx_feedback(&s,b,1320);
    assert(s.telemetry&&s.air_telem_frames==1&&s.air_attitude_valid&&!s.air_distance_valid);
    assert(s.air_pitch_cdeg==-1234&&s.air_roll_cdeg==5678&&s.air_distance_cm==UINT16_MAX);
    assert(s.feedback_ms==1320&&s.air_bad==7&&s.air_collective==42&&s.air_first_event==11);
    assert(!s.detail&&!s.diagnostics&&s.phase==TX_READY);
    /* Each range edge is explicit. Out-of-range replies cannot refresh heartbeat. */
    p.assist=3;
    for(unsigned cm=19;cm<=721;++cm) {
        p.channels[7]=cm;hc_encode(&p,b);
        assert(hc_decode(b,&decoded)==(cm>=20&&cm<=720));
    }
    p.channels[7]=720;p.sequence=++s.sequence;hc_encode(&p,b);tx_feedback(&s,b,1620);
    assert(s.air_distance_valid&&s.air_distance_cm==720&&s.air_telem_gap_ms==300&&s.air_telem_max_gap_ms==300);
    tx_feedback(&s,b,1700);assert(s.rejected_sequence==1&&s.telemetry_ms==1620);
    p.channels[5]=HC_ATTITUDE_UNKNOWN;hc_encode(&p,b);tx_feedback(&s,b,1720);
    assert(s.rejected_schema==1&&s.feedback_ms==1620);
    p.assist=0;p.channels[7]=HC_DISTANCE_UNKNOWN;p.sequence=++s.sequence;
    hc_encode(&p,b);tx_feedback(&s,b,1920);
    assert(!s.air_attitude_valid&&!s.air_distance_valid&&s.air_pitch_cdeg==INT16_MIN);
    p.session=999;p.sequence=++s.sequence;hc_encode(&p,b);tx_feedback(&s,b,2000);
    assert(s.rejected_session==1&&s.telemetry_ms==1920);
    p.session=123;p.sequence=s.sequence+1;hc_encode(&p,b);tx_feedback(&s,b,2010);
    assert(s.rejected_sequence==2&&s.telemetry_ms==1920);
    b[20]^=1;tx_feedback(&s,b,2020);assert(s.rejected_schema==2);
    s.phase=TX_OFF;assert(tx_start_v3(&s,3000,456));
    assert(!s.telemetry&&!s.air_telem_frames&&s.air_distance_cm==UINT16_MAX);
}
int main(void)
{
    test_air_telemetry();test_altitude_mode();test_feedback_diagnostics();test_loss_and_manual_recovery();
    puts("PASS: HC3 lifecycle, downlink-only outage, warning recovery, manual/resume, diagnostics, watchdogs, CRC/sequence");
}
