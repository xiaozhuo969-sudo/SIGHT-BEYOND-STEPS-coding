#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "control_output.h"
#include "gesture.h"
#include "remote_protocol.h"
#include "tx_control.h"
#include "../../shared/hand_protocol.h"

static void assert_neutral(const uint16_t c[8])
{
    for(unsigned i=0;i<4;++i) assert(c[i]==1500);
}

static void test_palm_priority_and_forward_delay(void)
{
    gesture_state_t g={0};uint16_t c[8];
    gesture_calibrate(&g,0,0);
    const float poses[][2]={{0,0},{30,0},{-30,0},{0,-30},{0,30}};
    for(unsigned i=0;i<5;++i) {
        gesture_result_t r=gesture_update(&g,poses[i][0],poses[i][1],true,true,i*50);
        assert(r.signal==GESTURE_FIST);
        control_output_altitude(true,r.signal,r.valid,c);assert_neutral(c);
    }
    // Releasing the palm at flat must wait a full second before forwarding.
    gesture_result_t r=gesture_update(&g,0,0,true,false,1000);
    control_output_altitude(true,r.signal,r.valid,c);assert_neutral(c);
    r=gesture_update(&g,0,0,true,false,1999);
    control_output_altitude(true,r.signal,r.valid,c);assert_neutral(c);
    r=gesture_update(&g,0,0,true,false,2000);
    control_output_altitude(true,r.signal,r.valid,c);
    assert(c[1]==1700&&c[0]==1500&&c[2]==1500&&c[3]==1500);
    // Turning replaces forward immediately; a pass through flat stays neutral.
    r=gesture_update(&g,0,-30,true,false,2001);
    control_output_altitude(true,r.signal,r.valid,c);
    assert(c[3]==1300&&c[0]==1500&&c[1]==1500&&c[2]==1500);
    r=gesture_update(&g,0,0,true,false,2050);
    control_output_altitude(true,r.signal,r.valid,c);assert_neutral(c);
    r=gesture_update(&g,0,30,true,false,2100);
    control_output_altitude(true,r.signal,r.valid,c);assert(c[3]==1700&&c[1]==1500);
    r=gesture_update(&g,0,30,true,true,2101);
    control_output_altitude(true,r.signal,r.valid,c);assert_neutral(c);
}

static void test_forward_only_in_live_control(void)
{
    uint16_t forward[8],out[8];uint8_t bytes[32];hc_packet_t packet;
    control_output_altitude(true,GESTURE_FLAT,true,forward);
    const unsigned stages[]={TX_READY,TX_ARM_REQUEST,TX_ALT_IDLE,TX_ALT_TAKEOFF,
        TX_ALT_LIVE,TX_ALT_LAND,TX_ALT_LOST,TX_ALT_MANUAL,TX_ALT_RESUME,TX_STOPPING};
    for(unsigned i=0;i<sizeof(stages)/sizeof(stages[0]);++i) {
        const unsigned stage=stages[i];
        tx_control_t tx={.v2=true,.v3=true,.session=1,.phase=stage,
            .since=1000,.heartbeat=1000,.feedback=true,.feedback_ms=1000};
        assert(tx_step(&tx,1000,true,true,forward,out));
        tx_encode(&tx,out,bytes);assert(hc_decode(bytes,&packet));
        const bool live=stage==TX_ALT_LIVE||stage==TX_ALT_MANUAL;
        assert(packet.channels[1]==(live?1700:1500));
        assert(packet.channels[0]==1500&&packet.channels[3]==1500);
        if(live) {
            assert(packet.command==HC_CONTROL);
            uint16_t palm[8];control_output_altitude(true,GESTURE_FIST,true,palm);
            assert(tx_step(&tx,1000,true,false,palm,out));assert_neutral(out);
        }
    }
}

static void test_swapped_default_directions(void)
{
    gesture_state_t g=GESTURE_INITIALIZER;
    gesture_calibrate(&g,5,-7); // Startup flat zero must preserve the new mapping.
    const struct { float p,r; unsigned signal,axis,value; } cases[]={
        {5,-37,GESTURE_UP,2,1800},       // Previous left -> up.
        {35,-7,GESTURE_LEFT,3,1300},     // Previous up -> left yaw.
        {5,23,GESTURE_DOWN,2,1100},      // Previous right -> down.
        {-25,-7,GESTURE_RIGHT,3,1700},   // Previous down -> right yaw.
    };
    uint16_t c[8];
    for(unsigned i=0;i<4;++i) {
        gesture_result_t r=gesture_update(&g,cases[i].p,cases[i].r,true,false,i*50);
        assert(r.valid&&r.signal==cases[i].signal);
        control_output_altitude(true,r.signal,r.valid,c);
        for(unsigned axis=0;axis<4;++axis)
            assert(c[axis]==(axis==cases[i].axis?cases[i].value:1500));
        r=gesture_update(&g,cases[i].p,cases[i].r,true,true,i*50+1);
        control_output_altitude(true,r.signal,r.valid,c);assert_neutral(c);
    }
    gesture_result_t r=gesture_update(&g,5,-7,true,false,200);
    control_output_altitude(true,r.signal,r.valid,c);assert_neutral(c);
    r=gesture_update(&g,5,-7,true,false,1199);
    control_output_altitude(true,r.signal,r.valid,c);assert_neutral(c);
    r=gesture_update(&g,5,-7,true,false,1200);
    assert(r.signal==GESTURE_FLAT);
    control_output_altitude(true,r.signal,r.valid,c);assert(c[1]==1700);
    gesture_reset(&g);
    assert(g.invert_pitch&&g.invert_roll&&g.swap_axes);
    // Existing manual axis configuration still permits restoring the old mapping.
    gesture_set_direction(&g,false,false,false);
    assert(gesture_update(&g,0,-30,true,false,1300).signal==GESTURE_LEFT);
}

int main(void)
{
    uint16_t c[8];
    for(unsigned yaw=0;yaw<2;++yaw) {
        for(unsigned signal=0;signal<=GESTURE_FIST;++signal) {
            control_output_altitude(yaw,signal,true,c);
            assert(c[2]==(signal==GESTURE_UP?1800:signal==GESTURE_DOWN?1100:1500));
            unsigned active=0;
            for(unsigned axis=0;axis<4;++axis) if(c[axis]!=1500) ++active;
            assert(active==((signal==GESTURE_FIST||signal==GESTURE_UNKNOWN)?0:1));
            if(signal==GESTURE_LEFT) assert(c[yaw?3:0]==1300);
            if(signal==GESTURE_RIGHT) assert(c[yaw?3:0]==1700);
            if(signal==GESTURE_FLAT) assert(c[1]==1700);
            if(signal==GESTURE_FIST) assert_neutral(c);
        }
        control_output_altitude(yaw,GESTURE_UP,true,c);
        control_output_altitude(yaw,GESTURE_UNKNOWN,true,c);
        assert(c[2]==1500);
        control_output_altitude(yaw,GESTURE_UP,false,c);
        assert(c[0]==1500&&c[1]==1500&&c[2]==1500&&c[3]==1500);
    }
    test_palm_priority_and_forward_delay();test_forward_only_in_live_control();
    test_swapped_default_directions();
    puts("PASS: swapped default directions, palm holds, flat waits 1s then forwards, yaw/roll selection, one axis, flight stage gates");
}
