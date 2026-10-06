#include "ALL_DEFINE.h"
#include "obstacle.h"


volatile uint32_t SysTick_count; //系统时间计数
_st_Mpu MPU6050;   //MPU6050原始数据
_st_AngE Angle;    //当前角度姿态值
_st_Remote Remote; //遥控通道值


_st_ALL_flag ALL_flag; //系统标志位，包含解锁标志位等


PidObject pidRateX; //内环PID数据
PidObject pidRateY;
PidObject pidRateZ;

PidObject pidPitch; //外环PID数据
PidObject pidRoll;
PidObject pidYaw;


PidObject pidHeightRate;
PidObject pidHeightHigh;


PidObject pidPositionX;
PidObject pidPositionY;

PidObject pidPositionX;
PidObject pidPositionY;
PidObject pidPosRateX;
PidObject pidPosRateY;

void pid_param_Init(void); //PID控制参数初始化，改写PID并不会保存数据，请调试完成后直接在程序里更改 再烧录到飞控


int16_t motor_PWM_Value[4];//


void ALL_Init(void)
{
	delay_ms(1000);    //单独使用USB供电需要延时，怕刚插入电压不稳定
	
	USB_HID_Init();   		//USB上位机初始化
	
	IIC_Init();             //I2C初始化
	
	pid_param_Init();       //PID参数初始化
	
	MpuInit();              //MPU6050初始化
//----------------------------------------	
// 水平静止标定，该功能只需要进行一次，不要每次进行。店家发货前已经进行一次标定了，标定完后会自动保存到MCU的FLASH中。
// 如需校准，重新打开即可，延时5S是为了插上电池后有充足的时间将飞行器放在地上进行水平静止标定。
//delay_ms(1000); 	MpuGetOffset();
//----------------------------------------		
//	USART1_Config();  //备用串口     

	
	NRF24L01_init();				//2.4G遥控通信初始化
	
	TIM2_PWM_Config();			//4路PWM初始化
	TIM3_PWM_Config();      //LED PWM初始化
	
	
	
	obstacle_init();
	TIM1_Config();					//系统工作周期初始化 
	

	
}


//PID在此处修改
void pid_param_Init(void)//PID参数初始化
{
	pidRateX.kp = 3.f;
	pidRateY.kp = 3.f;
	pidRateZ.kp = 8.0f;
	
	pidRateX.ki = 0.05;
	pidRateY.ki = 0.05;
	pidRateZ.ki = 0.0;	
	
	pidRateX.kd = 0.2;
	pidRateY.kd = 0.2;
	pidRateZ.kd = 0.4f;	
	
	pidPitch.kp = 8.0f;
	pidRoll.kp = 8.0f;
	pidYaw.kp = 6.0f;	
	





		//高度串级PID
	//内环
	pidHeightRate.kp = 1.2f; //1.2f
	pidHeightRate.ki = 0.04f;
	pidHeightRate.kd = 0.085f;
	
//	pidHeightRate.IntegLimitHigh = 100;//有积分就要限幅
//	pidHeightRate.IntegLimitLow = -100;
	
		//外环PID参数
	pidHeightHigh.kp = 1.2f;//1.2f

	pidHeightHigh.kd = 0.085f;//0.085f
	



		//位置串级PID
	pidPosRateX.kp = 0.15f;
	pidPosRateY.kp = 0.15f;
	pidPosRateX.kd = 0.035f;
	pidPosRateY.kd = 0.035f;	
	pidPosRateX.ki = 0.04f;
	pidPosRateY.ki = 0.04f;

	pidPositionX.kp = 0.02f;
	pidPositionY.kp  = 0.02f;




}












