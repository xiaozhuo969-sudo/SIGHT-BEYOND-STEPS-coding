#ifndef FLOW_DIAGNOSTICS_TX_H
#define FLOW_DIAGNOSTICS_TX_H
/* Included by hand_link.c only. Captures existing state, never changes control. */
extern volatile uint8_t flow_control_status;
extern volatile uint16_t flow_target_captures;
static uint16_t fd_words[FD_PAGES][6],fd_sample_id;
static uint32_t fd_sample_at;
static uint8_t fd_next_page=FD_PAGES,fd_regular_slot;
static bool fd_started,fd_last_reply;

static uint16_t fd_scaled(float value,float scale,uint16_t *flags) {
    if(!isfinite(value)) {*flags|=FD_NONFINITE;return 0x8000;}
    value*=scale;
    if(value>32767) {*flags|=FD_CLIPPED;return 32767;}
    if(value< -32767) {*flags|=FD_CLIPPED;return (uint16_t)(int16_t)-32767;}
    return (uint16_t)(int16_t)value;
}
static void flight_diag_reset(void) {
    fd_started=fd_last_reply=false;fd_next_page=FD_PAGES;fd_regular_slot=0;
}
static bool flight_diag_due(uint32_t now) {
    return !fd_last_reply && (fd_next_page<FD_PAGES || !fd_started || now-fd_sample_at>=500);
}
/* Caller has already masked interrupts while copying the normal reply. This
 * bounded snapshot is five fixed pages; no allocation, I/O, or sensor reads. */
static void flight_diag_capture(uint32_t now) {
    uint16_t flags=flow_source_age_ms()<=100 && flow_source_good()?FD_SOURCE_FRESH:0;
    if(flow_position_ready())flags|=FD_POSITION_READY;
    if(ALL_flag.unlock)flags|=FD_UNLOCKED;
    if(ALL_flag.height_lock)flags|=FD_HEIGHT_LOCK;
    if(ALL_flag.flow_control)flags|=FD_FLOW_REQUEST;
    if(flow_control_status==PROCESS_31 && flow_position_ready() &&
       ALL_flag.unlock && ALL_flag.height_lock && ALL_flag.flow_control)flags|=FD_LOOP_ACTIVE;
    bool attitude=hand_link.imu_ok && now-hand_link.imu_ms<=100 &&
        isfinite(Angle.pitch) && isfinite(Angle.roll) && fabsf(Angle.pitch)<=180 && fabsf(Angle.roll)<=180;
    if(attitude)flags|=FD_ATTITUDE_VALID;
    fd_words[0][0]=((uint16_t)flow_data.squal<<8)|flow_control_status;
    fd_words[0][1]=flow_source_age_ms();
    fd_words[0][2]=Remote.pitch;fd_words[0][3]=Remote.roll;
    fd_words[0][4]=flow_target_captures;
    fd_words[1][0]=fd_scaled(flow_x_vel_lpf_i,10,&flags);
    fd_words[1][1]=fd_scaled(flow_y_vel_lpf_i,10,&flags);
    fd_words[1][2]=fd_scaled(flow_x_lpf_att_i,10,&flags);
    fd_words[1][3]=fd_scaled(flow_y_lpf_att_i,10,&flags);
    fd_words[1][4]=fd_scaled(pidPositionX.desired,10,&flags);
    fd_words[1][5]=fd_scaled(pidPositionY.desired,10,&flags);
    fd_words[2][0]=fd_scaled(pidPosRateX.desired,10,&flags);
    fd_words[2][1]=fd_scaled(pidPosRateY.desired,10,&flags);
    fd_words[2][2]=fd_scaled(pidPosRateX.out,100,&flags);
    fd_words[2][3]=fd_scaled(pidPosRateY.out,100,&flags);
    fd_words[2][4]=fd_scaled(pidPitch.desired,100,&flags);
    fd_words[2][5]=fd_scaled(pidRoll.desired,100,&flags);
    fd_words[3][0]=attitude?fd_scaled(Angle.pitch,100,&flags):0x8000;
    fd_words[3][1]=attitude?fd_scaled(Angle.roll,100,&flags):0x8000;
    uint16_t limits=0;
    for(unsigned i=0;i<4;i++) {
        int value=motor_PWM_Value[i];
        if(value<=0)limits|=1U<<i;
        if(value>=1000)limits|=1U<<(i+4);
        fd_words[3][i+2]=value<0?0:value>1000?1000:value;
    }
    fd_words[4][0]=(uint16_t)flow_data.vel_x;fd_words[4][1]=(uint16_t)flow_data.vel_y;
    fd_words[4][2]=flow_invalidations;fd_words[4][3]=flow_sample_count;
    fd_words[4][4]=height_fresh()?hand_link.height_mm:0xffff;
    fd_words[4][5]=limits;fd_words[0][5]=flags;
    ++fd_sample_id;fd_sample_at=now;fd_next_page=0;fd_started=true;
}
static void flight_diag_reply(uint8_t raw[32],uint32_t now) {
    if(fd_next_page>=FD_PAGES)flight_diag_capture(now);
    raw[3]=HC_FLOW_HEALTH+fd_next_page;
    hc_put16(raw+10,fd_sample_id);hc_put16(raw+12,(uint16_t)fd_sample_at);
    for(unsigned i=0;i<6;i++)hc_put16(raw+14+2*i,fd_words[fd_next_page][i]);
    raw[26]=1;raw[27]=0;
    uint32_t age=now-fd_sample_at;hc_put16(raw+28,age>65535?65535:age);
    hc_put16(raw+30,hc_crc(raw,30));++fd_next_page;fd_last_reply=true;
}
/* Round-robin only at actual normal transmissions. Unlike received%N this
 * cannot repeatedly overwrite DETAIL before it gets a radio slot. */
static void flight_diag_regular_reply(uint8_t raw[32]) {
    unsigned slot=fd_regular_slot++%3;
    raw[3]=slot==1?HC_DETAIL:slot==2?HC_DIAGNOSTICS:HC_STATUS;
    hc_put16(raw+20,0);hc_put16(raw+22,0);hc_put16(raw+24,(uint16_t)hand_link.bad);
    if(slot==1) {
        hc_put16(raw+20,height_fresh()?hand_link.height_mm:0xffff);
        hc_put16(raw+22,hand_link.collective);hc_put16(raw+24,hand_link.radio_faults);
    } else if(slot==2) {
        hc_put16(raw+20,hand_link.rx_gap_ms>65535?65535:hand_link.rx_gap_ms);
        hc_put16(raw+22,hand_link.rx_max_gap_ms>65535?65535:hand_link.rx_max_gap_ms);
        hc_put16(raw+24,hand_link.first_event);
    }
    hc_put16(raw+30,hc_crc(raw,30));fd_last_reply=false;
}
#endif
