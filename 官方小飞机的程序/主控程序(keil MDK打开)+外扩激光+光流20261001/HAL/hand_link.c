#include "hand_link.h"
#include "ALL_DEFINE.h"
#include "flow.h"
#include "obstacle.h"
#include "../../../shared/hand_protocol.h"
#include <math.h>

volatile HandLinkState hand_link;
volatile uint32_t hand_milliseconds;
volatile uint8_t hand_radio_regs[16];
volatile uint32_t hand_radio_tx_us;
static uint8_t reply[32];
static volatile bool reply_pending;
static bool radio_tx;
static uint32_t reply_at,tx_at,radio_check_at,last_tick,lift_since,tx_started_us;
static uint32_t telemetry_at;
static uint16_t input[8]={1500,1500,1500,1500,2000,1000,1000,1000};
static float ramp_collective;
static bool manual_landing;
extern uint16_t thr_hold;
extern int16_t roll_offset,pitch_offset;
extern uint16_t last_Remote_AUX1;
#define CE_LOW() (GPIOB->BRR=GPIO_Pin_8)
#define CE_HIGH() (GPIOB->BSRR=GPIO_Pin_8)

/* GCC needs the equivalent of the CMSIS intrinsic absent from old core_cm3.c. */
static uint32_t enter_critical(void) {
 uint32_t old;
 __asm volatile("mrs %0, primask\ncpsid i":"=r"(old)::"memory");return old;
}
static void leave_critical(uint32_t old) { __asm volatile("msr primask, %0"::"r"(old):"memory"); }
uint32_t hand_time_us(void) {
 uint32_t a,b,v;do {a=hand_milliseconds;v=SysTick->VAL;b=hand_milliseconds;}while(a!=b);
 return a*1000+(SysTick->LOAD-v)/(SystemCoreClock/1000000);
}
static uint32_t elapsed(uint32_t then) {return hand_milliseconds-then;}
static bool height_fresh(void) {return hand_link.height_frames&&hand_link.height_ok&&elapsed(hand_link.height_ms)<=100;}
static bool height_airborne(void) {return height_fresh()&&hand_link.height_mm>=50&&hand_link.height_mm<3500;}
static void event(unsigned reason) {if(reason&&!hand_link.first_event)hand_link.first_event=reason;}
static void neutral(void) {Remote.roll=Remote.pitch=Remote.yaw=1500;pidRoll.desired=pidPitch.desired=0;}
static void locked(unsigned reason) {
 if(reason) {event(reason);if(!hand_link.last_fault)hand_link.last_fault=reason;}
 hand_link.reason=hand_link.last_fault;
 ALL_flag.unlock=ALL_flag.height_lock=ALL_flag.flow_control=0;
 Remote.thr=1000;neutral();
 for(unsigned i=0;i<4;i++)motor_PWM_Value[i]=0;
 TIM2->CCR1=TIM2->CCR2=TIM2->CCR3=TIM2->CCR4=0;
 manual_landing=false;
 hand_link.stage=reason?HC_STAGE_FAULT:HC_STAGE_LOCKED;hand_link.collective=0;
 hand_link.recovery_ready=0;hand_link.recovery_since=0;
 LED.status=AllFlashLight;LED.FlashTime=300;
}
static uint16_t throttle_from_collective(float c) {
 /* Exact inverse of the official manual law, subject to its 1000..2000 range. */
 if(c<205)c=205;if(c>650)c=650;
 return (uint16_t)(1000+(c-200)/0.45f+0.5f);
}
static void manual(void) {
 event(HC_HEIGHT);hand_link.reason=HC_HEIGHT;
 ALL_flag.height_lock=ALL_flag.flow_control=0;
 ramp_collective=hand_link.collective;
 if(ramp_collective<205)ramp_collective=205;if(ramp_collective>650)ramp_collective=650;
 hand_link.stage=HC_STAGE_MANUAL;hand_link.neutral_seen=0;
 Remote.thr=throttle_from_collective(ramp_collective);
 LED.status=AlwaysOn;
}
static void assist_start(void) {
 Remote.thr=1500;neutral();
 ALL_flag.height_lock=1;ALL_flag.flow_control=(hand_link.assist&2)!=0;
 /* The original HeightPidControl initializes its own PID state and base=500. */
 hand_link.stage=HC_STAGE_HOLD;hand_link.reason=0;
}
void hand_link_imu(bool ok) {hand_link.imu_ok=ok;if(ok)hand_link.imu_ms=hand_milliseconds;}
void hand_link_height(bool ok,uint16_t mm) {
 hand_link.height_ok=ok&&mm<3500;
 if(ok) {hand_link.height_mm=mm;hand_link.height_ms=hand_milliseconds;++hand_link.height_frames;}
 if(height_fresh()&&(hand_link.stage==HC_STAGE_LOCKED||hand_link.stage==HC_STAGE_IDLE)) {
  hand_link.ground_mm=mm;hand_link.ground_set=1;
 }
 if(height_airborne()) {
  if(!hand_link.recovery_since)hand_link.recovery_since=hand_milliseconds;
  hand_link.recovery_ready=elapsed(hand_link.recovery_since)>=500;
 }else {hand_link.recovery_since=0;hand_link.recovery_ready=0;}
}
bool hand_link_owned(void) {return hand_link.owned!=0;}
bool hand_radio_transmitting(void) {return radio_tx;}
void hand_link_init(void) {Remote.thr=1000;neutral();for(unsigned i=4;i<8;i++)((&Remote.AUX1)[i-4])=1000;}

/* Called in the existing 3 ms scheduler before native attitude and motor control.
 * Main-loop/SPI stalls cannot keep an old control command alive. */
void hand_link_tick(void) {
 remote_timeout_tick();
 if(!hand_link.owned)return;
 uint32_t dt=elapsed(last_tick);last_tick=hand_milliseconds;if(dt>100)dt=0;
 if(hand_link.active&&elapsed(hand_link.last_ms)>600) {
  locked(ALL_flag.unlock?HC_TIMEOUT:0);
  hand_link.active=hand_link.owned=0;reply_pending=false;
  remote_reset_session();return;
 }
 if(!ALL_flag.unlock)return;
 if(elapsed(hand_link.imu_ms)>100) {locked(HC_IMU);return;}
 if(!isfinite(Angle.roll)||!isfinite(Angle.pitch)) {locked(HC_IMU);return;}
 if(hand_link.stage==HC_STAGE_IDLE) {Remote.thr=1000;neutral();return;}
 if(hand_link.stage==HC_STAGE_TAKEOFF) {
  /* Ramp from the native motor-start threshold to the cap in 2000 ms. */
  neutral();ramp_collective+=(500.0f-245.5f)*dt/2000.0f;
  if(ramp_collective>500)ramp_collective=500;
  Remote.thr=throttle_from_collective(ramp_collective);
  /* A ground reading of zero never blocks TAKEOFF or the motor ramp. */
  bool lifted=height_airborne()&&hand_link.height_mm>=hand_link.ground_mm+200;
  if(!lifted)lift_since=0;
  else if(!lift_since)lift_since=hand_milliseconds;
  else if(elapsed(lift_since)>=100) {assist_start();return;}
  if(elapsed(hand_link.stage_ms)>15000) {
   /* Missing range evidence must not cut a possibly airborne aircraft. */
   event(HC_TAKEOFF_TIMEOUT);manual();
  }
  return;
 }
 if((hand_link.stage==HC_STAGE_HOLD||hand_link.stage==HC_STAGE_LAND)&&!ALL_flag.height_lock&&!manual_landing) {
  bool landing=hand_link.stage==HC_STAGE_LAND;manual();
  if(landing) {manual_landing=true;hand_link.stage=HC_STAGE_LAND;}
 }
 if(hand_link.stage==HC_STAGE_HOLD)Remote.thr=input[2];
 else if(hand_link.stage==HC_STAGE_LAND) {
  neutral();
  if(manual_landing) {
   ramp_collective-=25.0f*dt/1000.0f;
   if(ramp_collective<=205) {locked(0);return;}
   Remote.thr=throttle_from_collective(ramp_collective);
  }else Remote.thr=1100;
  return;
 }
 else if(hand_link.stage==HC_STAGE_MANUAL) {
  if(input[2]>=1150&&input[2]<=1750)hand_link.neutral_seen=1;
  if(hand_link.neutral_seen) {
   if(input[2]>1750)ramp_collective+=25.0f*dt/1000.0f;
   else if(input[2]<1150)ramp_collective-=25.0f*dt/1000.0f;
  }
  if(ramp_collective<205)ramp_collective=205;if(ramp_collective>650)ramp_collective=650;
  Remote.thr=throttle_from_collective(ramp_collective);
 }
 /* Keep bounded attitude gestures; translate only flow/yaw switch thresholds. */
 Remote.roll=ALL_flag.flow_control?(input[0]<1500?1200:input[0]>1500?1800:1500):input[0];
 Remote.pitch=ALL_flag.flow_control?(input[1]<1500?1200:input[1]>1500?1800:1500):input[1];
 Remote.yaw=input[3]<1500?1100:input[3]>1500?1900:1500;
 if(!ALL_flag.flow_control) {
  pidRoll.desired=(1500-(int)Remote.roll)*0.04f;
  pidPitch.desired=(1500-(int)Remote.pitch)*0.04f;
 } else {
  /* The native flow branch leaves the OTHER axis at its old command when a
   * new axis is selected. Clear only the released gesture axis here. */
  if(Remote.roll==1500)pidPosRateX.desired=0;
  if(Remote.pitch==1500)pidPosRateY.desired=0;
 }
 if(Remote.yaw>1820)pidYaw.desired-=10.0f*dt/1000.0f;
 else if(Remote.yaw<1180)pidYaw.desired+=10.0f*dt/1000.0f;
}
void hand_link_motor_observe(void) {
 if(!hand_link.owned)return;
 if(!ALL_flag.unlock) {
  if(hand_link.stage==HC_STAGE_LAND)locked(0);
  else if(hand_link.stage!=HC_STAGE_LOCKED&&hand_link.stage!=HC_STAGE_FAULT)locked(HC_TILT);
  return;
 }
 if(hand_link.stage==HC_STAGE_IDLE) {hand_link.collective=0;return;}
 float c=ALL_flag.height_lock?thr_hold+pidHeightRate.out:200+0.45f*(Remote.thr-1000);
 if(!isfinite(c)) {locked(HC_IMU);return;}
 hand_link.collective=(uint32_t)(c<0?0:c>800?800:c);
}
static void make_reply(const hc_packet_t *p) {
 hc_packet_t s={.version=3,.command=HC_STATUS,.session=p->session,.sequence=p->sequence};
 s.channels[0]=(ALL_flag.unlock?1:0)|(ALL_flag.height_lock?2:0)|(ALL_flag.flow_control?16:0);
 s.channels[1]=hand_link.reason;s.channels[2]=Remote.thr;s.channels[3]=p->command;
 s.channels[4]=(uint16_t)hand_link.received;s.channels[7]=(uint16_t)hand_link.bad;
 s.state=hand_link.stage;
 if(hand_link.ground_set)s.state|=HC_STATE_GROUND_READY;
 if(hand_link.recovery_ready)s.state|=HC_STATE_RESUME_READY;
 if(height_fresh())s.state|=HC_STATE_HEIGHT_FRESH;
 s.height=height_fresh()&&hand_link.ground_set?(uint16_t)(int16_t)((int)hand_link.height_mm-(int)hand_link.ground_mm):HC_HEIGHT_UNKNOWN;
 if(hand_link.received%5==0) {
  s.command=HC_DETAIL;s.channels[5]=height_fresh()?hand_link.height_mm:0xffff;
  s.channels[6]=hand_link.collective;s.channels[7]=hand_link.radio_faults;
 }
 if(hand_link.received%20==0) {
  s.command=HC_DIAGNOSTICS;s.channels[5]=hand_link.rx_gap_ms>65535?65535:hand_link.rx_gap_ms;
  s.channels[6]=hand_link.rx_max_gap_ms>65535?65535:hand_link.rx_max_gap_ms;s.channels[7]=hand_link.first_event;
 }
 hc_encode(&s,reply);if(!reply_pending)reply_at=hand_milliseconds;reply_pending=true;
}
bool hand_link_receive(const uint8_t raw[32]) {
 if(raw[0]!='H'||raw[1]!='C')return hand_link_owned();
 hc_packet_t p;
 if(!hc_decode(raw,&p)||p.version!=3||p.command>HC_RESUME) {++hand_link.bad;return true;}
 for(unsigned i=0;i<8;i++)if(p.channels[i]<1000||p.channels[i]>2000) {++hand_link.bad;return true;}
 if(p.command!=HC_DISARM&&!(p.assist&1)) {++hand_link.bad;return true;}
 uint32_t key=enter_critical();
 bool fresh_session=!hand_link.active||p.session!=hand_link.session;
 if(fresh_session) {
  if(p.command!=HC_DISARM||ALL_flag.unlock)goto out;
  if(p.session!=hand_link.session) {
   hand_link.last_fault=hand_link.first_event=hand_link.reason=0;
   hand_link.rx_max_gap_ms=0;
  }
  remote_reset_session();
  hand_link.session=p.session;hand_link.lock_ms=hand_milliseconds;hand_link.arm_latched=0;
  telemetry_at=hand_milliseconds;reply_pending=false;
 } else if(!hc_newer(p.sequence,(uint16_t)hand_link.sequence))goto out;
 else {
  hand_link.rx_gap_ms=elapsed(hand_link.last_ms);
  if(hand_link.rx_gap_ms>hand_link.rx_max_gap_ms)hand_link.rx_max_gap_ms=hand_link.rx_gap_ms;
 }
 hand_link.owned=hand_link.active=1;hand_link.last_ms=hand_milliseconds;
 hand_link.sequence=p.sequence;++hand_link.received;hand_link.assist=p.assist;
 if(p.command==HC_DISARM) {
  if(hand_link.arm_latched)hand_link.lock_ms=hand_milliseconds;
  locked(0);hand_link.arm_latched=0;
 }else if(p.command==HC_ARM) {
  if(hand_link.arm_latched) {} /* Retries after a fault cannot re-arm. */
  else if(hand_link.last_fault)hand_link.reason=hand_link.last_fault;
  else if(p.channels[2]!=1000||p.channels[0]!=1500||p.channels[1]!=1500||p.channels[3]!=1500)hand_link.reason=HC_BAD_INPUT;
  else if(elapsed(hand_link.lock_ms)<500)hand_link.reason=HC_NEED_LOCK;
  else if(!hand_link.imu_ok||elapsed(hand_link.imu_ms)>100)hand_link.reason=HC_IMU;
  else if(!isfinite(Angle.roll)||!isfinite(Angle.pitch)||fabsf(Angle.roll)>15||fabsf(Angle.pitch)>15)hand_link.reason=HC_NOT_LEVEL;
  else {
   Remote.thr=1000;neutral();Remote.AUX1=last_Remote_AUX1=1000;
   roll_offset=pitch_offset=0;pidRoll.offset=pidPitch.offset=0;
   imu_rest();ALL_flag.height_lock=ALL_flag.flow_control=0;ALL_flag.unlock=1;
   hand_link.arm_latched=1;hand_link.stage=HC_STAGE_IDLE;hand_link.reason=0;LED.status=AlwaysOn;
  }
 }else if(!ALL_flag.unlock||!hand_link.arm_latched) {
  hand_link.reason=hand_link.last_fault?hand_link.last_fault:HC_NOT_READY;
 }else if(p.command==HC_TAKEOFF) {
  if(hand_link.stage==HC_STAGE_IDLE) {
   if(height_fresh()) {hand_link.ground_mm=hand_link.height_mm;hand_link.ground_set=1;}
   if(!hand_link.ground_set)hand_link.ground_mm=0;
   hand_link.stage=HC_STAGE_TAKEOFF;hand_link.stage_ms=hand_milliseconds;
   ramp_collective=245.5f;lift_since=0;last_tick=hand_milliseconds;
   for(unsigned i=0;i<4;i++)input[i]=1500;
   Remote.thr=1101;hand_link.reason=0;
  }
 }else if(p.command==HC_LAND) {
  if(hand_link.stage==HC_STAGE_IDLE)locked(0);
  else if(hand_link.stage==HC_STAGE_TAKEOFF||hand_link.stage==HC_STAGE_MANUAL) {
   manual();manual_landing=true;hand_link.stage=HC_STAGE_LAND;neutral();
  }else {hand_link.stage=HC_STAGE_LAND;neutral();Remote.thr=1100;}
 }else if(p.command==HC_RESUME) {
  bool center=p.channels[0]==1500&&p.channels[1]==1500&&p.channels[2]==1500&&p.channels[3]==1500;
  if(hand_link.stage==HC_STAGE_MANUAL&&hand_link.recovery_ready&&center)assist_start();
  else if(hand_link.stage!=HC_STAGE_HOLD)hand_link.reason=HC_NOT_READY;
 }else {
  for(unsigned i=0;i<8;i++)input[i]=p.channels[i];
  for(unsigned i=0;i<4;i++)if(i!=2) {if(input[i]<1300)input[i]=1300;if(input[i]>1700)input[i]=1700;}
  hand_link.reason=hand_link.stage==HC_STAGE_MANUAL?HC_HEIGHT:hand_link.last_fault;
 }
 make_reply(&p);
out:leave_critical(key);return true;
}
void hand_radio_init(void) {
 NRF24L01_Configuration();hand_link.radio_ready=0;radio_tx=false;
 for(unsigned i=0;i<3;i++) {
  if(!NRF24L01_Check()) {
   RX_Mode();NRF24L01_Write_Reg(SPI_WRITE_REG+STATUS,0x70);
   NRF24L01_Write_Reg(FLUSH_RX,0xff);NRF24L01_Write_Reg(FLUSH_TX,0xff);
   hand_link.radio_ready=1;break;
  }
 }
 radio_check_at=hand_milliseconds;
}
void hand_link_poll(void) {
 uint32_t now=hand_milliseconds;
 if(radio_tx) {
  uint8_t status=NRF24L01_Read_Reg(STATUS);
  if((status&0x30)||(now-tx_at)>=6) {
   hand_radio_tx_us=hand_time_us()-tx_started_us;
   CE_LOW();NRF24L01_Write_Reg(SPI_WRITE_REG+STATUS,0x30);
   if(!(status&TX_OK)) {NRF24L01_Write_Reg(FLUSH_TX,0xff);++hand_link.reply_failures;}
   else ++hand_link.replies;
   NRF24L01_Write_Reg(SPI_WRITE_REG+NCONFIG,0x0f);CE_HIGH();radio_tx=false;
  }
  return;
 }
 if(now-radio_check_at>=100) {
  radio_check_at=now;
  uint8_t cfg=NRF24L01_Read_Reg(NCONFIG),ch=NRF24L01_Read_Reg(RF_CH);
  if(cfg!=0x0f||ch!=1) {
   ++hand_link.radio_faults;hand_link.radio_ready=0;
   uint32_t key=enter_critical();if(hand_link.owned)locked(HC_RADIO);leave_critical(key);
   reply_pending=false;hand_radio_init();return;
  }
 }
 if(reply_pending&&now-reply_at>=20&&hand_link.radio_ready) {
  static uint8_t address[5]={0xe1,0xe2,0xe3,0xe4,0xe5};
  uint32_t key=enter_critical();
  reply_pending=false;
  if(!hand_link.active||!hand_link.owned||now-hand_link.last_ms>600) {leave_critical(key);return;}
  /* Reuse one normal response slot, so control/ack cadence is unchanged. */
  if((uint32_t)(now-telemetry_at)>=HC_TELEMETRY_INTERVAL_MS) {
   float pitch=Angle.pitch,roll=Angle.roll;
   bool valid=hand_link.imu_ok&&now-hand_link.imu_ms<=100&&
       isfinite(pitch)&&isfinite(roll)&&fabsf(pitch)<=180&&fabsf(roll)<=180;
   reply[3]=HC_TELEMETRY;reply[26]=valid?HC_ATTITUDE_VALID:0;
   hc_put16(reply+20,valid?(uint16_t)(int16_t)(pitch*100):HC_ATTITUDE_UNKNOWN);
   hc_put16(reply+22,valid?(uint16_t)(int16_t)(roll*100):HC_ATTITUDE_UNKNOWN);
   uint16_t distance=obstacle_distance_cm();
   hc_put16(reply+24,distance);
   if(distance!=HC_DISTANCE_UNKNOWN)reply[26]|=HC_DISTANCE_VALID;
   hc_put16(reply+30,hc_crc(reply,30));
   /* Keep the 300ms clock phase across 50ms-slot jitter; never catch up in bursts. */
   telemetry_at+=((uint32_t)(now-telemetry_at)/HC_TELEMETRY_INTERVAL_MS)*HC_TELEMETRY_INTERVAL_MS;
  }
  leave_critical(key);
  CE_LOW();NRF24L01_Write_Reg(SPI_WRITE_REG+STATUS,0x30);
  NRF24L01_Write_Reg(FLUSH_TX,0xff);
  NRF24L01_Write_Buf(SPI_WRITE_REG+TX_ADDR,address,5);
  NRF24L01_Write_Reg(SPI_WRITE_REG+NCONFIG,0x0e);
  NRF24L01_Read_Buf(TX_ADDR,(uint8_t *)hand_radio_regs,5);
  for(unsigned i=0;i<7;i++)hand_radio_regs[5+i]=NRF24L01_Read_Reg(i);
  hand_radio_regs[12]=NRF24L01_Read_Reg(0x1c);hand_radio_regs[13]=NRF24L01_Read_Reg(0x1d);
  NRF24L01_Write_Buf(WR_TX_PLOAD,reply,32);CE_HIGH();tx_started_us=hand_time_us();tx_at=now;radio_tx=true;
 }
}
