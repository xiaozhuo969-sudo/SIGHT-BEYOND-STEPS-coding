#include "stm32f10x.h"
#include "ALL_DEFINE.h"
#include "flow.h"
#include "control.h"
#include "hand_link.h"





 FLow flow_data;



//       ^ Y
//       |
//       |
//<------------X
//       |
//       |


float gyroX_i;
float gyroY_i;

float flow_x_i;
float flow_y_i;

float flow_x_vel_lpf_i;
float flow_y_vel_lpf_i;
float flow_x_pos_lpf_i;
float flow_y_pos_lpf_i;

float flow_x_lpf_att_i;
float flow_y_lpf_att_i;
float flow_x_att;
float flow_y_att;
float flow_vel_x_i;
float flow_vel_y_i;
float last_flow_pos_x_i;
float last_flow_pos_y_i;


#define  angle_to_rad  0.0174f  //角度转弧度


#include <math.h>
void flow_data_sins()
{

	if(flow_data.squal>30&&VL53L01_high>=80)
	{
//低通滤波
		flow_x_pos_lpf_i += (flow_data.pos_x_i - flow_x_pos_lpf_i) *0.10;
		flow_y_pos_lpf_i += (flow_data.pos_y_i - flow_y_pos_lpf_i) *0.10;
		//倾角位移补偿，并做低通滤波
		flow_x_att += (700.0f*tanf(Angle.roll *angle_to_rad) - flow_x_att) *0.10;
		flow_y_att += (700.0f*tanf(Angle.pitch*angle_to_rad) - flow_y_att) *0.10;
	
   //倾角补偿后的位移输出
	flow_x_lpf_att_i = (flow_x_pos_lpf_i+flow_x_att)*0.8f; 
	flow_y_lpf_att_i = (flow_y_pos_lpf_i-flow_y_att)*0.8f; 

	//求微分速度  
	flow_vel_x_i = (flow_x_lpf_att_i - last_flow_pos_x_i);  
	flow_vel_y_i = (flow_y_lpf_att_i - last_flow_pos_y_i);  	
  last_flow_pos_x_i = flow_x_lpf_att_i; 
	last_flow_pos_y_i = flow_y_lpf_att_i;  

	//低通滤波
	flow_x_vel_lpf_i += (  flow_vel_x_i - flow_x_vel_lpf_i ) * 0.15f;
	flow_y_vel_lpf_i += ( flow_vel_y_i - flow_y_vel_lpf_i ) * 0.15f;
		
	 //激光高度补偿位移	//高度越高 位移数值应该越大
	flow_x_vel_lpf_i += +flow_vel_x_i*(VL53L01_high/3400.f);
	flow_y_vel_lpf_i += +flow_vel_y_i*(VL53L01_high/3400.f);
	}
	else 
	{
		flow_x_att=0;
		flow_y_att=0;
		flow_x_pos_lpf_i=0;
		flow_y_pos_lpf_i=0;
		flow_data.pos_x_i=0;
		flow_data.pos_y_i=0;
		flow_x_lpf_att_i=0;
  	flow_y_lpf_att_i=0;
		last_flow_pos_x_i=0;
		last_flow_pos_y_i=0;

		flow_x_vel_lpf_i=0;
		flow_y_vel_lpf_i=0;
		flow_vel_x_i=0;
		flow_vel_y_i=0;

	}
}



uint8_t temp_data[6];
uint8_t error_count=0;

void flow_get_data(void)
//----------------------------------------------	
{	//获取当前激光高度数据  LC08激光光流模块

	#define LC08_ADDRESS 0xC4 //模块地址 最低位读写标志位 0：写  1：读
	#define READ_ADDRESS 0X33 //高度数据寄存器地址
	if(IIC_read_Bytes(LC08_ADDRESS,READ_ADDRESS,temp_data,5) != FAILED)//如果读取成功
	{
			u16 high;
			flow_data.vel_x =(*(int8_t*)&temp_data[2]);
			flow_data.vel_y = (*(int8_t*)&temp_data[3]);
			//光流数据积分得出位移
			flow_data.pos_x_i += flow_data.vel_x;
			flow_data.pos_y_i += flow_data.vel_y;
			flow_data.squal = temp_data[4];
			 high = ((u32)temp_data[0]<<8) | ((u16)temp_data[1]) ;   //激光高度
			hand_link_height(true,high);
			if(high!=0)
				VL53L01_high=high;
			error_count=0;
	}
	else
	{
			hand_link_height(false,0);
			if(error_count<10)//如果通信失败，则要退出定高定点
			{
				error_count++;
			}
			else
			{
					ALL_flag.height_lock=0;
					ALL_flag.flow_control=0;
			}
	}
	
}	

















