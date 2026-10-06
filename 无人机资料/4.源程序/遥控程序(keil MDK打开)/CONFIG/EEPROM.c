#include "EEPROM.h"
#include "delay.h"
#include "i2c.h"


#define AT24C02_ADDR 0xA0  //AT24c02  IIC地址


#undef SUCCESS
#define SUCCESS 0
#undef FAILED
#define FAILED  1
#define START_ADDR 0  //256字节 //起始地址
#define  STOP_ADDR 0XFF //结束地址
 






uint16_t read_AT24C02(uint8_t addr)
{
	uint16_t data;
////	delay_ms(100);
////	flag = IIC_Read_One_Byte(AT24C02_ADDR,addr); //读取是否已经校准过数据的标志位
////	if(flag == 0x55) //如果该组数据已经校准过了，则读取数据，否则不读
//	{
//		u8 data_L;
//		u8 data_H;
//		delay_ms(100);
//		data_L = IIC_Read_One_Byte(AT24C02_ADDR,addr);
//		delay_ms(100);
//		data_H = IIC_Read_One_Byte(AT24C02_ADDR,addr+2);
//		return (data_L | (data_H<<8));//返回存在EEPROM的校准值
//	}
  IIC_read_Bytes(AT24C02_ADDR,addr,(uint8_t*)&data,2);
	return data; //返回已当前组数据是否曾经被写入的标志位供使用者思考是否再次写入
}


void write_AT24C02(uint8_t addr,uint16_t data)
{
//	u8 data_L;
//	u8 data_H;
//	delay_ms(100);
//	IIC_Write_One_Byte(AT24C02_ADDR,addr,0x55); //表示已经曾经有校准过，则可以读写校准值
//	
//	data_L = data;
//	data_H = data>>8;
//	delay_ms(100);
//	IIC_Write_One_Byte(AT24C02_ADDR,addr,data_L);
	delay_ms(10);
//	IIC_Write_One_Byte(AT24C02_ADDR,addr+2,data_H);
	IIC_Write_Bytes(AT24C02_ADDR,addr,(uint8_t*)&data,2);
	
}




uint8_t test_AT24C02(void)//测试，在地址0x55的内存地址写入0xAA后读回出来，如果出来的与写入的相同则测试成功
{
	uint8_t test = 0;
  IIC_Write_One_Byte(AT24C02_ADDR,0x55,0xAA);
	delay_ms(1000);
	test = IIC_Read_One_Byte(AT24C02_ADDR,0x55);
	if(test != 0xAA) //存储器异常
		return FAILED;
	else
		return SUCCESS;//返回成功
}
	













