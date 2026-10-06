#ifndef REMOTE_LOGIC_H
#define REMOTE_LOGIC_H

#include <stdbool.h>
#include <stdint.h>

/* 名称为分析后重新命名；不是丢失源文件中的原始名称。 */
typedef struct {
    uint16_t channels[8];
    uint8_t packet[32];
    uint16_t previous_ch0;
    uint16_t previous_ch1;
    uint16_t previous_ch3;
    int16_t trim[5];
    uint8_t key_state;
    uint8_t control_divider;
    uint32_t tick_ms;
    uint32_t last_key_tick;
} RemoteState;

typedef struct {
    int8_t status_pin;       /* -1: 不改 PA12；0: 拉低；1: 拉高。 */
    bool write_eeprom;
    uint8_t eeprom_address;
    uint16_t eeprom_value;
} KeyAction;

void rc_compute_channels(RemoteState *s, const volatile uint16_t adc[4]);
void rc_build_packet(RemoteState *s);
KeyAction rc_scan_keys(RemoteState *s, uint32_t gpioa_idr,
                      uint32_t gpiob_idr, uint32_t gpioc_idr);
bool rc_tick(RemoteState *s);

#endif
