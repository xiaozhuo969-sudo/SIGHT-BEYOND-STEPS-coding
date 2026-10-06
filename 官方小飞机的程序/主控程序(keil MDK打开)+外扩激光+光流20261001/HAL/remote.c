/*******************************************************************
 *@title Remote Control;
 *@brief remote data process on polling every 10ms.
 *@brief remote control on interrupt every 24ms
 *@time  2016.11.21
 *@editor小南&zin
 *飞控爱好QQ群551883670,邮箱759421287@qq.com
 ******************************************************************/
#include "ALL_DATA.h"
#include "nrf24l01.h"
#include "control.h"
#include <math.h>
#include "myMath.h"
#include "LED.h"
#include "Remote.h"
#include "mpu6050.h"
#include "hand_link.h"

#define SUCCESS 0
#undef FAILED
#define FAILED  1
/*****************************************************************************************
 *  通道数据处理
 * @param[in] 
 * @param[out] 
 * @return     
 ******************************************************************************************/	
	uint8_t RC_rxData[32];
void remote_unlock(void);
uint16_t last_Remote_AUX1 = 1000;
uint16_t last_Remote_AUX3 = 1000;
uint16_t last_Remote_AUX4 = 1000;
uint16_t last_Remote_AUX2 = 1000;

int16_t roll_offset; //微调值
int16_t pitch_offset;
 

/* Receiver ownership ends after loss of fresh input. A different controller
 * must always perform its own complete unlock sequence. */
volatile uint32_t legacy_received,legacy_last_ms,legacy_active,legacy_timeouts;
static uint8_t unlock_status=WAITING_1,unlock_high_count;
static uint16_t unlock_low_count;
void remote_reset_session(void)
{
 unlock_status=WAITING_1;unlock_high_count=0;unlock_low_count=0;legacy_active=0;
}
void remote_timeout_tick(void)
{
 if(hand_link_owned()||!legacy_active)return;
 if((uint32_t)(hand_milliseconds-legacy_last_ms)<=600)return;
 ++legacy_timeouts;remote_reset_session();
 ALL_flag.unlock=ALL_flag.height_lock=ALL_flag.flow_control=0;
 Remote.thr=1000;Remote.roll=Remote.pitch=Remote.yaw=1500;
 for(unsigned i=0;i<4;i++)motor_PWM_Value[i]=0;
 TIM2->CCR1=TIM2->CCR2=TIM2->CCR3=TIM2->CCR4=0;
 LED.status=AllFlashLight;LED.FlashTime=300;
}

void RC_Analy(void)  
{
/*             Receive  and check RC data                               */	
	if(NRF24L01_RxPacket(RC_rxData)==SUCCESS)
	{ 	

		if(hand_link_receive(RC_rxData))return;
		uint8_t i;
		uint8_t CheckSum=0;
		for(i=0;i<31;i++)
		{
			CheckSum +=  RC_rxData[i];
		}
		if(RC_rxData[31]==CheckSum && RC_rxData[0]==0xAA && RC_rxData[1]==0xAF && RC_rxData[2]==3 && RC_rxData[3]==27)  //如果接收到的遥控数据正确
		{
          if(!legacy_active)remote_reset_session();
          legacy_active=1;legacy_last_ms=hand_milliseconds;++legacy_received;
			  Remote.roll = ((uint16_t)RC_rxData[4]<<8) | RC_rxData[5];  //通道1
				Remote.roll = LIMIT(Remote.roll,1000,2000);
				Remote.pitch = ((uint16_t)RC_rxData[6]<<8) | RC_rxData[7];  //通道2
				Remote.pitch = LIMIT(Remote.pitch,1000,2000);
				Remote.thr = 	((uint16_t)RC_rxData[8]<<8) | RC_rxData[9];   //通道3
				Remote.thr = 	LIMIT(Remote.thr,1000,2000);
				Remote.yaw =  ((uint16_t)RC_rxData[10]<<8) | RC_rxData[11];   //通道4
				Remote.yaw =  LIMIT(Remote.yaw,1000,2000);
				Remote.AUX1 =  ((uint16_t)RC_rxData[12]<<8) | RC_rxData[13];   //通道5  左上角按键都属于通道5  
				Remote.AUX1 =  LIMIT(Remote.AUX1,1000,2000);
				Remote.AUX2 =  ((uint16_t)RC_rxData[14]<<8) | RC_rxData[15];   //通道6  右上角按键都属于通道6 
				Remote.AUX2 =  LIMIT(Remote.AUX2,1000,2000);
				Remote.AUX3 =  ((uint16_t)RC_rxData[16]<<8) | RC_rxData[17];   //通道7  左下边按键都属于通道7 
				Remote.AUX3 =  LIMIT(Remote.AUX3,1000,2000);
				Remote.AUX4 =  ((uint16_t)RC_rxData[18]<<8) | RC_rxData[19];   //通道8  右下边按键都属于通道6  
				Remote.AUX4 = LIMIT(Remote.AUX4,1000,2000);		

							//通道5作为定高控制 每按一下就会在1000和2000之间切换 
							if(Remote.AUX1!=last_Remote_AUX1) //如果大于1700 则进入定高模式
							{
								last_Remote_AUX1=Remote.AUX1;
								ALL_flag.height_lock = ALL_flag.flow_control ^= 1;//取反
			
							}														
				{
						
							if(!ALL_flag.flow_control)//如果不处于定点模式则飞行角度由遥控的遥杆控制
							{
										 float roll_pitch_ratio = 0.04f;

										pidPitch.desired =(-(Remote.pitch-1500)+pitch_offset)*roll_pitch_ratio;	 //将遥杆值作为飞行角度的期望值
										pidRoll.desired = (-(Remote.roll-1500)+roll_offset)*roll_pitch_ratio;	
							}

							{
									const float yaw_ratio =   0.5f;		//偏航角灵敏度度
							
									if(Remote.yaw>1820)
									{
										pidYaw.desired -= yaw_ratio;	//符号控制偏航角方向
									}
									else if(Remote.yaw <1180)
									{
										pidYaw.desired += yaw_ratio;	//符号控制偏航角方向
									}		
							}							
				}
				remote_unlock();
			
		}
  }
  /* The 3 ms scheduler runs remote_timeout_tick using real milliseconds. */
}

/*****************************************************************************************
 *  解锁判断
 * @param[in] 
 * @param[out] 
 * @return     
 ******************************************************************************************/	
#include "imu.h"
void remote_unlock(void)
{
	/* State is reset when ownership changes or the link times out. */

	if(Remote.thr<1050 &&Remote.yaw<1150)                         //油门遥杆左下角锁定
	{
		unlock_status = EXIT_255;
	}
	
	switch(unlock_status)
	{
		case WAITING_1://等待解锁
			if(Remote.thr<1150)           //解锁三步奏，油门最低->油门最高->油门最低 看到LED灯不闪了 即完成解锁
			{			 
					 unlock_status = WAITING_2;				 
			}		
			break;
		case WAITING_2:
			if(Remote.thr>1800)          
			{		
					 	unlock_high_count++;		
						if(unlock_high_count>5) //最高油门需保持200ms以上，防止遥控开机初始化未完成的错误数据
						{	
								unlock_high_count=0;
								unlock_status = WAITING_3;
						}
			}			
			break;
		case WAITING_3:
			if(Remote.thr<1100)          
			{			 
					 unlock_status = WAITING_4;				 
			}			
			break;			
		case WAITING_4:	//解锁前准备	  
						
				ALL_flag.unlock = 1;
				unlock_status = PROCESS_31;
				LED.status = AlwaysOn;	
				last_Remote_AUX1=Remote.AUX1;
				ALL_flag.height_lock = 0;//必须在姿态模式下起飞
				ALL_flag.flow_control = 0;//必须在姿态模式下起飞	
				pidRoll.offset =   -(Remote.roll-1500)*0.04;//微调值做前馈
				pidPitch.offset  = -(Remote.pitch-1500)*0.04;
				roll_offset = (Remote.roll-1500); //记录微调值
				pitch_offset = (Remote.pitch-1500);
						imu_rest();	
				 break;		
		case PROCESS_31:	//进入解锁状态
				if(Remote.thr<1020)
				{
					if(!ALL_flag.height_lock )
					{
						if(unlock_low_count++ > 3000)                                     // 油门遥杆处于最低9S自动上锁
						{								
							unlock_status = EXIT_255;								
						}
					}
				}
				else if(!ALL_flag.unlock)                           //Other conditions lock 
				{
					unlock_status = EXIT_255;				
				}
				else					
					unlock_low_count = 0;
			break;
		case EXIT_255: //进入锁定
			LED.status = AllFlashLight;	                                 //exit
			unlock_low_count = 0;
			LED.FlashTime = 300; //100*3ms		
			ALL_flag.unlock = 0;
			unlock_status = WAITING_1;
			break;
		default:
			unlock_status = EXIT_255;
			break;
	}
}
/***********************END OF FILE*************************************/







