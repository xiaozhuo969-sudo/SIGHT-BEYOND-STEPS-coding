/*******************************************************************
 *MPU6050
 *@brief 
 *@brief 
 *@time  2016.1.8
 *@editor小南&zin
 *飞控爱好QQ群551883670,邮箱759421287@qq.com
 *非授权使用人员，禁止使用。禁止传阅，违者一经发现，侵权处理。
 ******************************************************************/
#include "stm32f10x.h"
#include "misc.h"
#include "delay.h"
#include "ALL_DATA.h"
#include "hand_link.h"
static volatile uint32_t usTicks = 0;


/**
  * @brief  This function handles SysTick_Handler exception.
  * @param  None
  * @retval None
  */
void SysTick_Handler(void)
{
	SysTick_count++;
	hand_milliseconds++;
}



void cycleCounterInit(void)
{
	//滴答定时器初始化
	SysTick_Config(SystemCoreClock / 1000);	
}

/***********************************************************************
 * 获得系统当前运行的us值 
 * @param[in] 
 * @param[out] 
 * @return     
 **********************************************************************/
uint32_t GetSysTime_us(void) //获取当前系统的运行时间 单位us
{
    return (SysTick_count*1000  + (72000 - SysTick->VAL) / 72); //SysTick->VAL 当前倒计数的值//72000每个ms计数的重装值//1000 ms转化成us
}

//    毫秒级延时函数	 
uint32_t last_systick;
void delay_ms(uint16_t nms)
{
	last_systick=SysTick_count;
	while(SysTick_count - last_systick < nms);	  	  
}


void delay_us(unsigned int i)
{
    volatile uint32_t *demcr=(uint32_t *)0xE000EDFC;
    volatile uint32_t *ctrl=(uint32_t *)0xE0001000;
    volatile uint32_t *cycles=(uint32_t *)0xE0001004;
    *demcr |= (1u<<24); *ctrl |= 1;
    uint32_t start=*cycles, ticks=i*(SystemCoreClock/1000000);
    while((uint32_t)(*cycles-start)<ticks) {}
}
