#ifndef _SYS_H___
#define _SYS_H___

#include "stm32f10x.h"

#define BEEP_H GPIOA->BSRR = GPIO_Pin_12//·äÃùÆ÷Ïì
#define BEEP_L GPIOA->BRR = GPIO_Pin_12//·äÃùÆ÷²»Ïì

extern uint32_t SysTick_count;

extern void _g_Init_sys(void);

#endif
