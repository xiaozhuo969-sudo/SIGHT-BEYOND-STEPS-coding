/*******************************************************************
 *SYS 系统控制文件
 *@brief 
 *@brief 
 *@time  2016.1.8
 *@editor小南&zin
 *飞控爱好QQ群551883670,邮箱759421287@qq.com
 *非授权使用人员，禁止使用。禁止传阅，违者一经发现，侵权处理。
 ******************************************************************/
#include "sys.h"
#include "ADC_DMA_Config.h"
#include "SPI.h"
#include "nrf24l01.h"
#include "TIM.h"
#include "eeprom.h"
#include "remote.h"
#include <string.h>
	#include "I2C.h"
uint32_t SysTick_count = 0;


void _g_Init_sys(void)
{
	IIC_Init();//硬件I2C初始化
	NRF24L01_init(); //NRF初始化
	
	while(test_AT24C02()!=0);//EEPROM初始化失败
	ADC1_GPIO_Config();//ADC引脚初始化
	ADC1_Mode_Config();//ADC DMA 初始化，开启DMA后，连续转换ADC，并DMA自动回读DAC的值，往后整个程序不再手动操作读取DAC

	RC_INIT();//遥控值初始化
}




//通信协议：
//起始帧0XAA,0XAF + 地址帧 0X03 + 数据长度 + 12个遥控数据 + 校验字节


void SysTick_Handler(void)//滴答定时器1ms中断
{   
	static uint8_t Count_10ms;
	
	SysTick_count++;	
	
	if(Count_10ms++>=10)
	{
		Count_10ms = 0;
		RC_Analy();//遥控数据解析
	}
}




