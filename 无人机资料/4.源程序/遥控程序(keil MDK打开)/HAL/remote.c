/*******************************************************************
 *@brief 
 *@brief 
 *@time ZIN电子产品与技术 2017.1.8
 *@editor小南&zin
 *飞控爱好QQ群551883670,邮箱759421287@qq.com
 *非授权使用人员，禁止使用。禁止传阅，违者一经发现，侵权处理。
 *专业的飞控才是最好的飞控
 ******************************************************************/
#include "stm32f10x.h" 
#include "remote.h"
#include "ADC_DMA_Config.h"
#include "sys.h"
#include "nrf24l01.h"
#include <math.h>
#include "eeprom.h"
#include "delay.h"
__align(4) _st_Remote Remote={1000,1000,1000,1000,1000,1000,1000,1000};


void key(void);
	

struct //校准数据
{
	int16_t flag;		//校准标志位
	int16_t roll;
	int16_t pitch;	
	int16_t thr;	

	int16_t yaw;	
}offset = {0};
/******************************************************************************
函数原型：
功    能：	按键初始化
*******************************************************************************/ 

//PA15 AUX1
//PB11 AUX2
//PC14 AUX3
//PC15 AUX4
//PB1 front
//PB0 back
//PA6 left
//PA7 right
void key_init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	
	AFIO->MAPR = 0X02000000; //使用四线SWD
	RCC_APB2PeriphClockCmd( RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOC , ENABLE);
	
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6|GPIO_Pin_7|GPIO_Pin_4|GPIO_Pin_15;      
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;    //上拉输入
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0|GPIO_Pin_1|GPIO_Pin_11;
	GPIO_Init(GPIOB, &GPIO_InitStructure);	
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_14|GPIO_Pin_15;
	GPIO_Init(GPIOC, &GPIO_InitStructure);	
}
/******************************************************************************
函数原型：
功    能：	校准数据初始化
*******************************************************************************/ 
void RC_INIT(void)
{
	offset.roll =	read_AT24C02(ROLL_ADDR_OFFSET);//读取校准值
	offset.pitch =	read_AT24C02(PITCH_ADDR_OFFSET);
//	write_AT24C02(ROLL_ADDR_OFFSET,0);
//	write_AT24C02(PITCH_ADDR_OFFSET,0);
	key_init();
}
/******************************************************************************
函数原型：
功    能：	10ms发送一次数据，每个通道都是高位在前低位在后
*******************************************************************************/ 
uint8_t tx_data[32] = {0xAA,0xAF,0x03,32-5};//匿名通信协议
void NRF_SEND(void)
{
	for(uint8_t i=4;i<26;i+=2)
	{
		tx_data[i] = *((uint8_t*)&Remote+i-3);	 //高位在前
		tx_data[31] += tx_data[i];
		tx_data[i+1] = *((uint8_t*)&Remote+i-4);	
		tx_data[31] += tx_data[i];
	}
	tx_data[31] = 0;
		for(uint8_t i=0;i<31;i++)  //校验位
		{
			tx_data[31] +=  tx_data[i];
		}
	NRF24L01_TxPacket((uint8_t*)&tx_data); //调用NRF发射数据
}
/******************************************************************************
函数原型：
功    能：	遥控数据解析
*******************************************************************************/ 
void RC_Analy(void) //10ms调用一次
{
	static uint16_t last_thr,last_roll,last_pitch,last_yaw;
	//ADC_ConvertedValue  DMA自动回读的值，直接可以用，无需手动读取ADC转换结果
	Remote.thr = (uint16_t)(0.20f*ADC_ConvertedValue[1])+1000 + offset.thr;//油门
	last_thr = Remote.thr = Remote.thr*0.25f + 0.75f*last_thr;
	Remote.pitch 	= 	1000 + (uint16_t)(0.25f*ADC_ConvertedValue[2]) + offset.pitch;//
	last_pitch = Remote.pitch = Remote.pitch*0.25f + 0.75f*last_pitch;
	Remote.yaw 		= 	1000 + (uint16_t)(0.25f*ADC_ConvertedValue[0]) + offset.yaw;//方向
	last_yaw = Remote.yaw = Remote.yaw*0.25f + 0.75f*last_yaw;
	Remote.roll 	=	1000 +(uint16_t)(0.25f*ADC_ConvertedValue[3]) + offset.roll;//副翼
	last_roll = Remote.roll = Remote.roll*0.25f + 0.75f*last_roll;
  if(Remote.thr>2000)
			Remote.thr = 2000;
	else if(Remote.thr<1000)
			Remote.thr = 1000;
  if(Remote.pitch>2000)
			Remote.pitch = 2000;
	else if(Remote.pitch<1000)
			Remote.pitch = 1000;
  if(Remote.roll>2000)
			Remote.roll = 2000;
	else if(Remote.roll<1000)
			Remote.roll = 1000;
  if(Remote.yaw>2000)
			Remote.yaw = 2000;
	else if(Remote.yaw<1000)
			Remote.yaw = 1000;
	
	key();//扫描按键
	NRF_SEND();//发送到飞控端
}


/******************************************************************************
函数原型：
功    能：	按键扫描
*******************************************************************************/ 
void key(void)
{
	
//PA15 AUX1
//PB11 AUX2
//PC14 AUX3
//PC15 AUX4
//PB1 front
//PB0 back
//PA6 left
//PA7 right	
	
#define aux1 GPIO_Pin_15 //GPIOA
#define aux2 GPIO_Pin_11 //GPIOB
#define aux3 GPIO_Pin_14//GPIOC
#define aux4 GPIO_Pin_15//GPIOC
#define front GPIO_Pin_0 //GPIOB
#define back GPIO_Pin_1 //GPIOB
#define left GPIO_Pin_6 //GPIOA
#define right GPIO_Pin_7 //GPIOA	
	
   volatile static uint8_t status = 0;	
 	static uint32_t temp;
	switch(status)
	{
		case 0:
			if(SysTick_count - temp >30) //300ms 按键音
			{
				if(  ((GPIOA->IDR & (GPIO_Pin_6|GPIO_Pin_7|GPIO_Pin_15)) == (GPIO_Pin_6|GPIO_Pin_7|GPIO_Pin_15))
					&& ((GPIOB->IDR & (GPIO_Pin_0|GPIO_Pin_1|GPIO_Pin_11)) == (GPIO_Pin_0|GPIO_Pin_1|GPIO_Pin_11))
					&& ((GPIOC->IDR & (GPIO_Pin_14|GPIO_Pin_15)) == (GPIO_Pin_14|GPIO_Pin_15))
					)
					status = 1;
				BEEP_L;
			}
			break;
		case 1:
			if(((GPIOA->IDR & (GPIO_Pin_6|GPIO_Pin_7|GPIO_Pin_15)) != (GPIO_Pin_6|GPIO_Pin_7|GPIO_Pin_15))
				|| ((GPIOB->IDR & (GPIO_Pin_0|GPIO_Pin_1|GPIO_Pin_11)) != (GPIO_Pin_0|GPIO_Pin_1|GPIO_Pin_11))
				|| ((GPIOC->IDR & (GPIO_Pin_14|GPIO_Pin_15)) != (GPIO_Pin_14|GPIO_Pin_15))
				)
				status = 2;
			break;
		case 2:	
			if(!(GPIOA->IDR & aux1))
			{
					Remote.AUX1 ^= (2000^1000); //1000和2000之间变化
			}
			else if(!(GPIOB->IDR & aux2))
			{
					Remote.AUX2 ^= (2000^1000); //1000和2000之间变化
			}
			else if(!(GPIOC->IDR & aux3))
			{
					Remote.AUX3 ^= (2000^1000); //1000和2000之间变化
			}
			else if(!(GPIOC->IDR & aux4))
			{
					Remote.AUX4 ^= (2000^1000); //1000和2000之间变化	
			}			
			else if(!(GPIOA->IDR & left))
			{
				if(offset.roll>-250)
				{
					offset.roll-=5;	
					write_AT24C02(ROLL_ADDR_OFFSET,offset.roll);	//记录微调校准值	
				}					
			}
			else if(!(GPIOA->IDR & right))
			{
				if(offset.roll<250)
				{
					offset.roll+=5;
					write_AT24C02(ROLL_ADDR_OFFSET,offset.roll);
				}					
			}
			else if(!(GPIOB->IDR & front))
			{
				if(offset.pitch>-250)
				{
					offset.pitch-=5;	
					write_AT24C02(PITCH_ADDR_OFFSET,offset.pitch);	//记录微调校准值	
				}
			}
			else if(!(GPIOB->IDR & back))
			{
				if(offset.pitch<250)
				{
					offset.pitch+=5;
					write_AT24C02(PITCH_ADDR_OFFSET,offset.pitch);	//记录微调校准值	
				}
			}
			status = 0;			
			BEEP_H;
			temp = SysTick_count;
			break;
		}			
}


