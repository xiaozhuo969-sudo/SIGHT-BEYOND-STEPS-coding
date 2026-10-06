#include <stdint.h>

extern uint32_t _estack, _sidata, _sdata, _edata, _sbss, _ebss;
extern int main(void);
extern void SystemInit(void), SysTick_Handler(void), USART1_IRQHandler(void);
void Reset_Handler(void);
void Default_Handler(void) { for (;;) { __asm__ volatile ("nop"); } }
void NMI_Handler(void) { }
void SVC_Handler(void) { }
void DebugMon_Handler(void) { }
void PendSV_Handler(void) { }

__attribute__((used, section(".isr_vector")))
void (* const interrupt_vectors[59])(void) = {
    [0] = (void (*)(void))&_estack,
    [1] = Reset_Handler,
    [2] = NMI_Handler,
    [3] = Default_Handler,
    [4] = Default_Handler,
    [5] = Default_Handler,
    [6] = Default_Handler,
    [11] = SVC_Handler,
    [12] = DebugMon_Handler,
    [14] = PendSV_Handler,
    [15] = SysTick_Handler,
    [16 ... 52] = Default_Handler,
    [53] = USART1_IRQHandler,
    [54 ... 58] = Default_Handler,
};

void Reset_Handler(void)
{
    SystemInit();
    volatile uint32_t *dst;
    const volatile uint32_t *src = &_sidata;
    for (dst = &_sdata; dst < &_edata;) *dst++ = *src++;
    for (dst = &_sbss; dst < &_ebss;) *dst++ = 0;
    *(volatile uint32_t *)(uintptr_t)0xe000ed08u = 0x08000000u;
    (void)main();
    Default_Handler();
}
