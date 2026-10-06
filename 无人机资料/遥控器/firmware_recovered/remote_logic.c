/*
 * 人工整理的等效 C 逻辑，依据 STM32F103C8T6_backup.bin。
 * 原始函数地址写在各函数前，便于和反编译结果对照。
 * 原程序的边界行为及数据包额外字节被有意保留。
 */
#include "remote_logic.h"

static uint16_t limit_channel(uint16_t value)
{
    if (value > 2000u) return 2000u;
    if (value < 1000u) return 1000u;
    return value;
}

static uint16_t smooth_channel(uint16_t previous, uint16_t current)
{
    return (uint16_t)(uint32_t)(0.75f * (float)previous + 0.25f * (float)current);
}

/* 0x080015C0: ADC 顺序 PA0、PA1、PA2、PA3，通道名称的飞行轴意义待确认。 */
void rc_compute_channels(RemoteState *s, const volatile uint16_t adc[4])
{
    s->channels[2] = (uint16_t)(1000u + (uint32_t)(0.2f * (float)adc[1]) + s->trim[3]);
    s->channels[1] = (uint16_t)(1000u + (uint32_t)(0.25f * (float)adc[2]) + s->trim[2]);
    s->channels[1] = smooth_channel(s->previous_ch1, s->channels[1]);
    s->previous_ch1 = s->channels[1];

    s->channels[3] = (uint16_t)(1000u + (uint32_t)(0.25f * (float)adc[0]) + s->trim[4]);
    s->channels[3] = smooth_channel(s->previous_ch3, s->channels[3]);
    s->previous_ch3 = s->channels[3];

    s->channels[0] = (uint16_t)(1000u + (uint32_t)(0.25f * (float)adc[3]) + s->trim[1]);
    s->channels[0] = smooth_channel(s->previous_ch0, s->channels[0]);
    s->previous_ch0 = s->channels[0];

    /* 原程序先保存滤波历史，之后才限幅；这里保持这个顺序。 */
    s->channels[2] = limit_channel(s->channels[2]);
    s->channels[1] = limit_channel(s->channels[1]);
    s->channels[0] = limit_channel(s->channels[0]);
    s->channels[3] = limit_channel(s->channels[3]);
}

static uint8_t original_channel_area_byte(const RemoteState *s, unsigned offset)
{
    if (offset < 16u) {
        return (uint8_t)(s->channels[offset / 2u] >> (8u * (offset % 2u)));
    }
    /* 原 RAM: channels @0x20000028，packet @0x20000038，紧邻。
     * 超过 8 个通道后实际读到 packet[0..5]，不能简单改为补零。
     * 使用显式映射还原该行为，避免新的 C 数组越界。 */
    return s->packet[offset - 16u];
}

/* 0x08001188: 32 字节，AA AF 03 1B，前 8 通道按高字节在前编码。 */
void rc_build_packet(RemoteState *s)
{
    for (unsigned dst = 4; dst < 26; dst += 2) {
        s->packet[dst] = original_channel_area_byte(s, dst - 3);
        s->packet[dst + 1] = original_channel_area_byte(s, dst - 4);
    }
    /* 原函数循环中还有一次累加，但随后清零，因此不影响最终数据包。 */
    uint8_t checksum = 0;
    for (unsigned i = 0; i < 31; ++i) checksum = (uint8_t)(checksum + s->packet[i]);
    s->packet[31] = checksum;
}

static bool all_keys_released(uint32_t a, uint32_t b, uint32_t c)
{
    return (a & 0x80c0u) == 0x80c0u &&
           (b & 0x0803u) == 0x0803u && (c & 0xc000u) == 0xc000u;
}

/* 0x080019F8: 三态按键检测，一次按下只触发一次，低电平有效。 */
KeyAction rc_scan_keys(RemoteState *s, uint32_t a, uint32_t b, uint32_t c)
{
    KeyAction action = {.status_pin = -1};
    if (s->key_state == 0) {
        if ((uint32_t)(s->tick_ms - s->last_key_tick) > 30u) {
            if (all_keys_released(a, b, c)) s->key_state = 1;
            action.status_pin = 0;
        }
    } else if (s->key_state == 1) {
        if (!all_keys_released(a, b, c)) s->key_state = 2;
    } else if (s->key_state == 2) {
        if ((a & 0x8000u) == 0) {
            s->channels[4] ^= 0x0438u; /* 1000 <-> 2000 */
        } else if ((b & 0x0800u) == 0) {
            s->channels[5] ^= 0x0438u;
        } else if ((c & 0x4000u) == 0) {
            s->channels[6] ^= 0x0438u;
        } else if ((c & 0x8000u) == 0) {
            s->channels[7] ^= 0x0438u;
        } else if ((a & 0x0040u) == 0) {
            if (s->trim[1] > -250) {
                s->trim[1] -= 5;
                action.write_eeprom = true;
                action.eeprom_address = 0;
                action.eeprom_value = (uint16_t)s->trim[1];
            }
        } else if ((a & 0x0080u) == 0) {
            if (s->trim[1] < 250) {
                s->trim[1] += 5;
                action.write_eeprom = true;
                action.eeprom_address = 0;
                action.eeprom_value = (uint16_t)s->trim[1];
            }
        } else if ((b & 0x0001u) == 0) {
            if (s->trim[2] > -250) {
                s->trim[2] -= 5;
                action.write_eeprom = true;
                action.eeprom_address = 4;
                action.eeprom_value = (uint16_t)s->trim[2];
            }
        } else if ((b & 0x0002u) == 0 && s->trim[2] < 250) {
            s->trim[2] += 5;
            action.write_eeprom = true;
            action.eeprom_address = 4;
            action.eeprom_value = (uint16_t)s->trim[2];
        }
        s->key_state = 0;
        action.status_pin = 1;
        s->last_key_tick = s->tick_ms;
    }
    return action;
}

/* 0x080018CC: 原表达式是 divider++ >= 10，实际每 11 次中断更新一次。 */
bool rc_tick(RemoteState *s)
{
    ++s->tick_ms;
    uint8_t old_divider = s->control_divider++;
    if (old_divider >= 10u) {
        s->control_divider = 0;
        return true;
    }
    return false;
}
