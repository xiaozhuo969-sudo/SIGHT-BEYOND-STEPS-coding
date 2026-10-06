/*
 * STM32F103C8T6 遥控器固件的人工重建。
 * 依据 BIN 反汇编；库函数被整理为直接寄存器访问，未恢复原工程/原符号名。
 * 核心逻辑已与机器码仿真对照。软件 I2C/SPI 的电气时序和完整启动尚未上板验证。
 */
#include "remote_logic.h"
#include <stddef.h>

#define REG32(address) (*(volatile uint32_t *)(uintptr_t)(address))
#define REG8(address)  (*(volatile uint8_t *)(uintptr_t)(address))
#define BIT(n) (1u << (n))

typedef struct {
    volatile uint32_t CRL, CRH, IDR, ODR, BSRR, BRR, LCKR;
} GPIO;
#define GPIOA ((GPIO *)(uintptr_t)0x40010800u)
#define GPIOB ((GPIO *)(uintptr_t)0x40010c00u)
#define GPIOC ((GPIO *)(uintptr_t)0x40011000u)
#define RCC_CR       REG32(0x40021000u)
#define RCC_CFGR     REG32(0x40021004u)
#define RCC_CIR      REG32(0x40021008u)
#define RCC_AHBENR   REG32(0x40021014u)
#define RCC_APB2ENR  REG32(0x40021018u)
#define RCC_APB1ENR  REG32(0x4002101cu)
#define FLASH_ACR    REG32(0x40022000u)
#define AFIO_MAPR    REG32(0x40010004u)
#define DMA1_IFCR    REG32(0x40020004u)
#define DMA1_CCR1    REG32(0x40020008u)
#define DMA1_CNDTR1  REG32(0x4002000cu)
#define DMA1_CPAR1   REG32(0x40020010u)
#define DMA1_CMAR1   REG32(0x40020014u)
#define ADC1_CR1     REG32(0x40012404u)
#define ADC1_CR2     REG32(0x40012408u)
#define ADC1_SMPR2   REG32(0x40012410u)
#define ADC1_SQR1    REG32(0x4001242cu)
#define ADC1_SQR2    REG32(0x40012430u)
#define ADC1_SQR3    REG32(0x40012434u)
#define SYSTICK_CTRL REG32(0xe000e010u)
#define SYSTICK_LOAD REG32(0xe000e014u)
#define SYSTICK_VAL  REG32(0xe000e018u)

static RemoteState remote = {
    .channels = {1000,1000,1000,1000,1000,1000,1000,1000},
    .packet = {0xaa,0xaf,0x03,0x1b}
};
static volatile uint16_t adc_samples[4];
static uint32_t cycles_per_us = 48u;

/* 0x08000710: mode/speed 编码与旧版 STM32 标准外设库一致。 */
static void gpio_config(GPIO *port, uint16_t pins, uint8_t mode, uint8_t speed)
{
    uint32_t config = mode & 15u;
    if (mode & 0x10u) config |= speed;
    for (unsigned pin = 0; pin < 16; ++pin) {
        if (!(pins & BIT(pin))) continue;
        volatile uint32_t *cr = pin < 8 ? &port->CRL : &port->CRH;
        unsigned shift = (pin & 7u) * 4u;
        *cr = (*cr & ~(15u << shift)) | (config << shift);
        if (mode == 0x28u) port->BRR = BIT(pin);  /* input pull-down */
        if (mode == 0x48u) port->BSRR = BIT(pin); /* input pull-up */
    }
}

void SystemInit(void) { RCC_CR |= BIT(0); } /* 原函数 0x080018FC */

/* 0x08001344: 等待外部晶振，但 PLL 实际选 HSI/2 x12，SYSCLK=48MHz。 */
static void clock_init(void)
{
    RCC_CR |= BIT(0);
    RCC_CFGR &= 0xf8ff0000u;
    RCC_CR &= 0xfef6ffffu;
    RCC_CR &= ~BIT(18);
    RCC_CFGR &= 0xff80ffffu;
    RCC_CIR = 0x009f0000u;
    RCC_CR |= BIT(16);
    while (!(RCC_CR & BIT(17))) { }
    FLASH_ACR |= BIT(4);
    FLASH_ACR = (FLASH_ACR & 0x38u) | 2u;
    RCC_CFGR &= ~0xf0u;
    RCC_CFGR = (RCC_CFGR & ~0x700u) | 0x400u; /* APB1 /2 */
    RCC_CFGR &= ~0x3800u;                     /* APB2 /1 */
    RCC_CFGR = (RCC_CFGR & ~0x3f0000u) | 0x280000u;
    RCC_CR |= BIT(24);
    while (!(RCC_CR & BIT(25))) { }
    RCC_CFGR = (RCC_CFGR & ~3u) | 2u;
    while ((RCC_CFGR & 12u) != 8u) { }
    RCC_APB2ENR |= 0x521du;
    RCC_APB1ENR |= 0x00240002u;
}

/* 0x0800082C: 保留原来的无符号运算与 SysTick 周期计算。 */
static uint32_t micros(void)
{
    const volatile uint32_t *ticks = &remote.tick_ms;
    uint32_t before, after, counter;
    do {
        before = *ticks;
        counter = SYSTICK_VAL;
        after = *ticks;
    } while (before != after);
    return before * 1000u + (cycles_per_us * 1000u - counter) / cycles_per_us;
}

/* 原函数 0x080019D4；在 SysTick 内调用的等待问题见 README。 */
static void delay_ms(uint32_t duration)
{
    uint32_t start = micros();
    while ((uint32_t)(micros() - start) < duration * 1000u) { }
}

static void status_gpio_init(void)
{
    RCC_APB2ENR |= BIT(2);
    gpio_config(GPIOA, BIT(12), 0x10u, 3u);
    GPIOA->BSRR = BIT(12);
}

/* 0x0800023C/0x0800026C: ADC1 扫描 PA0..3，DMA1 通道1循环传送半字。 */
static void adc_dma_init(void)
{
    RCC_AHBENR |= BIT(0);
    RCC_APB2ENR |= 0x204u;
    gpio_config(GPIOA, 0x000fu, 0x00u, 0u);
    DMA1_CCR1 &= 0xfffeu;
    DMA1_CCR1 = 0;
    DMA1_CNDTR1 = 0;
    DMA1_CPAR1 = 0;
    DMA1_CMAR1 = 0;
    DMA1_IFCR |= 15u;
    DMA1_CCR1 = (DMA1_CCR1 & ~0x7ff0u) | 0x25a0u;
    DMA1_CNDTR1 = 4;
    DMA1_CPAR1 = 0x4001244cu;
    DMA1_CMAR1 = (uint32_t)(uintptr_t)adc_samples;
    DMA1_CCR1 |= BIT(0);
    ADC1_CR1 = (ADC1_CR1 & 0xfff0feffu) | BIT(8);
    ADC1_CR2 = (ADC1_CR2 & 0xfff1f7fdu) | 0x000e0002u;
    ADC1_SQR1 = (ADC1_SQR1 & ~0x00f00000u) | 0x00300000u;
    RCC_CFGR = (RCC_CFGR & ~0xc000u) | 0xc000u; /* ADC /8 = 6 MHz */
    for (unsigned channel = 0; channel < 4; ++channel) {
        unsigned sample_shift = channel * 3u;
        unsigned rank_shift = channel * 5u;
        ADC1_SMPR2 = (ADC1_SMPR2 & ~(7u << sample_shift)) | (5u << sample_shift);
        ADC1_SQR3 = (ADC1_SQR3 & ~(31u << rank_shift)) | (channel << rank_shift);
    }
    ADC1_CR2 |= BIT(8);
    ADC1_CR2 |= BIT(0);
    ADC1_CR2 |= BIT(3);
    while (ADC1_CR2 & BIT(3)) { }
    ADC1_CR2 |= BIT(2);
    while (ADC1_CR2 & BIT(2)) { }
    ADC1_CR2 |= 0x00500000u;
}

/* PB6=SCL，PB7=SDA，开漏。delay 的循环量来自原机器码，时序仍需实测。 */
static void i2c_delay(void)
{
    volatile uint8_t count = 5;
    while (count) --count;
}
static void scl_high(void) { GPIOB->BSRR = BIT(6); }
static void scl_low(void)  { GPIOB->BRR = BIT(6); }
static void sda_high(void) { GPIOB->BSRR = BIT(7); }
static void sda_low(void)  { GPIOB->BRR = BIT(7); }

static uint8_t i2c_start(void)
{
    sda_high(); scl_high(); i2c_delay();
    /* 0x08000BB0 原先判断 (!GPIOB_IDR) & 0x80，恒为0；不添加修正逻辑。 */
    sda_low(); i2c_delay();
    if (GPIOB->IDR & BIT(7)) return 1;
    scl_low(); i2c_delay();
    return 0;
}
static void i2c_stop(void)
{
    scl_low(); i2c_delay();
    sda_low(); i2c_delay(); i2c_delay();
    scl_high(); i2c_delay();
    sda_high(); i2c_delay();
}
static uint8_t i2c_wait_ack(void)
{
    scl_low(); i2c_delay();
    sda_high(); i2c_delay();
    scl_high(); i2c_delay(); i2c_delay(); i2c_delay();
    uint8_t nack = (GPIOB->IDR & BIT(7)) ? 1u : 0u;
    scl_low();
    return nack;
}
static void i2c_reply(bool nack)
{
    scl_low(); i2c_delay();
    if (nack) sda_high(); else sda_low();
    i2c_delay(); scl_high();
    i2c_delay(); i2c_delay(); i2c_delay(); i2c_delay();
    scl_low(); i2c_delay();
}
static void i2c_write_byte(uint8_t value)
{
    for (unsigned i = 0; i < 8; ++i) {
        scl_low(); i2c_delay();
        if (value & 0x80u) sda_high(); else sda_low();
        value <<= 1;
        i2c_delay(); scl_high();
        i2c_delay(); i2c_delay(); i2c_delay();
    }
    scl_low();
}
static uint8_t i2c_read_byte(void)
{
    uint8_t value = 0;
    sda_high();
    for (unsigned i = 0; i < 8; ++i) {
        value <<= 1;
        scl_low(); i2c_delay(); i2c_delay();
        scl_high(); i2c_delay(); i2c_delay(); i2c_delay();
        if (GPIOB->IDR & BIT(7)) value |= 1u;
    }
    scl_low();
    return value;
}
static uint8_t i2c_write_buffer(uint8_t address, uint8_t reg,
                                const uint8_t *bytes, unsigned count)
{
    if (i2c_start()) return 1;
    i2c_write_byte(address);
    if (i2c_wait_ack()) { i2c_stop(); return 1; }
    i2c_write_byte(reg); (void)i2c_wait_ack();
    for (unsigned i = 0; i < count; ++i) {
        i2c_write_byte(bytes[i]);
        if (i2c_wait_ack()) { i2c_stop(); return 1; }
    }
    i2c_stop(); return 0;
}
static uint8_t i2c_read_buffer(uint8_t address, uint8_t reg,
                               uint8_t *bytes, unsigned count)
{
    if (i2c_start()) return 1;
    i2c_write_byte(address);
    if (i2c_wait_ack()) { i2c_stop(); return 1; }
    i2c_write_byte(reg); (void)i2c_wait_ack(); i2c_stop();
    (void)i2c_start();
    i2c_write_byte((uint8_t)(address + 1));
    if (i2c_wait_ack()) { i2c_stop(); return 1; }
    for (unsigned i = 0; i < count; ++i) {
        bytes[i] = i2c_read_byte();
        i2c_reply(i + 1 == count);
    }
    i2c_stop(); return 0;
}
static void eeprom_write_u16(uint8_t address, uint16_t value)
{
    const uint8_t bytes[2] = {(uint8_t)value, (uint8_t)(value >> 8)};
    delay_ms(10);
    (void)i2c_write_buffer(0xa0u, address, bytes, 2);
}
static uint16_t eeprom_read_u16(uint8_t address)
{
    /* 原函数忽略读取失败，失败时返回栈中残值；重建时明确返回0。 */
    uint8_t bytes[2] = {0, 0};
    (void)i2c_read_buffer(0xa0u, address, bytes, 2);
    return (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8));
}
static bool eeprom_check(void)
{
    /* 0x08001CDA 使用单字节写函数：仅检查设备地址ACK，其余ACK被忽略。 */
    if (!i2c_start()) {
        i2c_write_byte(0xa0u);
        if (!i2c_wait_ack()) {
            i2c_write_byte(0x55u); (void)i2c_wait_ack();
            i2c_write_byte(0xaau); (void)i2c_wait_ack();
        }
        i2c_stop();
    }
    delay_ms(1000);
    uint8_t value = 1;
    (void)i2c_read_buffer(0xa0u, 0x55u, &value, 1);
    return value == 0xaau;
}

/* 软件 SPI: PB13 SCK，PB15 MOSI，PB14 MISO；PB12 CSN。 */
static uint8_t spi_transfer(uint8_t value)
{
    uint8_t result = 0;
    for (unsigned bit = 0; bit < 8; ++bit) {
        GPIOB->BRR = BIT(13);
        if (value & 0x80u) GPIOB->BSRR = BIT(15); else GPIOB->BRR = BIT(15);
        value <<= 1;
        result <<= 1;
        GPIOB->BSRR = BIT(13);
        if (GPIOB->IDR & BIT(14)) ++result;
        GPIOB->BRR = BIT(13);
    }
    return result;
}
static uint8_t nrf_write_register(uint8_t command, uint8_t value)
{
    GPIOB->BRR = BIT(12);
    uint8_t status = spi_transfer(command);
    (void)spi_transfer(value);
    GPIOB->BSRR = BIT(12);
    return status;
}
static uint8_t nrf_read_register(uint8_t command)
{
    GPIOB->BRR = BIT(12);
    (void)spi_transfer(command);
    uint8_t result = spi_transfer(0xffu);
    GPIOB->BSRR = BIT(12);
    return result;
}
static void nrf_write_buffer(uint8_t command, const uint8_t *bytes, unsigned count)
{
    GPIOB->BRR = BIT(12);
    (void)spi_transfer(command);
    for (unsigned i = 0; i < count; ++i) (void)spi_transfer(bytes[i]);
    GPIOB->BSRR = BIT(12);
}
static void nrf_read_buffer(uint8_t command, uint8_t *bytes, unsigned count)
{
    GPIOB->BRR = BIT(12);
    (void)spi_transfer(command);
    for (unsigned i = 0; i < count; ++i) bytes[i] = spi_transfer(0xffu);
    GPIOB->BSRR = BIT(12);
}
static bool nrf_check(void)
{
    uint8_t probe[5] = {0xa5,0xa5,0xa5,0xa5,0xa5};
    nrf_write_buffer(0x30u, probe, 5);
    nrf_read_buffer(0x10u, probe, 5);
    for (unsigned i = 0; i < 5; ++i) if (probe[i] != 0xa5u) return false;
    return true;
}
static void nrf_init(void)
{
    RCC_APB2ENR |= BIT(2) | BIT(3);
    gpio_config(GPIOA, BIT(11), 0x10u, 3u); /* CE */
    gpio_config(GPIOB, BIT(12), 0x10u, 3u); /* CSN */
    GPIOA->BSRR = BIT(11); GPIOB->BSRR = BIT(12);
    gpio_config(GPIOA, BIT(8), 0x48u, 3u);  /* IRQ input pull-up */
    GPIOA->BSRR = BIT(8);
    GPIOB->BSRR = BIT(12);
    gpio_config(GPIOB, BIT(13) | BIT(15), 0x10u, 3u);
    gpio_config(GPIOB, BIT(14), 0x04u, 3u);
    GPIOB->BRR = BIT(13); GPIOB->BSRR = BIT(15);
    GPIOA->BRR = BIT(11); GPIOB->BSRR = BIT(12);
    while (!nrf_check()) { }
    static const uint8_t address[5] = {0xe1,0xe2,0xe3,0xe4,0xe5};
    GPIOA->BRR = BIT(11);
    nrf_write_buffer(0x30u, address, 5); /* TX_ADDR */
    nrf_write_buffer(0x2au, address, 5); /* RX_ADDR_P0 */
    (void)nrf_write_register(0x21u, 0x00u); /* EN_AA=0 */
    (void)nrf_write_register(0x22u, 0x01u); /* EN_RXADDR pipe0 */
    (void)nrf_write_register(0x24u, 0x1au); /* SETUP_RETR */
    (void)nrf_write_register(0x25u, 0x01u); /* RF_CH=1 */
    (void)nrf_write_register(0x26u, 0x07u); /* 1Mbps, 0dBm */
    (void)nrf_write_register(0x20u, 0x0eu); /* CRC enabled, 2-byte CRC, PTX powered */
    GPIOA->BSRR = BIT(11);
}
static uint8_t nrf_transmit_32(const uint8_t bytes[32])
{
    GPIOA->BRR = BIT(11);
    nrf_write_buffer(0xa0u, bytes, 32);
    GPIOA->BSRR = BIT(11);
    while (GPIOA->IDR & BIT(8)) { } /* 原程序没有超时 */
    uint8_t status = nrf_read_register(0x07u);
    (void)nrf_write_register(0x27u, status);
    if (status & 0x10u) {
        (void)nrf_write_register(0xe1u, 0xffu);
        return 0x10u;
    }
    if (status & 0x20u) return 0x20u;
    return 0xffu;
}

static void keys_gpio_init(void)
{
    AFIO_MAPR = 0x02000000u; /* 关闭JTAG，保留SWD，让PA15可作按键。 */
    RCC_APB2ENR |= 0x1cu;
    gpio_config(GPIOA, 0x80d0u, 0x48u, 3u);
    gpio_config(GPIOB, 0x0803u, 0x48u, 3u);
    gpio_config(GPIOC, 0xc000u, 0x48u, 3u);
}
static void board_init(void)
{
    gpio_config(GPIOB, BIT(6), 0x10u, 3u);
    gpio_config(GPIOB, BIT(6) | BIT(7), 0x14u, 3u);
    nrf_init();
    while (!eeprom_check()) { }
    adc_dma_init();
    remote.trim[1] = (int16_t)eeprom_read_u16(0);
    remote.trim[2] = (int16_t)eeprom_read_u16(4);
    keys_gpio_init();
}

void SysTick_Handler(void)
{
    if (!rc_tick(&remote)) return;
    rc_compute_channels(&remote, adc_samples);
    KeyAction action = rc_scan_keys(&remote, GPIOA->IDR, GPIOB->IDR, GPIOC->IDR);
    if (action.write_eeprom) eeprom_write_u16(action.eeprom_address, action.eeprom_value);
    if (action.status_pin == 1) GPIOA->BSRR = BIT(12);
    if (action.status_pin == 0) GPIOA->BRR = BIT(12);
    rc_build_packet(&remote);
    (void)nrf_transmit_32(remote.packet);
}

void USART1_IRQHandler(void)
{
    /* 0x08001978 原函数只读两次SR，没有收发协议解析。 */
    (void)*(volatile uint16_t *)(uintptr_t)0x40013800u;
    (void)*(volatile uint16_t *)(uintptr_t)0x40013800u;
}

int main(void)
{
    clock_init();
    cycles_per_us = 48u;
    status_gpio_init();
    for (volatile int32_t delay = 2000000; delay >= 0; --delay) { }
    GPIOA->BSRR = BIT(12);
    SYSTICK_LOAD = 47999u;
    REG8(0xe000ed23u) = 0xf0u;
    SYSTICK_VAL = 0;
    SYSTICK_CTRL = 7u;
    /* 保留原main顺序：先开SysTick，再初始化外设；启动阶段也可能发送数据。 */
    board_init();
    REG32(0xe000ed0cu) = 0x05fa0700u;
    REG8(0xe000e400u + 37u) = 0x00u;
    REG32(0xe000e104u) = BIT(5); /* USART1 IRQ 37 */
    REG8(0xe000e400u + 29u) = 0x30u;
    REG32(0xe000e100u) = BIT(29); /* TIM3 IRQ29，原程序也启用但未配置定时器 */
    for (;;) { __asm__ volatile ("nop"); }
}
