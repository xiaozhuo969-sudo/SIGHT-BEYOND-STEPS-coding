#include <inttypes.h>
#include <stdbool.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mirf.h"
#include "nrf24_demo.h"
#include "remote_protocol.h"
#include "../../shared/hand_protocol.h"
#include "telemetry.h"

// New wiring: SCK=6, MOSI=7, MISO=8, CSN=16, CE=4, optional IRQ=2.
#define NRF_IRQ_GPIO GPIO_NUM_2
#define NRF_PAYLOAD_SIZE 32
#define NRF_RX_CONFIG ((1 << MASK_TX_DS) | (1 << EN_CRC) | (1 << CRCO) | \
                       (1 << PWR_UP) | (1 << PRIM_RX))

static const char *TAG = "nrf24_demo";
// Byte order here is the order clocked over SPI. Only pipe 1 is enabled.
static const uint8_t s_rx_address[5] = {0xE1, 0xE2, 0xE3, 0xE4, 0xE5};

static uint8_t read_reg(NRF24_t *radio, uint8_t reg)
{
    uint8_t value = 0;
    Nrf24_readRegister(radio, reg, &value, 1);
    return value;
}

static bool check_reg(NRF24_t *radio, uint8_t reg, uint8_t expected)
{
    const uint8_t actual = read_reg(radio, reg);
    if (actual != expected) {
        ESP_LOGW(TAG, "reg 0x%02X: expected=0x%02X read=0x%02X", reg, expected, actual);
        return false;
    }
    return true;
}

static void log_spi_diagnostic(NRF24_t *radio)
{
    Nrf24_ceLow(radio);
    ESP_LOGW(TAG, "probe registers: CONFIG=0x%02X AW=0x%02X RF_CH=0x%02X STATUS=0x%02X",
             read_reg(radio, CONFIG), read_reg(radio, SETUP_AW),
             read_reg(radio, RF_CH), read_reg(radio, STATUS));
    // Change only weak input bias. Never drive MISO against the module.
    // Following both biases while CSN is active suggests an undriven MISO line;
    // it does not identify which wire/power connection is faulty.
    ESP_ERROR_CHECK(gpio_set_pull_mode(CONFIG_MISO_GPIO, GPIO_PULLDOWN_ONLY));
    vTaskDelay(1);
    const uint8_t down = read_reg(radio, STATUS);
    ESP_ERROR_CHECK(gpio_set_pull_mode(CONFIG_MISO_GPIO, GPIO_PULLUP_ONLY));
    vTaskDelay(1);
    const uint8_t up = read_reg(radio, STATUS);
    ESP_ERROR_CHECK(gpio_set_pull_mode(CONFIG_MISO_GPIO, GPIO_FLOATING));
    ESP_LOGW(TAG, "MISO weak-bias test: STATUS pulldown=0x%02X pullup=0x%02X%s",
             down, up, down == 0 && up == 0xFF ? " (line appears undriven; check module power/CSN/MISO)" : "");
}

static bool probe_and_configure(NRF24_t *radio)
{
    // nRF24L01 has no WHO_AM_I register. Exercise several writable bits and
    // a complete address instead of accepting one plausible STATUS byte.
    Nrf24_powerDown(radio);
    vTaskDelay(pdMS_TO_TICKS(5));
    const uint8_t status = read_reg(radio, STATUS);
    if (status == 0xFF || (status & 0x80)) {
        ESP_LOGW(TAG, "invalid STATUS=0x%02X", status);
        return false;
    }
    static const uint8_t channels[] = {0x2A, 0x55};
    for (size_t i = 0; i < sizeof(channels); ++i) {
        Nrf24_configRegister(radio, RF_CH, channels[i]);
        if (!check_reg(radio, RF_CH, channels[i])) return false;
    }
    Nrf24_configRegister(radio, SETUP_AW, 3);  // Five-byte addresses.
    if (!check_reg(radio, SETUP_AW, 3)) return false;
    uint8_t patterns[][5] = {
        {0xA5, 0x3C, 0x96, 0x69, 0x5A},
        {0x5A, 0xC3, 0x69, 0x96, 0xA5},
    };
    for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); ++i) {
        uint8_t actual[5] = {0};
        Nrf24_writeRegister(radio, RX_ADDR_P1, patterns[i], sizeof(actual));
        Nrf24_readRegister(radio, RX_ADDR_P1, actual, sizeof(actual));
        if (memcmp(actual, patterns[i], sizeof(actual)) != 0) {
            ESP_LOGW(TAG, "address read/write test failed (pattern %u)", (unsigned)i);
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, actual, sizeof(actual), ESP_LOG_WARN);
            return false;
        }
    }

    // Explicitly replace retained settings after an MCU-only reset.
    Nrf24_configRegister(radio, EN_AA, 0);      // Aircraft disables auto ACK.
    Nrf24_configRegister(radio, EN_RXADDR, 2);  // Pipe 1 only.
    Nrf24_configRegister(radio, SETUP_RETR, 0);
    Nrf24_configRegister(radio, RF_SETUP, 0x07); // 1 Mbps, 0 dBm, LNA enabled.
    Nrf24_configRegister(radio, DYNPD, 0);
    Nrf24_configRegister(radio, FEATURE, 0);
    uint8_t address[5];
    memcpy(address, s_rx_address, sizeof(address));
    Nrf24_writeRegister(radio, RX_ADDR_P1, address, sizeof(address));
    Nrf24_writeRegister(radio, TX_ADDR, address, sizeof(address));
    Nrf24_configRegister(radio, RF_CH, CONFIG_RADIO_CHANNEL);
    Nrf24_configRegister(radio, RX_PW_P1, NRF_PAYLOAD_SIZE);
    radio->channel = CONFIG_RADIO_CHANNEL;
    radio->payload = NRF_PAYLOAD_SIZE;
    Nrf24_flushRx(radio);
    spi_csnLow(radio);
    spi_transfer(radio, FLUSH_TX);
    spi_csnHi(radio);
    Nrf24_configRegister(radio, STATUS, (1 << RX_DR) | (1 << TX_DS) | (1 << MAX_RT));
    Nrf24_configRegister(radio, CONFIG, NRF_RX_CONFIG);
    // Wait for the oscillator before asserting CE; polling does not need IRQ.
    vTaskDelay(pdMS_TO_TICKS(10));
    uint8_t actual_address[5] = {0};
    Nrf24_readRegister(radio, RX_ADDR_P1, actual_address, sizeof(actual_address));
    if (!check_reg(radio, CONFIG, NRF_RX_CONFIG) ||
        !check_reg(radio, RF_CH, CONFIG_RADIO_CHANNEL) ||
        !check_reg(radio, RX_PW_P1, NRF_PAYLOAD_SIZE) ||
        !check_reg(radio, EN_AA, 0) || !check_reg(radio, EN_RXADDR, 2) ||
        !check_reg(radio, RF_SETUP, 0x07) ||
        !check_reg(radio, DYNPD, 0) || !check_reg(radio, FEATURE, 0) ||
        !check_reg(radio, FIFO_STATUS, 0x11) ||
        memcmp(actual_address, s_rx_address, sizeof(actual_address)) != 0) {
        return false;
    }
    Nrf24_readRegister(radio, TX_ADDR, actual_address, sizeof(actual_address));
    if (memcmp(actual_address, s_rx_address, sizeof(actual_address)) != 0) return false;
    radio->PTX = 0;
    Nrf24_ceHi(radio);
    ESP_LOGI(TAG, "SPI self-test PASS; NRF24 ready, RX default (TX requires START): channel=%d, 1Mbps, CRC16, payload=32, address=E1:E2:E3:E4:E5, auto-ack=off",
             CONFIG_RADIO_CHANNEL);
    return true;
}

static void nrf24_task(void *arg)
{
    NRF24_t radio = {0};
    const gpio_config_t irq_config = {
        .pin_bit_mask = 1ULL << NRF_IRQ_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&irq_config));
    Nrf24_init(&radio);
    ESP_ERROR_CHECK(gpio_set_pull_mode(CONFIG_MISO_GPIO, GPIO_FLOATING));
    ESP_LOGI(TAG, "SPI mode0 %dHz: SCK=%d MOSI=%d MISO=%d CSN=%d CE=%d IRQ=2 (optional)",
             CONFIG_NRF24_SPI_CLOCK_HZ, CONFIG_SCLK_GPIO, CONFIG_MOSI_GPIO,
             CONFIG_MISO_GPIO, CONFIG_CSN_GPIO, CONFIG_CE_GPIO);
    vTaskDelay(pdMS_TO_TICKS(100));
    uint32_t received = 0, rejected = 0;
    uint16_t latest[8] = {0};
    for (;;) {
        telemetry_set_radio_ready(false);
        if (!probe_and_configure(&radio)) {
            Nrf24_powerDown(&radio);
            log_spi_diagnostic(&radio);
            ESP_LOGE(TAG, "NRF24 probe failed; check 3.3V/GND/SCK/MOSI/MISO/CSN wiring; retry in 5s");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        telemetry_set_radio_ready(true);
        TickType_t last_log = xTaskGetTickCount();
        bool pending = false;
        int64_t last_tx = 0;
        int64_t tx_started = 0;
        for (;;) {
            uint8_t status = read_reg(&radio, STATUS);
            uint8_t fifo = read_reg(&radio, FIFO_STATUS);
            const uint8_t config = read_reg(&radio, CONFIG);
            const uint8_t channel = read_reg(&radio, RF_CH);
            const uint8_t expected_config = pending ? (NRF_RX_CONFIG & ~(1 << PRIM_RX)) : NRF_RX_CONFIG;
            if ((status & 0x80) || (fifo & 0x8C) ||
                config != expected_config || channel != CONFIG_RADIO_CHANNEL) {
                telemetry_set_radio_ready(false);
                Nrf24_ceLow(&radio);
                /* Preserve the first read and a confirmation before reconfiguration.
                 * RF range cannot explain a local CONFIG/RF_CH mismatch by itself. */
                const uint8_t again_status = read_reg(&radio, STATUS);
                const uint8_t again_fifo = read_reg(&radio, FIFO_STATUS);
                const uint8_t again_config = read_reg(&radio, CONFIG);
                const uint8_t again_channel = read_reg(&radio, RF_CH);
                ESP_LOGE(TAG, "NRF24 local register fault: STATUS=0x%02X FIFO=0x%02X CONFIG=0x%02X expected=0x%02X RF_CH=%u expected=%u pending=%u PTX=%u; repeat=%02X/%02X/%02X/%02X; re-probing in 5s",
                         status, fifo, config, expected_config, channel, CONFIG_RADIO_CHANNEL,
                         pending, radio.PTX, again_status, again_fifo, again_config, again_channel);
                vTaskDelay(pdMS_TO_TICKS(5000));
                break;
            }
            const int64_t now = esp_timer_get_time();
            if (pending) {
                if (status & (1 << TX_DS)) {
                    Nrf24_ceLow(&radio);
                    Nrf24_powerUpRx(&radio);
                    telemetry_tx_rx_time((uint32_t)(esp_timer_get_time()-tx_started));
                    telemetry_tx_result(true);
                    pending = false;
                } else if ((status & (1 << MAX_RT)) || now - last_tx > 40000) {
                    Nrf24_ceLow(&radio);
                    radio.PTX = 0;
                    telemetry_tx_result(false);
                    ESP_LOGE(TAG, "local TX failed/timed out: STATUS=0x%02X CONFIG=0x%02X RF_CH=%u elapsed_us=%" PRId64 "; explicit restart required",
                             status, config, channel, now-tx_started);
                    break;
                }
            }
            if (pending) {
                vTaskDelay(1);
                continue;
            }
            // Refresh after TX -> RX; the snapshot above can predate a reply.
            fifo = read_reg(&radio, FIFO_STATUS);
            // Drain the FIFO, not just RX_DR: the driver clears RX_DR per read.
            // At most three packets per pass, so a noisy sender cannot starve other tasks.
            // Apply queued feedback before telemetry_tx_frame checks its timeout.
            for (unsigned i = 0; i < 3 && !(fifo & (1 << RX_EMPTY)); ++i) {
                uint8_t payload[NRF_PAYLOAD_SIZE] = {0};
                Nrf24_getData(&radio, payload);
                if (payload[0]=='H'&&payload[1]=='C') {
                    telemetry_air_packet(payload);
                } else if (remote_decode(payload, sizeof(payload), latest)) {
                    ++received;
                    telemetry_radio_packet(latest);
                } else {
                    ++rejected;
                    telemetry_radio_packet(NULL);
                }
                fifo = read_reg(&radio, FIFO_STATUS);
            }
            const int64_t send_now = esp_timer_get_time();
            if (send_now - last_tx >= telemetry_tx_interval_ms()*1000) {
                uint8_t packet[32];
                if (telemetry_tx_frame(packet)) {
                    // Prior TX has completed or faulted: prevent MiRF's unbounded PTX wait.
                    radio.PTX = 0;
                    tx_started=esp_timer_get_time();
                    Nrf24_send(&radio, packet);
                    last_tx = send_now;
                    pending = true;
                    /* Normal no-ACK TX takes well under 1 ms. Return to RX
                     * without waiting for a 10 ms RTOS tick; a stuck radio
                     * still falls back to the existing bounded timeout. */
                    const int64_t fast_deadline=esp_timer_get_time()+2000;
                    while(esp_timer_get_time()<fast_deadline) {
                        const uint8_t sent_status=read_reg(&radio,STATUS);
                        if(sent_status&(1<<TX_DS)) {
                            Nrf24_ceLow(&radio);Nrf24_powerUpRx(&radio);
                            telemetry_tx_rx_time((uint32_t)(esp_timer_get_time()-tx_started));
                            telemetry_tx_result(true);pending=false;break;
                        }
                        if(sent_status&(1<<MAX_RT)) break;
                        esp_rom_delay_us(50);
                    }
                }
            }
            if (pending) {
                vTaskDelay(1);
                continue;
            }
            if (xTaskGetTickCount() - last_log >= pdMS_TO_TICKS(5000)) {
                last_log = xTaskGetTickCount();
                ESP_LOGI(TAG, "alive: STATUS=0x%02X CONFIG=0x%02X FIFO=0x%02X IRQ=%d RX=%" PRIu32,
                         status, read_reg(&radio, CONFIG), fifo,
                         gpio_get_level(NRF_IRQ_GPIO), received);
                ESP_LOGI(TAG, "STM32 RX valid=%" PRIu32 " bad=%" PRIu32
                         " roll=%u pitch=%u thr=%u yaw=%u AUX=%u/%u/%u/%u (last accepted)",
                         received, rejected, latest[0], latest[1], latest[2], latest[3],
                         latest[4], latest[5], latest[6], latest[7]);
            }
            vTaskDelay(pdMS_TO_TICKS(5) > 0 ? pdMS_TO_TICKS(5) : 1);
        }
    }
}

void nrf24_demo_start(void)
{
    if (xTaskCreate(nrf24_task, "nrf24_demo", 4096, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to start NRF24 task");
    }
}
