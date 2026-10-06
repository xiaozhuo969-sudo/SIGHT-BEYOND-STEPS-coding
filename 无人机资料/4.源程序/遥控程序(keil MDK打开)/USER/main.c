/*******************************************************************
 *程序入口
 *@brief 主初始化函数。轮询调用的函数
 *@brief 
 *@time ZIN电子产品与技术 2017.1.8
 *@editor小南&zin
 *飞控爱好QQ群551883670,邮箱759421287@qq.com
 *非授权使用人员，禁止使用。禁止传阅，违者一经发现，侵权处理。
 *专业的飞控才是最好的飞控
 *@brief
 通用型遥控，可与任意NRF通信，只要NRF地址码匹配，地址码请见NRF24L01.C
 配套数据帧收发详情请见数据帧手册。
 ******************************************************************/
#include "stm32f10x.h"
#include "sys.h"
#include "stdio.h"
#include "delay.h"
#include "usart.h"
//配置系统时钟,使能各外设时钟
void RCC_Configuration(void)
{
		RCC_DeInit();//将外设 RCC寄存器重设为缺省值  
  
    RCC_HSICmd(ENABLE);//使能HSI    
    while(RCC_GetFlagStatus(RCC_FLAG_HSIRDY) == RESET);//等待HSI使能成功  
    //加上这两句才能到64M
    FLASH_PrefetchBufferCmd(FLASH_PrefetchBuffer_Enable);  
    FLASH_SetLatency(FLASH_Latency_2);  
     
    RCC_HCLKConfig(RCC_SYSCLK_Div1);     
    RCC_PCLK1Config(RCC_HCLK_Div2);  
    RCC_PCLK2Config(RCC_HCLK_Div1);  
      
    //设置 PLL 时钟源及倍频系数  48Mhz
    RCC_PLLConfig(RCC_PLLSource_HSI_Div2, RCC_PLLMul_12);//使能或者失能 PLL,这个参数可以取：ENABLE或者DISABLE   
    RCC_PLLCmd(ENABLE);//如果PLL被用于系统时钟,那么它不能被失能  
    //等待指定的 RCC 标志位设置成功 等待PLL初始化成功  
    while(RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == RESET);  
  
    //设置系统时钟（SYSCLK） 设置PLL为系统时钟源  
    RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);//选择想要的系统时钟   
    //等待PLL成功用作于系统时钟的时钟源  
    //  0x00：HSI 作为系统时钟   
    //  0x04：HSE作为系统时钟   
    //  0x08：PLL作为系统时钟    
    while(RCC_GetSYSCLKSource() != 0x08);//需与被选择的系统时钟对应起来，RCC_SYSCLKSource_PLL  
  
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA 
                           |RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOC
						   |RCC_APB2Periph_ADC1  | RCC_APB2Periph_AFIO 
                           |RCC_APB2Periph_SPI1  
						   	, ENABLE );
	
   	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3 | RCC_APB1Periph_I2C1| RCC_APB1Periph_TIM3, ENABLE );
}



void NVIC_Configuration(void)
{
	NVIC_InitTypeDef NVIC_InitStructure; 
	/* Configure the NVIC Preemption Priority Bits */  
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_0);
	
	/* 使能串口1中断 */
	NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;	//USART1  串口1全局中断 
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;//抢占优先级1
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0; //子优先级1
	/*IRQ通道使能*/
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	/*根据NVIC_InitStruct中指定的参数初始化外设NVIC寄存器USART1*/
	NVIC_Init(&NVIC_InitStructure);
	
	
	NVIC_InitStructure.NVIC_IRQChannel = TIM3_IRQn;  //TIM3中断
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;  //先占优先级0级
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;  //从优先级3级
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE; //IRQ通道被使能
	NVIC_Init(&NVIC_InitStructure);  //根据NVIC_InitStruct中指定的参数初始化外设NVIC寄存器
}

void BEEP_INIT(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);    
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_12;          //PA12
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;    //推挽输出
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
	
	BEEP_H;
}



//main函数 程序从这里开始执行
int main(void)
{


	RCC_Configuration();//配置晶振//遥控要求比较低。可以使用使用内部晶振，8M/2分频*12倍频=48MHz
	
	cycleCounterInit();//初始化每个滴答定时器的计数值，为延时函数做准备
	
	
	BEEP_INIT(); //开机蜂鸣声
	{
		int32_t beep_cnt = 2000000;
		while(beep_cnt-->0)
		{
			
		}
		
		BEEP_L;
	}
	
	
	
	SysTick_Config(48000000 / 1000);	//配置系统滴答时钟 开启1ms发生一次中断
	_g_Init_sys(); //10ms中断处理，详情请见SYS.C


	NVIC_Configuration();//中断向量配置
	
	
//	USART1_Config();//备用串口
	
	while(1)
	{
		//所有数据都在10ms中断处理，详情请见SYS.C
	}
}










