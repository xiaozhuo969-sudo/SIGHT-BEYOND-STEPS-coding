#include <assert.h>
#include <stdio.h>
#include "flight_keys.h"
#include "../../shared/hand_protocol.h"
static const uint16_t moving[8]={1700,1700,1800,1300,2000,1000,1000,1000};
static void setup(flight_keys_t *k,tx_control_t *t,unsigned stage) {
 *t=(tx_control_t){.v2=true,.v3=true,.phase=stage,.session=123,.heartbeat=1000};
 flight_keys_init(k,t,false,false,1000);k->local_owner=true;
}
static void packet(tx_control_t *t,uint32_t now,unsigned cmd,bool pose) {
 uint16_t ch[8];uint8_t raw[32];hc_packet_t p;
 assert(tx_step(t,now,pose,false,moving,ch));tx_encode(t,ch,raw);
 assert(hc_decode(raw,&p)&&p.command==cmd);
 assert(ch[0]==1500&&ch[1]==1500&&ch[3]==1500);
 assert(ch[2]==(cmd==HC_LAND?1500:1000));
}
int main(void) {
 flight_keys_t k;tx_control_t t;
 unsigned phases[]={TX_ALT_TAKEOFF,TX_ALT_LIVE,TX_ALT_MANUAL,TX_ALT_LOST,TX_ALT_RESUME};
 for(unsigned i=0;i<sizeof(phases)/sizeof(*phases);++i) {
  setup(&k,&t,phases[i]);flight_keys_edge(&k,&t,1,true,1050);
  assert(t.phase==TX_ALT_LAND&&!t.estop_active&&k.landing_active&&!k.pending);
  uint32_t since=t.since;
  /* A held button, stale hand IMU and moving gestures must not cut thrust. */
  for(uint32_t now=1100;now<4100;now+=50) {
   flight_keys_tick(&k,&t,now,true,false,false,false,false);
   flight_keys_edge(&k,&t,1,true,now);packet(&t,now,HC_LAND,false);
   assert(t.since==since&&k.status==KEY_LANDING);
  }
  flight_keys_edge(&k,&t,1,false,4100);packet(&t,4100,HC_LAND,true);
  flight_keys_edge(&k,&t,1,true,4150);packet(&t,4150,HC_LAND,true);
  assert(t.since==since);
  flight_keys_edge(&k,&t,1,false,4200);
  flight_keys_edge(&k,&t,0,true,4210);flight_keys_edge(&k,&t,0,false,4220);
  assert(!k.launch_requested&&!flight_keys_begin(&k,&t,4220,3,true,true,false));
 }
 /* Either press order invokes emergency stop on the second press. */
 for(unsigned order=0;order<2;++order) {
  setup(&k,&t,TX_ALT_LIVE);
  if(order==0)flight_keys_palm(&k,&t,true,1050);
  flight_keys_edge(&k,&t,1,true,1100);
  if(order==1)flight_keys_palm(&k,&t,true,1150);
  uint32_t stopped=t.estop_ms;
  assert(t.estop_active&&t.phase==TX_READY&&!k.landing_active);
  packet(&t,1150,HC_DISARM,false);
  for(uint32_t now=1200;now<2000;now+=50) {
   flight_keys_palm(&k,&t,true,now);flight_keys_edge(&k,&t,1,true,now);
   flight_keys_tick(&k,&t,now,true,false,false,false,true);
   assert(t.estop_ms==stopped);packet(&t,now,HC_DISARM,false);
  }
 }
 /* Landing acknowledgement retains communication and starts the10s gate. */
 setup(&k,&t,TX_ALT_LIVE);flight_keys_edge(&k,&t,1,true,1050);
 flight_keys_edge(&k,&t,1,false,1100);
 hc_packet_t p={.version=3,.command=HC_STATUS,.session=123,.sequence=1,
 .state=HC_STAGE_LOCKED,.channels={0,0,1000,HC_LAND,1,0,0,0}};
 t.sequence=1;uint8_t b[32];hc_encode(&p,b);tx_feedback(&t,b,1200);
 assert(t.estop_active&&t.phase==TX_READY&&tx_estop_remaining(&t,1200)==10000);
 flight_keys_autoconnect(&k,&t,1250,234,true,true);
 assert(!k.landing_active&&!k.launch_requested&&!k.pending);packet(&t,1250,HC_DISARM,true);
 assert(!tx_can_arm(&t,11199));
 /* GPIO9 alone keeps the established flight phase; no LAND or DISARM. */
 setup(&k,&t,TX_ALT_LIVE);flight_keys_palm(&k,&t,true,1050);
 assert(t.phase==TX_ALT_LIVE&&!t.estop_active);
 /* The independent control-task heartbeat still detects a stalled task. */
 setup(&k,&t,TX_ALT_LIVE);flight_keys_edge(&k,&t,1,true,1050);
 packet(&t,1801,HC_DISARM,true);assert(t.reason==TX_REASON_HEARTBEAT);
 puts("PASS: GPIO11 latched landing, both GPIO9+11 press orders, held keys, hand-IMU independence,10s lock communication and no queued launch");
}
