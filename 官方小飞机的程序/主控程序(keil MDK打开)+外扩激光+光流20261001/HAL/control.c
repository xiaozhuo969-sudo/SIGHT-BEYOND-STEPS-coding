/*******************************************************************
 *MPU6050
 *@brief 
 *@brief 
 *@time  2023.3.23
 *@editor小南&zin
 *飞控爱好QQ群551883670,邮箱759421287@qq.com
 *非授权使用人员，禁止使用。禁止传阅，违者一经发现，侵权处理。

 ******************************************************************/
#include "ALL_DATA.h" 
#include "ALL_DEFINE.h" 
#include "control.h"
#include "pid.h"
#include "flow.h"
#include "kalman.h"
//------------------------------------------------------------------------------
#undef NULL
#define NULL 0
#undef DISABLE 
#define DISABLE 0
#undef ENABLE 
#define ENABLE 1
#undef REST
#define REST 0
#undef SET 
#define SET 1 
#undef EMERGENT
#define EMERGENT 0
//------------------------------------------------------------------------------
PidObject *(pPidObject[])={&pidRateX,&pidRateY,&pidRateZ,&pidRoll,&pidPitch,&pidYaw   //结构体数组，将每一个数组放一个pid结构体，这样就可以批量操作各个PID的数据了  比如解锁时批量复位pid控制数据，新手明白这句话的作用就可以了
		,&pidHeightRate
		,&pidHeightHigh
		,&pidPosRateX
		,&pidPosRateY		
		,&pidPositionX
		,&pidPositionY	
};

float sins_high;
float sins_vel;

uint16_t thr_hold = 0; //进入高度时记录当前油门值
/**************************************************************
 *  Height control  //仅供加装LC06高度计的买家使用，基本版进入此函数会立刻返回
 * @param[in] 
 * @param[out] 
 * @return     
 ***************************************************************/
float last_high,last_vel;
 uint32_t VL53L01_high = 0; //当前高度
static uint8_t high_error_count;
  uint8_t set_high = 0;
void HeightPidControl(float dt)
{
	volatile static uint8_t status=WAITING_1;
		int16_t acc;       //当前你飞行器的加速度值
   	int16_t acc_error; //当前加速度减去重力加速度则为上下移动的加速度
   static int16_t acc_offset;//重力加速度值
	 
	{ //获取垂直速度数据

		acc = (int16_t)GetNormAccz();//提取重力向量
		
			if(!ALL_flag.unlock) //取得静态加速度值
			{
				acc_offset = acc;
			}
			acc_error = acc - acc_offset;	
			
			if(VL53L01_high<3500)			
			{//此处做一个速度与高度的互补滤波 
					if(VL53L01_high-sins_high>50)sins_high += 50;//高度异常突变
					else if(VL53L01_high-sins_high<-50)sins_high -= 50;//高度异常突变
					else 	sins_high= VL53L01_high;
				
					sins_vel=(last_vel + acc_error * dt)*0.985f+0.015f*(sins_high - last_high)/dt; //速度环永远是主调，所以互补滤波关键就抓在速度环这里
			
				//sins_high= high;
				pidHeightRate.measured = last_vel=	sins_vel;
				pidHeightHigh.measured=last_high = sins_high;  //高度辅调

			}	
		}
	//----------------------------------------------紧急终止飞行
	if(ALL_flag.unlock == EMERGENT) //意外情况，请使用遥控紧急上锁，飞控就可以在任何情况下紧急中止飞行，锁定飞行器，退出PID控制
		status = EXIT_255;
	//----------------------------------------------控制
	switch(status)
	{
		case WAITING_1: //检测解锁
		  if( ALL_flag.unlock) 
			{
				pidHeightRate.measured=0;
				
				pidHeightRate.measured = last_vel=	sins_vel=0;
				pidHeightHigh.measured=last_high = sins_high;
				status = WAITING_2;
				high_error_count=0;
			}
			break;
		case WAITING_2: //检测定高，定高前准备
			if(ALL_flag.height_lock) 
			{
				
				set_high=0;

				LED.status = WARNING;
			thr_hold=500;
			status = PROCESS_31;
			}
			break;
		
		case PROCESS_31://进入定高	
		
			 if(Remote.thr<1750 && Remote.thr>1150) //如果油门已回中，不调高度
			 {
						if(set_high == 0) //如果刚退出调高，记录当前位置为定高位置
						{
							set_high = 1;
							pidHeightHigh.desired = pidHeightHigh.measured;//记录油门回中的高度当做当前定高高度
						}
						pidUpdate(&pidHeightHigh,dt);    //调用PID处理函数来处理外环	俯仰角PID	
						pidHeightRate.desired = pidHeightHigh.out;  
			 }
			else if(Remote.thr>1750) //油门上拉则上升 调整高度
			{
				if(VL53L01_high<3500)//高度大于3500mm太高不要飞
				{
					set_high = 0;
					pidHeightRate.desired = 250; //上升速度可调
				}
			}
			else if	(Remote.thr<1150) //油门下拉则下降	调整高度	
			{
				set_high = 0;
				pidHeightRate.desired = -350; //下降速度可调
				if(pidHeightHigh.measured<10)//着地
				{
					ALL_flag.unlock = 0;
				}
			}					 
								 
			pidUpdate(&pidHeightRate,dt); //再调用内环				
				 
			if(!ALL_flag.height_lock)  //退出定高,当遥控操作退出定高或者大于3.5m，激光测距的限制
			{
				LED.status = AlwaysOn ;
				status = EXIT_255;
			}
			if(VL53L01_high<50||VL53L01_high>3700)//高度异常
			{
				high_error_count++;
				if(high_error_count>50)//50*6ms,300ms都是异常
				{
						ALL_flag.height_lock=0;
						ALL_flag.flow_control=0;//退出定高定点
						status = EXIT_255;
				}
			}
			else
			{
				high_error_count=0;
			}
			break;
		case EXIT_255: //退出定高
			pidRest(&pPidObject[6],2);	//清除当前的定高输出值
			status = WAITING_1;//回到等待进入定高
			break;
		default:
			status = WAITING_1;
			break;	
	}	
}
/**************************************************************
 *  flow control
 * @param[in] 
 * @param[out] 
 * @return     
 ***************************************************************/


void FlowPidControl(float dt)
{
	volatile static uint8_t status=WAITING_1;
	static uint8_t set_pos = 0;

	//----------------------------------------------紧急终止飞行
	if(ALL_flag.unlock == EMERGENT) //意外情况，请使用遥控紧急上锁，飞控就可以在任何情况下紧急中止飞行，锁定飞行器，退出PID控制
		status = EXIT_255;
	//----------------------------------------------控制
	switch(status)
	{
		case WAITING_1:		
			if(ALL_flag.unlock) //检测解锁
			{
					status = WAITING_2;	
			}
			break;
		case WAITING_2: //当油门大于1300  即默认为起飞后进入定点控制
			if(ALL_flag.flow_control&&VL53L01_high>200) 
			{
				pidRest(&pPidObject[8],4);  //复位上次遗留下来的PID数据
				status = PROCESS_31;
				set_pos = 1;
			}			
			break;
		case PROCESS_31://进入
			{
				
				if(Remote.roll>1750||Remote.roll<1250||Remote.pitch>1750||Remote.pitch<1250)
				{
						set_pos = 1;
						if(Remote.roll>1750)
							pidPosRateX.desired = -15;   //直接控制速度 并且关掉外环计算
						else if(Remote.roll<1250)
							pidPosRateX.desired = 15; 
						if(Remote.pitch>1750)
							pidPosRateY.desired = 15;   //直接控制速度 并且关掉外环计算
						else if(Remote.pitch<1250)
							pidPosRateY.desired = -15; 
				}
				else
				{	
					
					if(set_pos == 1)   //记录位置 一次
					{
						set_pos = 0; 		//关闭记录位置标志

						pidPositionX.desired = flow_x_lpf_att_i;
						pidPositionY.desired = flow_y_lpf_att_i;	
					}
					
					//外环位置控制
					pidPositionX.measured = flow_x_lpf_att_i;//实时位置反馈
					pidUpdate(&pidPositionX,dt);//位置运算PID
					pidPositionY.measured = flow_y_lpf_att_i;//实时位置反馈
					pidUpdate(&pidPositionY,dt);//位置运算PID
					//内环期望
					pidPosRateX.desired = LIMIT(pidPositionX.out,-20,20);//位置PID输出给速度期望
					pidPosRateY.desired = LIMIT(pidPositionY.out,-20,20);//位置PID输出给速度期望
				}
				
								//内环
				pidPosRateX.measured = flow_x_vel_lpf_i;//速度反馈
				pidUpdate(&pidPosRateX,dt);//速度运算
				pidPosRateY.measured = flow_y_vel_lpf_i;//速度反馈
				pidUpdate(&pidPosRateY,dt);//速度运算
				
				
				pidRoll.desired = LIMIT(pidPosRateX.out,-15,15)  ; //姿态外环期望值
				pidPitch.desired = -LIMIT(pidPosRateY.out,-15,15); //姿态外环期望值		

				if(!ALL_flag.flow_control||!ALL_flag.height_lock)
				{
			
					status = EXIT_255;
				}
			}
			break;	
		case EXIT_255: //退出定点
			pidRest(&pPidObject[8],4);	
			status = WAITING_1;
			break;
		default:
			status = WAITING_1;
			break;		
	}
}
/**************************************************************
 *  flight control
 * @param[in] 
 * @param[out] 
 * @return     
 ***************************************************************/
void FlightPidControl(float dt)
{
	volatile static uint8_t status=WAITING_1;

	switch(status)
	{		
		case WAITING_1: //等待解锁
			if(ALL_flag.unlock)
			{
				status = READY_11;	
			}			
			break;
		case READY_11:  //准备进入控制
			pidRest(pPidObject,6); //批量复位PID数据，防止上次遗留的数据影响本次控制

			Angle.yaw = pidYaw.desired =  pidYaw.measured = 0;   //锁定偏航角
		
			status = PROCESS_31;
		
			break;			
		case PROCESS_31: //正式进入控制
			if(Angle.pitch<-50||Angle.pitch>50||Angle.roll<-50||Angle.roll>50)//倾斜检测，大角度判定为意外情况，则紧急上锁	
					if(Remote.thr>1200)//当油门的很低时不做倾斜检测，防止在空中油门拉的太低，飞行器做自由落体，自由落体下状态未知
						ALL_flag.unlock = EMERGENT;//打入紧急情况
					
      pidRateX.measured = MPU6050.gyroX * Gyro_G; //内环测量值 角度/秒
			pidRateY.measured = MPU6050.gyroY * Gyro_G;
			pidRateZ.measured = MPU6050.gyroZ * Gyro_G;
		
			pidPitch.measured = Angle.pitch; //外环测量值 单位：角度
		  pidRoll.measured = Angle.roll;
			pidYaw.measured = Angle.yaw;
		
		 	pidUpdate(&pidRoll,dt);    //调用PID处理函数来处理外环	横滚角PID		
			pidRateX.desired = pidRoll.out; //将外环的PID输出作为内环PID的期望值即为串级PID
			pidUpdate(&pidRateX,dt);  //再调用内环

		 	pidUpdate(&pidPitch,dt);    //调用PID处理函数来处理外环	俯仰角PID	
			pidRateY.desired = pidPitch.out;  
			pidUpdate(&pidRateY,dt); //再调用内环

			pidUpdate(&pidYaw,dt);    //调用PID处理函数来处理外环	偏航角PID	
			pidRateZ.desired = pidYaw.out;  
			pidUpdate(&pidRateZ,dt); //再调用内环
			break;
		case EXIT_255:  //退出控制
			pidRest(pPidObject,6);
			status = WAITING_1;//返回等待解锁
		  break;
		default:
			status = EXIT_255;
			break;
	}
	if(ALL_flag.unlock == EMERGENT) //意外情况，请使用遥控紧急上锁，飞控就可以在任何情况下紧急中止飞行，锁定飞行器，退出PID控制
		status = EXIT_255;
}
/**************************************************************
 *  motor out
 * @param[in] 
 * @param[out] 
 * @return     
 ***************************************************************/

#define MOTOR1 motor_PWM_Value[0] 
#define MOTOR2 motor_PWM_Value[1] 
#define MOTOR3 motor_PWM_Value[2] 
#define MOTOR4 motor_PWM_Value[3] 
uint16_t low_thr_cnt;

void MotorControl(void)
{	
	volatile static uint8_t status=WAITING_1;
	
	
	if(ALL_flag.unlock == EMERGENT) //意外情况，请使用遥控紧急上锁，飞控就可以在任何情况下紧急中止飞行，锁定飞行器，退出PID控制
		status = EXIT_255;	
	switch(status)
	{		
		case WAITING_1: //等待解锁	
			MOTOR1 = MOTOR2 = MOTOR3 = MOTOR4 = 0;  //如果锁定，则电机输出都为0
			if(ALL_flag.unlock)
			{
				status = WAITING_2;
			}
		case WAITING_2: //解锁完成后判断使用者是否开始拨动遥杆进行飞行控制
			if(Remote.thr>1100)
			{
				status = PROCESS_31;
			}
			break;
		case PROCESS_31:
			{
				int16_t thr;
				if(ALL_flag.height_lock) //定高模式下 油门遥杆作为调整高度使用
				{		
					thr = pidHeightRate.out+thr_hold; //输出给电机的是定高输出值
				}
				else //正常飞行状态，油门正常使用
				{
					int16_t temp;
					temp = Remote.thr -1000; //油门+定高输出值					
						//油门比例规划
						thr = 200+0.45f * temp;
						thr_hold = thr;
						if(temp<10) 
						{
							
							low_thr_cnt++;
							if(low_thr_cnt>300)//1500ms
							{
								thr = 0;
								
								pidRest(pPidObject,6);
								MOTOR1 = MOTOR2 = MOTOR3 = MOTOR4 =0;
								status = WAITING_2;
								break;
							}
						}
						else low_thr_cnt=0;
				}
				
				//将油门值作为基础值给PWM
				MOTOR1 = MOTOR2 = MOTOR3 = MOTOR4 = LIMIT(thr,0,800); //留200给姿态控制
//以下输出的脉冲分配取决于电机PWM分布与飞控坐标体系。请看飞控坐标体系图解，与四个电机PWM分布分布	
//           机头      
//   PWM3     ♂       PWM1
//      *           *
//      	*       *
//    		  *   *
//      			*  
//    		  *   *
//      	*       *
//      *           *
//    PWM4           PWM2			
//		pidRateX.out 横滚角串级PID输出 控制左右，可以看出1 2和3 4，左右两组电机同增同减
//    pidRateY.out 俯仰角串级PID输出 控制前后，可以看出2 3和1 4，前后两组电机同增同减
//		pidRateZ.out 横滚角串级PID输出 控制旋转，可以看出2 4和1 3，两组对角线电机同增同减	

// 正负号取决于算法输出 比如输出是正的话  往前飞必然是尾巴两个电机增加,往右飞必然是左边两个电机增加		

				MOTOR1 +=    + pidRateX.out + pidRateY.out + pidRateZ.out;//; 姿态输出分配给各个电机的控制量
				MOTOR2 +=    + pidRateX.out - pidRateY.out - pidRateZ.out ;//;
				MOTOR3 +=    - pidRateX.out + pidRateY.out - pidRateZ.out;
				MOTOR4 +=    - pidRateX.out - pidRateY.out + pidRateZ.out;//;
			}	
			break;
		case EXIT_255:
			MOTOR1 = MOTOR2 = MOTOR3 = MOTOR4 = 0;  //如果锁定，则电机输出都为0
			status = WAITING_1;	
			break;
		default:
			break;
	}
	
	
	TIM2->CCR1 = LIMIT(MOTOR1,0,1000);  //更新PWM
	TIM2->CCR2 = LIMIT(MOTOR2,0,1000);
	TIM2->CCR3 = LIMIT(MOTOR3,0,1000);
	TIM2->CCR4 = LIMIT(MOTOR4,0,1000);
} 

/************************************END OF FILE********************************************/ 
