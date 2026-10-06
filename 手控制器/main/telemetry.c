#include <fcntl.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "telemetry.h"
#include "ppg_window.h"
#include "vibration.h"
#include "gesture.h"
#include "gyro_calibration.h"
#include "control_output.h"
#include "tx_control.h"
#include "flight_keys.h"
#include "led_demo.h"
#include "esp_random.h"
#include "../../shared/hand_protocol.h"

static const char *TAG = "telemetry";
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

typedef struct {
    float pitch;
    float roll;
    ppg_result_t spo2;
    int samples;
    unsigned presses;
    unsigned pattern;
    unsigned motor[2];
    uint32_t red;
    uint32_t ir;
    uint32_t ppg_samples;
    float ppg_correlation, ppg_quality;
    uint32_t ppg_contact_resets;
    bool imu;
    unsigned imu_cal, imu_cal_progress;
    float gyro_bias[3];
    bool max_ready;
    bool ppg_ok;
    bool finger;
    bool button;
    bool hold_button;
    unsigned hold_presses;
    bool leds;
    int64_t attitude_us;
    int64_t ppg_us;
    bool rc_ready;
    uint16_t rc[8];
    uint32_t rc_count, rc_bad;
    int64_t rc_us;
} live_state_t;

static live_state_t s_state = {.leds = true};
static gesture_state_t s_gesture = GESTURE_INITIALIZER;
static bool s_boot_zero_pending = true;
static gesture_result_t s_control_gesture;
static flight_keys_t s_keys;
static bool s_yaw_axis=true;
static tx_control_t s_tx = {.v2=true,.v3=true};
static uint16_t s_preview[8], s_actual[8];
static unsigned s_signal;
static int64_t s_preview_us;
static uint32_t s_tx_count, s_tx_errors;
static uint32_t s_tx_rx_us, s_tx_rx_max_us, s_tx_rx_late;
static bool s_radio_tx;
static uint8_t s_actual_packet[32];
static int s_led_test_pin=-1;
static uint32_t s_led_test_ms;

bool telemetry_tx_frame(uint8_t packet[32])
{
    const int64_t now = esp_timer_get_time();
    uint16_t channels[8];
    portENTER_CRITICAL(&s_mux);
    const bool fresh = s_state.imu && now - s_state.attitude_us < 500000 &&
                       s_preview_us && now - s_preview_us < 300000 &&
                       isfinite(s_state.pitch) && isfinite(s_state.roll);
    if(flight_stop_pressed()) {
        flight_keys_edge(&s_keys,&s_tx,1,true,(uint32_t)(now/1000));
        flight_keys_stop(&s_keys,&s_tx,(uint32_t)(now/1000));
    }
    const bool active = tx_step(&s_tx, (uint32_t)(now / 1000), fresh,
                               s_signal == GESTURE_FLAT, s_preview, channels);
    s_radio_tx = active;
    if (active) {
        memcpy(s_actual, channels, sizeof(s_actual));
        tx_encode(&s_tx,channels,packet);
        memcpy(s_actual_packet,packet,32);
    }
    portEXIT_CRITICAL(&s_mux);

    return active;
}

unsigned telemetry_tx_interval_ms(void)
{
    return 50;
}
void telemetry_tx_rx_time(uint32_t microseconds)
{
    portENTER_CRITICAL(&s_mux);
    s_tx_rx_us=microseconds;
    if(microseconds>s_tx_rx_max_us) s_tx_rx_max_us=microseconds;
    if(microseconds>=20000) ++s_tx_rx_late;
    portEXIT_CRITICAL(&s_mux);
}
void telemetry_air_packet(const uint8_t packet[32])
{
    portENTER_CRITICAL(&s_mux);
    tx_feedback(&s_tx,packet,(uint32_t)(esp_timer_get_time()/1000));
    portEXIT_CRITICAL(&s_mux);
}

void telemetry_tx_result(bool success)
{
    portENTER_CRITICAL(&s_mux);
    if (success) { ++s_tx_count; tx_sent(&s_tx); }
    else { ++s_tx_errors; tx_fault(&s_tx); s_radio_tx = false; }
    portEXIT_CRITICAL(&s_mux);
}


void telemetry_set_imu_calibration(unsigned status, unsigned progress, const float bias[3])
{
    portENTER_CRITICAL(&s_mux);
    s_state.imu_cal = status;
    s_state.imu_cal_progress = progress;
    if (bias) memcpy(s_state.gyro_bias, bias, sizeof(s_state.gyro_bias));
    if (status != GYRO_CAL_READY) s_state.imu = false;
    portEXIT_CRITICAL(&s_mux);
}

void telemetry_set_attitude(float pitch, float roll)
{
    portENTER_CRITICAL(&s_mux);
    s_state.pitch = pitch;
    s_state.roll = roll;
    s_state.imu = true;
    s_state.attitude_us = esp_timer_get_time();
    if (s_boot_zero_pending && s_state.imu_cal == GYRO_CAL_READY && tx_can_zero(&s_tx)) {
        if (!s_gesture.calibrated) {
            gesture_calibrate(&s_gesture, pitch, roll);
            s_signal = GESTURE_UNKNOWN;
        }
        s_boot_zero_pending = false;
    }
    portEXIT_CRITICAL(&s_mux);
}

void telemetry_set_spo2(bool finger, bool valid, float spo2, int samples, bool new_result)
{
    portENTER_CRITICAL(&s_mux);
    s_state.finger = finger;
    s_state.samples = samples;
    ppg_result_update(&s_state.spo2, finger, valid, spo2,
                      new_result ? esp_timer_get_time() : s_state.spo2.updated_us);
    portEXIT_CRITICAL(&s_mux);
}

void telemetry_set_ppg(bool ready, bool reading_ok, uint32_t red, uint32_t ir, unsigned samples)
{
    portENTER_CRITICAL(&s_mux);
    s_state.max_ready = ready;
    s_state.ppg_ok = reading_ok;
    s_state.red = reading_ok ? red : 0;
    s_state.ir = reading_ok ? ir : 0;
    if (reading_ok) s_state.ppg_samples += samples;
    s_state.ppg_us = reading_ok ? esp_timer_get_time() : 0;
    portEXIT_CRITICAL(&s_mux);
}

void telemetry_set_ppg_quality(float correlation, float quality, uint32_t contact_resets)
{
    portENTER_CRITICAL(&s_mux);
    s_state.ppg_correlation=isfinite(correlation)?correlation:0;
    s_state.ppg_quality=isfinite(quality)?quality:0;
    s_state.ppg_contact_resets=contact_resets;
    portEXIT_CRITICAL(&s_mux);
}

void telemetry_hud_snapshot(hud_sample_t *sample)
{
    live_state_t state; tx_control_t tx;
    portENTER_CRITICAL(&s_mux);
    state=s_state; tx=s_tx;
    portEXIT_CRITICAL(&s_mux);
    const int64_t now=esp_timer_get_time();
    *sample=hud_empty();
    if (state.finger && (state.spo2.valid || state.spo2.held) && state.ppg_ok && state.spo2.updated_us &&
        now-state.ppg_us<250000 && isfinite(state.spo2.value)) {
        sample->spo2_x10=(int32_t)lroundf(state.spo2.value*10);
        sample->spo2_age_ms=hud_age((now-state.spo2.updated_us)/1000);
    }
    if (tx.telemetry) {
        sample->air_age_ms=hud_age((uint32_t)((uint32_t)(now/1000)-tx.telemetry_ms));
        if (tx.air_attitude_valid) {
            sample->pitch_cdeg=tx.air_pitch_cdeg; sample->roll_cdeg=tx.air_roll_cdeg;
        }
        if (tx.air_distance_valid && tx.air_distance_cm!=UINT16_MAX) sample->distance_cm=tx.air_distance_cm;
    }
}

void telemetry_flight_keys_init(bool start_down, bool stop_down)
{
    portENTER_CRITICAL(&s_mux);
    flight_keys_init(&s_keys,&s_tx,start_down,stop_down,(uint32_t)(esp_timer_get_time()/1000));
    portEXIT_CRITICAL(&s_mux);
}

void telemetry_flight_key(unsigned key, bool pressed)
{
    portENTER_CRITICAL(&s_mux);
    if(key==0 && s_led_test_pin>=0) { portEXIT_CRITICAL(&s_mux); return; }
    flight_keys_edge(&s_keys,&s_tx,key,pressed,(uint32_t)(esp_timer_get_time()/1000));
    portEXIT_CRITICAL(&s_mux);
}

void telemetry_hold_key_init(bool pressed)
{
    portENTER_CRITICAL(&s_mux);
    s_state.hold_button=pressed;
    portEXIT_CRITICAL(&s_mux);
}

void telemetry_hold_key(bool pressed)
{
    portENTER_CRITICAL(&s_mux);
    bool edge=pressed && !s_state.hold_button;
    s_state.hold_button=pressed;
    if(edge) {
        ++s_state.hold_presses;
        if(!s_keys.down[1] && !s_keys.stop_latched)
            tx_toggle_height_hold(&s_tx,(uint32_t)(esp_timer_get_time()/1000));
        else s_tx.hold_reason=HOLD_NOT_FLYING;
        if(s_tx.height_hold) for(unsigned i=0;i<4;++i) s_preview[i]=1500;
    }
    bool hold=s_tx.height_hold;unsigned reason=s_tx.hold_reason;
    portEXIT_CRITICAL(&s_mux);
    if(edge) ESP_LOGI(TAG,"GPIO47 hold press: active=%u reason=%u",hold,reason);
}

void telemetry_set_button(bool pressed)
{
    portENTER_CRITICAL(&s_mux);
    if(pressed && !s_state.button && !s_keys.down[1] && !s_keys.stop_latched)
        tx_clear_obstacle(&s_tx,(uint32_t)(esp_timer_get_time()/1000));
    s_state.button = pressed;
    /* A palm edge can clear the local latch before the next 50 ms gesture tick. */
    if(pressed) for(unsigned i=0;i<4;++i) s_preview[i]=1500;
    if (pressed) ++s_state.presses;
    portEXIT_CRITICAL(&s_mux);
}

void telemetry_indicator_snapshot(bool *air, bool *obstacle, unsigned *gesture, int *test_pin)
{
    uint32_t now=(uint32_t)(esp_timer_get_time()/1000);
    portENTER_CRITICAL(&s_mux);
    bool fresh=s_tx.feedback && s_tx.phase!=TX_OFF && s_tx.phase!=TX_FAULT &&
               s_tx.phase!=TX_STOPPING && (uint32_t)(now-s_tx.feedback_ms)<300;
    *air=fresh && !s_tx.link_warning;
    *obstacle=s_tx.guard.hold;
    *gesture=s_control_gesture.valid && s_state.imu &&
        esp_timer_get_time()-s_state.attitude_us<500000 ? s_signal : GESTURE_UNKNOWN;
    if(s_tx.guard.hold && !(s_tx.phase==TX_ALT_MANUAL && *gesture==GESTURE_DOWN))
        *gesture=GESTURE_UNKNOWN;
    if(s_tx.height_hold) *gesture=GESTURE_UNKNOWN;
    *test_pin=s_led_test_pin;
    portEXIT_CRITICAL(&s_mux);
}
void telemetry_set_led(bool enabled, unsigned pattern)
{
    portENTER_CRITICAL(&s_mux);
    s_state.leds = enabled;
    s_state.pattern = pattern;
    portEXIT_CRITICAL(&s_mux);
}

void telemetry_set_motor(unsigned channel, unsigned strength)
{
    if (channel >= 2) return;
    portENTER_CRITICAL(&s_mux);
    s_state.motor[channel] = strength;
    portEXIT_CRITICAL(&s_mux);
}

void telemetry_set_radio_ready(bool ready)
{
    portENTER_CRITICAL(&s_mux);
    s_state.rc_ready = ready;
    if (!ready) {
        s_state.rc_us = 0;
        if (s_tx.phase != TX_OFF && s_tx.phase != TX_FAULT) tx_fault(&s_tx);
        s_radio_tx = false;
    }
    portEXIT_CRITICAL(&s_mux);
}

void telemetry_radio_packet(const uint16_t channels[8])
{
    portENTER_CRITICAL(&s_mux);
    if (channels) {
        memcpy(s_state.rc, channels, sizeof(s_state.rc));
        ++s_state.rc_count;
        s_state.rc_us = esp_timer_get_time();
    } else {
        ++s_state.rc_bad;
    }
    portEXIT_CRITICAL(&s_mux);
}

// Flight decisions stay independent of USB printf and upper-computer heartbeats.
static void control_task(void *arg)
{
    (void)arg;
    TickType_t wake=xTaskGetTickCount();
    for(;;) {
        const int64_t now=esp_timer_get_time();
        const uint32_t now_ms=(uint32_t)(now/1000), session=esp_random();
        const bool hard_stop=flight_stop_pressed();
        portENTER_CRITICAL(&s_mux);
        if(hard_stop) flight_keys_edge(&s_keys,&s_tx,1,true,now_ms);
        const bool fresh=s_state.imu && now-s_state.attitude_us<500000 &&
                         isfinite(s_state.pitch) && isfinite(s_state.roll);
        flight_keys_autoconnect(&s_keys,&s_tx,now_ms,session,s_state.rc_ready,fresh);
        if(s_led_test_pin>=0 && (uint32_t)(now_ms-s_led_test_ms)>=10000) s_led_test_pin=-1;
        if(s_led_test_pin<0 && flight_keys_begin(&s_keys,&s_tx,now_ms,session,s_state.rc_ready,fresh,s_state.button)) {
            // First physical launch records the hand's resting orientation.
            // Existing calibration/direction settings are preserved thereafter.
            if(!s_gesture.calibrated && tx_can_zero(&s_tx))
                gesture_calibrate(&s_gesture,s_state.pitch,s_state.roll);
        }
        s_control_gesture=gesture_update(&s_gesture,s_state.pitch,s_state.roll,fresh,s_state.button,now_ms);
        s_signal=s_control_gesture.signal;
        control_output_altitude(s_yaw_axis,s_signal,s_control_gesture.valid,s_preview);
        s_preview_us=now;
        flight_keys_tick(&s_keys,&s_tx,now_ms,s_state.rc_ready,fresh,
            s_gesture.calibrated && s_signal==GESTURE_FLAT,
            s_gesture.calibrated && s_gesture.phase==1,s_state.button);
        tx_guard_refresh(&s_tx,now_ms);
        if(s_tx.height_hold) for(unsigned i=0;i<4;++i) s_preview[i]=1500;
        bool obstacle_alarm=s_tx.guard.hold;
        bool tilt_alarm=s_tx.guard.tilt;
        portEXIT_CRITICAL(&s_mux);
        // Outside telemetry's lock: motor publication takes this same lock.
        // Stale telemetry leaves local alarms latched; no artificial rising edges.
        vibration_alerts(obstacle_alarm,tilt_alarm);
        xTaskDelayUntil(&wake,pdMS_TO_TICKS(50));
    }
}

static void telemetry_task(void *arg)
{
    (void)arg;
    unsigned previous_gesture = ~0U, previous_key_status=~0U, previous_key_reason=~0U;
    int64_t rate_since = esp_timer_get_time();
    uint32_t rate_count = 0;
    float rc_hz = 0;
    int64_t alert_at=0, key_raw_at=0;
    unsigned previous_key_raw=~0U;
    TickType_t publish_at=xTaskGetTickCount();
    for (;;) {
        live_state_t state;
        gesture_state_t settings;
        flight_keys_t keys;
        gesture_result_t gesture;
        bool yaw_axis;
        uint16_t output[8];
        unsigned tx_phase, tx_reason;
        uint32_t tx_count, tx_errors, tx_rx_us, tx_rx_max_us, tx_rx_late;
        bool radio_tx;
        tx_control_t tx;
        uint8_t packet[32];
        const int64_t now = esp_timer_get_time();
        portENTER_CRITICAL(&s_mux);
        state = s_state;
        state.ppg_ok = state.ppg_ok && state.ppg_us && now-state.ppg_us < 250000;
        state.spo2.valid = state.spo2.valid && state.ppg_ok;
        state.spo2.held = state.spo2.held && state.ppg_ok;
        state.imu = state.imu && now - state.attitude_us < 500000 &&
                    isfinite(state.pitch) && isfinite(state.roll);
        gesture = s_control_gesture;
        settings = s_gesture;
        keys = s_keys;
        yaw_axis = s_yaw_axis;
        memcpy(output,s_preview,sizeof(output));
        radio_tx = s_radio_tx;
        tx_phase = s_tx.phase; tx_reason = s_tx.reason;
        tx_count = s_tx_count; tx_errors = s_tx_errors;
        tx_rx_us=s_tx_rx_us;tx_rx_max_us=s_tx_rx_max_us;tx_rx_late=s_tx_rx_late;
        tx=s_tx;memcpy(packet,s_actual_packet,32);
        if (radio_tx) memcpy(output, s_actual, sizeof(output));
        portEXIT_CRITICAL(&s_mux);
        const unsigned key_raw=flight_keys_raw_levels();
        if(key_raw!=previous_key_raw || now-key_raw_at>=2000000) {
            ESP_LOGI("flight_keys","raw GPIO10=%u GPIO11=%u (0=pressed, 1=released)",
                     key_raw&1U,(key_raw>>1)&1U);
            previous_key_raw=key_raw; key_raw_at=now;
        }
        bool alert=radio_tx&&(tx.link_warning==2||tx.phase==TX_ALT_MANUAL);
        if(alert&&(!alert_at||now-alert_at>=2000000)) {
            vibration_pulse(0,60,150);alert_at=now;
        } else if(!alert) alert_at=0;
        if(keys.status!=previous_key_status || keys.reason!=previous_key_reason) {
            ESP_LOGI("flight_keys","status=%u reason=%u local=%u start=%u stop=%u",
                     keys.status,keys.reason,keys.local_owner,keys.down[0],keys.down[1]);
            previous_key_status=keys.status; previous_key_reason=keys.reason;
        }
        if (gesture.signal != previous_gesture) {
            ESP_LOGI("gesture", "signal=%u pitch=%.1f roll=%.1f fist=%u valid=%u",
                     gesture.signal, gesture.pitch, gesture.roll, state.button, gesture.valid);
            previous_gesture = gesture.signal;
        }
        if (!isfinite(state.pitch)) state.pitch = 0;
        if (!isfinite(state.roll)) state.roll = 0;
        const long rc_age = state.rc_us ? (long)((now - state.rc_us) / 1000) : -1;
        const bool rc_valid = state.rc_ready && rc_age >= 0 && rc_age < 500;
        if (now - rate_since >= 1000000) {
            rc_hz = (state.rc_count - rate_count) * 1000000.0f / (now - rate_since);
            rate_count = state.rc_count;
            rate_since = now;
        }
        char output_hex[65];
        if (!radio_tx) {
            hc_packet_t preview={.version=3,.session=tx.session,.command=HC_CONTROL,.assist=3};
            memcpy(preview.channels,output,sizeof output);hc_encode(&preview,packet);
        }
        for (unsigned i = 0; i < 32; ++i) snprintf(output_hex + i * 2, 3, "%02X", packet[i]);
        // During TX show the latest scheduled radio frame; otherwise show the preview.
        // One line and one printf per frame. The prefix lets the PC ignore boot/log text.
        printf("@HC:{\"v\":1,\"ms\":%lu,\"imu\":%u,\"pitch\":%.1f,\"roll\":%.1f,"
               "\"imu_cal\":%u,\"imu_cal_progress\":%u,\"gyro_bias_x\":%.3f,\"gyro_bias_y\":%.3f,\"gyro_bias_z\":%.3f,"
               "\"max_ready\":%u,\"ppg_ok\":%u,\"red\":%lu,\"ir\":%lu,"
               "\"finger\":%u,\"spo2_valid\":%u,\"spo2\":%.1f,\"samples\":%d,"
               "\"ppg_samples\":%lu,\"ppg_age_ms\":%ld,\"spo2_age_ms\":%ld,\"spo2_stream\":1,"
               "\"spo2_held\":%u,\"ppg_correlation\":%.2f,\"ppg_quality\":%.2f,\"ppg_contact_resets\":%lu,"
               "\"button\":%u,\"presses\":%u,\"leds\":%u,\"pattern\":%u,"
               "\"key_start\":%u,\"key_stop\":%u,\"key_start_count\":%u,\"key_stop_count\":%u,"
               "\"key_status\":%u,\"key_reason\":%u,\"key_local\":%u,\"key_pending\":%u,\"key_stop_latched\":%u,\"key_auto\":%u,"
               "\"key_hold\":%u,\"key_hold_count\":%u,\"height_hold\":%u,\"hold_reason\":%u,"
               "\"motor0\":%u,\"motor1\":%u,"
               "\"gesture\":%u,\"fist\":%u,\"gpitch\":%.1f,\"groll\":%.1f,"
               "\"gvalid\":%u,\"gcal\":%u,\"ginvp\":%u,\"ginvr\":%u,\"gswap\":%u,\"gphase\":%u,\"gconfirm\":%u,"
               "\"rc_ready\":%u,\"rc_valid\":%u,\"rc_count\":%lu,\"rc_bad\":%lu,"
               "\"rc_age_ms\":%ld,\"rc_hz\":%.1f,\"rc\":[%u,%u,%u,%u,%u,%u,%u,%u],"
               "\"out_valid\":%u,\"out_axis\":%u,\"out_rate\":%u,\"out_tx\":%u,\"tx_phase\":%u,\"tx_reason\":%u,\"tx_count\":%lu,\"tx_errors\":%lu,"
               "\"tx_v2\":%u,\"tx_can_arm\":%u,\"air_valid\":%u,\"air_flags\":%u,\"air_reason\":%u,\"air_cmd\":%u,"
               "\"air_rx_count\":%u,\"air_bad\":%u,\"air_thr\":%u,\"air_seq\":%u,\"air_frames\":%lu,"
               "\"air_age_ms\":%ld,\"air_gap_ms\":%lu,\"air_max_gap_ms\":%lu,"
               "\"tx_v3\":%u,\"tx_can_takeoff\":%u,\"air_stage\":%u,\"air_height_valid\":%u,\"air_ground_ready\":%u,"
               "\"air_height_mm\":%u,\"air_relative_mm\":%d,\"air_collective\":%u,\"air_radio_faults\":%u,\"air_detail_age_ms\":%ld,"
               "\"link_warning\":%u,\"tx_can_resume\":%u,\"air_resume_ready\":%u,"
               "\"guard_hold\":%u,\"guard_ready\":%u,\"tilt_alarm\":%u,"
               "\"tx_rx_us\":%lu,\"tx_rx_max_us\":%lu,\"tx_rx_late\":%lu,"
               "\"reject_schema\":%lu,\"reject_session\":%lu,\"reject_sequence\":%lu,"
               "\"air_rx_gap_ms\":%u,\"air_rx_max_gap_ms\":%u,\"air_first_event\":%u,\"air_diag_age_ms\":%ld,"
               "\"air_telem_received\":%u,\"air_attitude_valid\":%u,\"air_pitch_cdeg\":%d,\"air_roll_cdeg\":%d,"
               "\"air_distance_valid\":%u,\"air_distance_cm\":%u,\"air_telem_age_ms\":%ld,"
               "\"air_telem_frames\":%lu,\"air_telem_gap_ms\":%lu,\"air_telem_max_gap_ms\":%lu,"
               "\"out\":[%u,%u,%u,%u,%u,%u,%u,%u],\"out_hex\":\"%s\"}\n",
               (unsigned long)(esp_timer_get_time() / 1000), state.imu, state.pitch,
               state.roll, state.imu_cal, state.imu_cal_progress,
               state.gyro_bias[0], state.gyro_bias[1], state.gyro_bias[2], state.max_ready, state.ppg_ok,
               (unsigned long)state.red, (unsigned long)state.ir,
               state.finger, state.spo2.valid, state.spo2.value,
               state.samples, (unsigned long)state.ppg_samples,
               state.ppg_ok?(long)((now-state.ppg_us)/1000):-1L,
               (state.spo2.valid||state.spo2.held)&&state.spo2.updated_us?(long)((now-state.spo2.updated_us)/1000):-1L,
               state.spo2.held, state.ppg_correlation, state.ppg_quality, (unsigned long)state.ppg_contact_resets,
               state.button, state.presses, state.leds,
               state.pattern, keys.down[0],keys.down[1],keys.presses[0],keys.presses[1],
               keys.status,keys.reason,keys.local_owner,keys.pending,keys.stop_latched,keys.auto_connect,
               state.hold_button,state.hold_presses,tx.height_hold,tx.hold_reason,
               state.motor[0], state.motor[1],
               gesture.signal, state.button, gesture.pitch, gesture.roll,
               gesture.valid, settings.calibrated, settings.invert_pitch,
               settings.invert_roll, settings.swap_axes, settings.phase, settings.pending != GESTURE_UNKNOWN,
               state.rc_ready, rc_valid, (unsigned long)state.rc_count, (unsigned long)state.rc_bad,
               rc_age, rc_valid ? rc_hz : 0,
               state.rc[0], state.rc[1], state.rc[2], state.rc[3],
               state.rc[4], state.rc[5], state.rc[6], state.rc[7],
               gesture.valid || radio_tx, yaw_axis, 50U,
               radio_tx, tx_phase, tx_reason, (unsigned long)tx_count, (unsigned long)tx_errors,
               tx.v2, tx_can_arm(&tx,(uint32_t)(now/1000)),
               tx.feedback && (uint32_t)(now/1000-tx.feedback_ms)<600,
               tx.air_flags,tx.air_reason,tx.air_command,
               tx.air_rx_count,tx.air_bad,tx.air_thr,tx.air_seq,(unsigned long)tx.air_frames,
               tx.feedback?(long)(uint32_t)((uint32_t)(now/1000)-tx.feedback_ms):-1L,
               (unsigned long)tx.air_gap_ms,(unsigned long)tx.air_max_gap_ms,
               tx.v3,tx_can_takeoff(&tx,(uint32_t)(now/1000),gesture.valid&&settings.calibrated&&gesture.signal==GESTURE_FLAT),
               tx.air_stage,tx.air_height_valid,tx.air_ground_ready,tx.air_height_mm,tx.air_relative_mm,
               tx.air_collective,tx.air_radio_faults,tx.detail?(long)(uint32_t)((uint32_t)(now/1000)-tx.detail_ms):-1L,
               tx.link_warning,tx_can_resume(&tx,(uint32_t)(now/1000),gesture.valid&&settings.calibrated&&gesture.signal==GESTURE_FLAT),tx.resume_ready,
               tx.guard.hold,tx.guard.ready,tx.guard.tilt,
               (unsigned long)tx_rx_us,(unsigned long)tx_rx_max_us,(unsigned long)tx_rx_late,
               (unsigned long)tx.rejected_schema,(unsigned long)tx.rejected_session,(unsigned long)tx.rejected_sequence,
               tx.air_rx_gap_ms,tx.air_rx_max_gap_ms,tx.air_first_event,
               tx.diagnostics?(long)(uint32_t)((uint32_t)(now/1000)-tx.diagnostics_ms):-1L,
               tx.telemetry,tx.air_attitude_valid,tx.air_pitch_cdeg,tx.air_roll_cdeg,
               tx.air_distance_valid,tx.air_distance_cm,
               tx.telemetry?(long)(uint32_t)((uint32_t)(now/1000)-tx.telemetry_ms):-1L,
               (unsigned long)tx.air_telem_frames,(unsigned long)tx.air_telem_gap_ms,(unsigned long)tx.air_telem_max_gap_ms,
               output[0], output[1], output[2], output[3], output[4], output[5], output[6], output[7], output_hex);
        xTaskDelayUntil(&publish_at, pdMS_TO_TICKS(100));
    }
}

static void command_task(void *arg)
{
    (void)arg;
    char line[64];
    for (;;) {
        if (fgets(line, sizeof(line), stdin) == NULL) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        unsigned channel, strength, duration;
        char tail;
        line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, "@T,", 3)) {
            const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
            bool accepted = false;
            uint32_t session = esp_random();
            const bool hard_stop=flight_stop_pressed();
            portENTER_CRITICAL(&s_mux);
            if(hard_stop) flight_keys_edge(&s_keys,&s_tx,1,true,now);
            const bool motion_allowed=s_led_test_pin<0 && !s_keys.down[1] && !s_keys.stop_latched && !s_keys.pending;
            if (!strcmp(line, "@T,PING")) {
                s_tx.heartbeat = now; accepted = true;
            } else if (!strcmp(line, "@T,START3")) {
                if (s_state.rc_ready && !s_keys.down[1]) accepted=tx_start_v3(&s_tx,now,session);
                if(accepted) flight_keys_host_start(&s_keys);
            } else if (!strcmp(line, "@T,ARM") && motion_allowed) {
                const bool fresh = s_state.imu && esp_timer_get_time() - s_state.attitude_us < 500000;
                accepted = tx_arm(&s_tx, now, fresh && s_gesture.calibrated && s_signal == GESTURE_FLAT);
                if(accepted && s_keys.auto_connect) flight_keys_host_start(&s_keys);
            } else if (!strcmp(line, "@T,TAKEOFF") && motion_allowed) {
                const bool fresh = s_state.imu && esp_timer_get_time() - s_state.attitude_us < 500000;
                accepted = tx_takeoff(&s_tx, now, fresh && s_gesture.calibrated && s_signal == GESTURE_FLAT);
            } else if (!strcmp(line, "@T,RESUME") && motion_allowed) {
                const bool fresh = s_state.imu && esp_timer_get_time() - s_state.attitude_us < 500000;
                accepted = tx_resume(&s_tx, now, fresh && s_gesture.calibrated && s_signal == GESTURE_FLAT);
            } else if (!strcmp(line, "@T,LAND") && motion_allowed) {
                accepted = tx_land(&s_tx,now);
            } else if (!strcmp(line, "@T,STOP")) {
                flight_keys_stop(&s_keys,&s_tx,now); accepted = true;
            }
            portEXIT_CRITICAL(&s_mux);
            if (strcmp(line, "@T,PING")) ESP_LOGI(TAG, "%s %s", line, accepted ? "accepted" : "rejected");
            continue;
        }
        if(!strncmp(line,"@L,",3)) {
            int pin=-2;
            bool parsed=sscanf(line,"@L,TEST,%d %c",&pin,&tail)==1;
            bool valid=pin==-1 || pin==12 || pin==13 || pin==14 || pin==21;
            uint32_t now=(uint32_t)(esp_timer_get_time()/1000);
            portENTER_CRITICAL(&s_mux);
            bool safe=(s_tx.phase==TX_OFF || s_tx.phase==TX_FAULT ||
                (s_tx.phase==TX_READY && s_tx.feedback && now-s_tx.feedback_ms<300 && !(s_tx.air_flags&1))) &&
                !s_keys.pending && !s_keys.launch_requested && !s_keys.down[0];
            bool accepted=parsed && valid && (pin==-1 || safe);
            if(accepted) { s_led_test_pin=pin; s_led_test_ms=now; }
            portEXIT_CRITICAL(&s_mux);
            ESP_LOGI(TAG,"LED identify GPIO%d %s (10 s, -1=off)",pin,accepted?"accepted":"rejected");
            continue;
        }
        // Zero calibration also allowed while sending fixed low-throttle lock frames.
        portENTER_CRITICAL(&s_mux);
        const bool configuring = s_tx.phase == TX_OFF || s_tx.phase == TX_FAULT;
        const bool zero_allowed = !strcmp(line, "@G,ZERO") && tx_can_zero(&s_tx) && !s_keys.pending;
        portEXIT_CRITICAL(&s_mux);
        if ((!strncmp(line, "@G,", 3) || !strncmp(line, "@O,", 3)) && !configuring && !zero_allowed) {
            ESP_LOGW(TAG, "stop TX before changing gesture/output configuration");
            continue;
        }
        if (!strcmp(line, "@G,ZERO") || !strcmp(line, "@G,RESET")) {
            const bool zero = !strcmp(line, "@G,ZERO");
            portENTER_CRITICAL(&s_mux);
            const bool fresh = s_state.imu && esp_timer_get_time() - s_state.attitude_us < 500000 &&
                               isfinite(s_state.pitch) && isfinite(s_state.roll);
            if (zero && fresh) {
                gesture_calibrate(&s_gesture, s_state.pitch, s_state.roll);
                s_signal = GESTURE_UNKNOWN; // ARM must await a new full center dwell.
            }
            else if (!zero) gesture_reset(&s_gesture);
            portEXIT_CRITICAL(&s_mux);
            ESP_LOGI("gesture", "%s", zero ? (fresh ? "flat calibrated" : "calibration rejected: IMU stale") : "calibration cleared");
            continue;
        }
        unsigned ip, ir, swap;
        if (sscanf(line, "@G,DIR,%u,%u,%u %c", &ip, &ir, &swap, &tail) == 3 &&
            ip <= 1 && ir <= 1 && swap <= 1) {
            portENTER_CRITICAL(&s_mux);
            gesture_set_direction(&s_gesture, ip, ir, swap);
            portEXIT_CRITICAL(&s_mux);
            ESP_LOGI("gesture", "direction: invert_pitch=%u invert_roll=%u swap=%u", ip, ir, swap);
            continue;
        }
        unsigned option;
        if (sscanf(line, "@O,AXIS,%u %c", &option, &tail) == 1 && option <= 1) {
            portENTER_CRITICAL(&s_mux);
            s_yaw_axis = option;
            portEXIT_CRITICAL(&s_mux);
            ESP_LOGI(TAG, "output preview lateral axis=%u", option);
            continue;
        }
        int parsed = sscanf(line, "@M,%u,%u,%u %c", &channel, &strength, &duration, &tail);
        if (parsed == 3) {
            esp_err_t err = vibration_pulse(channel, strength, duration);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "motor %u: %u%% for %u ms", channel, strength, duration);
            } else {
                ESP_LOGW(TAG, "motor command rejected (%s): %s", esp_err_to_name(err), line);
            }
        } else {
            ESP_LOGW(TAG, "motor command parse count %d: %s", parsed, line);
        }
    }
}

void telemetry_start(void)
{
    usb_serial_jtag_driver_config_t config = {
        .tx_buffer_size = 2048,
        .rx_buffer_size = 256,
    };
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&config));
    usb_serial_jtag_vfs_use_driver();
    fcntl(fileno(stdin), F_SETFL, 0);
    setvbuf(stdin, NULL, _IONBF, 0);
    ESP_ERROR_CHECK(xTaskCreate(control_task,"hand_control",3072,NULL,5,NULL)
                    == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(telemetry_task, "telemetry", 4096, NULL, 4, NULL)
                    == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(command_task, "usb_commands", 4096, NULL, 4, NULL)
                    == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_LOGI(TAG, "USB host frames @HC: and motor commands @M,channel,percent,ms");
}
