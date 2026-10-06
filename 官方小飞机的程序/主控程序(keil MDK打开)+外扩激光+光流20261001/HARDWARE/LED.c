/*******************************************************************
 *@title LED system
 *@brief flight light
 *@brief 
 *@time  2016.10.19
 *@editor–°ƒœ&zin
 *∑…øÿ∞Æ∫√QQ»∫551883670,” œ‰759421287@qq.com
 ******************************************************************/
#include "stm32f10x.h"
#include "LED.h"
#include "ALL_DATA.h"
#include "hand_link.h"
#include "remote.h"





////”“«∞µ∆			 
#define fLED1_H()  TIM3->CCR1=1000 //∞µ
#define fLED1_L()  TIM3->CCR1=0  //¡¡
#define fLED1_Toggle()  TIM3->CCR1^=(1000^0)//…¡À∏
////◊Û«∞µ∆			 
#define fLED2_H()  TIM3->CCR2=1000 //∞µ
#define fLED2_L()  TIM3->CCR2=0  //¡¡
#define fLED2_Toggle()  TIM3->CCR2^=(1000^0)//…¡À∏
////◊Û∫Ûµ∆			 
#define bLED3_H()  TIM3->CCR3=1000 //∞µ
#define bLED3_L()  TIM3->CCR3=0  //¡¡
#define bLED3_Toggle()  TIM3->CCR3^=(1000^0)//…¡À∏
//-------------------------------------------------
////◊Û∫Ûµ∆			 
#define bLED4_H()  TIM3->CCR4=1000 //∞µ
#define bLED4_L()  TIM3->CCR4=0  //¡¡
#define bLED4_Toggle()  TIM3->CCR4^=(1000^0)//…¡À∏
//-------------------------------------------------

//-------------------------------------------------
//---------------------------------------------------------
/*     you can select the LED statue on enum contains            */
sLED LED = {300,AllFlashLight};  //LED initial statue is off;
                             //default 300ms flash the status

/**************************************************************
 *  LED system
 * @param[in] 
 * @param[out] 
 * @return     
 ***************************************************************/	
void PilotLED(void)
{
 static uint32_t last_time;
 static uint16_t last_period=300;
 static bool lights_on;
 uint32_t now=hand_milliseconds;
 /* Only accepted, fresh packets from the current owner indicate connection. */
 bool connected=hand_link_owned()
  ? hand_link.active&&(uint32_t)(now-hand_link.last_ms)<=600
  : legacy_active&&(uint32_t)(now-legacy_last_ms)<=600;
 /* Locked/offline: slow blink; locked/connected: fast blink; armed: solid.
  * Height hold and optical flow keep the same visible armed indication. */
 uint16_t period=ALL_flag.unlock?0:(connected?100:300);
 LED.status=period?AllFlashLight:AlwaysOn;
 LED.FlashTime=period;
 if(period!=last_period) {
  last_period=period;last_time=now;lights_on=true;
 }else if(period&&(uint32_t)(now-last_time)>=period) {
  last_time=now;lights_on=!lights_on;
 }else return;
 if(lights_on) {
  fLED1_L();fLED2_L();bLED3_L();bLED4_L();
 }else {
  fLED1_H();fLED2_H();bLED3_H();bLED4_H();
 }
}

/**************************END OF FILE*********************************/



