#ifndef _ADC_DMA_Config_H
#define _ADC_DMA_Config_H
#include "stm32f10x.h"

extern __IO uint16_t ADC_ConvertedValue[4];

extern void ADC1_GPIO_Config(void);
extern void ADC1_Mode_Config(void);
#endif
