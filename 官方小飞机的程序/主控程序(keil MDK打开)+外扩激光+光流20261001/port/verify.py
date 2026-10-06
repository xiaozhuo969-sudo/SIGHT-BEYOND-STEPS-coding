#!/usr/bin/env python3
"""Execute the actual Thumb images offline; never access SWD, USB or motors."""
from pathlib import Path
import sys,struct,json,hashlib,random
sys.path.insert(0,str(Path.home()/'.cache/codex-tools/firmware-python'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_CODE
from unicorn.arm_const import *
from elftools.elf.elffile import ELFFile
ROOT=Path(__file__).resolve().parents[1]
FIELDS='owned active session sequence received bad last_ms lock_ms reason first_event last_fault rx_gap_ms rx_max_gap_ms imu_ms imu_ok height_ms height_frames height_mm height_ok stage arm_latched stage_ms ground_mm ground_set assist recovery_since recovery_ready neutral_seen collective radio_faults radio_ready replies reply_failures control_us last_control_us max_control_us overruns'.split()
def fb(x):return struct.unpack('<I',struct.pack('<f',x))[0]
class Machine:
 def __init__(self,original=False):
  self.original=original;self.u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
  for a,n in [(0x08000000,0x20000),(0x20000000,0x10000),(0x40000000,0x30000),(0xe0000000,0x100000)]:self.u.mem_map(a,n)
  path=ROOT/('Output/mini.axf' if original else 'Output/gcc/official_hand.elf')
  with path.open('rb') as f:
   e=ELFFile(f);self.sym={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols() if s['st_value']}
   for s in e.iter_segments():
    if s['p_type']=='PT_LOAD':self.u.mem_write(s['p_vaddr'],s.data())
  self.booting=False;self.radio=None;self.radio_sent=[];self.u.hook_add(UC_HOOK_CODE,self.hook)
  if original:
   self.booting=True;self.call('Reset_Handler');self.booting=False
  self.call('pid_param_Init')
  if not original:self.call('hand_link_init')
 def hook(self,u,a,n,d):
  if a==0x2000ff00 or (self.booting and a==(self.sym['main']&~1)):u.emu_stop()
  elif a==(self.sym['SystemInit']&~1):u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
  elif not self.original and a==(self.sym['NRF24L01_RxPacket']&~1):
   if self.radio is not None:u.mem_write(u.reg_read(UC_ARM_REG_R0),self.radio)
   u.reg_write(UC_ARM_REG_R0,1 if self.radio is None else 0);self.radio=None
   u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
  elif not self.original and any(a==(self.sym[name]&~1) for name in ('NRF24L01_Read_Reg','NRF24L01_Write_Reg','NRF24L01_Read_Buf','NRF24L01_Write_Buf')):
   reg=u.reg_read(UC_ARM_REG_R0);ptr=u.reg_read(UC_ARM_REG_R1);length=u.reg_read(UC_ARM_REG_R2)
   value=0
   if a==(self.sym['NRF24L01_Read_Reg']&~1):value={0:15,5:1,7:32}.get(reg,0)
   elif a==(self.sym['NRF24L01_Read_Buf']&~1):u.mem_write(ptr,bytes(length))
   elif a==(self.sym['NRF24L01_Write_Buf']&~1) and reg==0xa0:
    self.radio_sent.append((self.get('hand_milliseconds','I')[0],bytes(u.mem_read(ptr,length))))
   u.reg_write(UC_ARM_REG_R0,value);u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
 def call(self,name,*args):
  self.u.reg_write(UC_ARM_REG_SP,0x20004f00);self.u.reg_write(UC_ARM_REG_LR,0x2000ff01)
  for reg,val in zip((UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3),args):self.u.reg_write(reg,val)
  self.u.emu_start(self.sym[name]|1,0,count=1000000)
  pc=self.u.reg_read(UC_ARM_REG_PC)
  assert pc==0x2000ff00 or (self.booting and pc==(self.sym['main']&~1)),(name,hex(pc))
  return self.u.reg_read(UC_ARM_REG_R0)
 def put(self,n,fmt,*v,offset=0):self.u.mem_write(self.sym[n]+offset,struct.pack('<'+fmt,*v))
 def get(self,n,fmt,offset=0):return struct.unpack('<'+fmt,self.u.mem_read(self.sym[n]+offset,struct.calcsize('<'+fmt)))
 def h(self,n,v=None):
  off=FIELDS.index(n)*4
  if v is not None:self.put('hand_link','I',v,offset=off)
  return self.get('hand_link','I',offset=off)[0]
 def now(self,n):self.put('hand_milliseconds','I',n)
 def rc(self,*vals):self.put('Remote','8H',*(list(vals)+[1000]*(8-len(vals))))
 def tick(self,now,mm=None):
  self.now(now);self.h('last_ms',now);self.call('hand_link_imu',1)
  if mm is not None:
   self.call('hand_link_height',1,mm)
   if mm:self.put('VL53L01_high','I',mm)
  self.call('hand_link_tick');self.call('FlightPidControl',fb(.003));self.call('MotorControl');self.call('hand_link_motor_observe')
  if now%6==0:self.call('HeightPidControl',fb(.006))
 def send(self,cmd,seq,session=123,channels=None,assist=3,damage=False):
  c=channels or [1500,1500,1000 if cmd in (1,2) else 1500,1500,2000,1000,1000,1000]
  b=bytearray(b'HC\x03'+bytes([cmd])+struct.pack('>IH8H',session,seq,*c)+bytes([assist,0,0,0]))
  crc=0xffff
  for x in b:
   crc^=x<<8
   for _ in range(8):crc=((crc<<1)^(0x1021 if crc&0x8000 else 0))&65535
  b+=struct.pack('>H',crc)
  if damage:b[10]^=1
  self.u.mem_write(0x2000e000,bytes(b));self.call('hand_link_receive',0x2000e000)
 def legacy(self,thr=1000,roll=1500,pitch=1500,yaw=1500,aux=1000,damage=False):
  b=bytearray([0xaa,0xaf,3,27])+bytearray(struct.pack('>8H',roll,pitch,thr,yaw,aux,1000,1000,1000))+bytearray(11)
  b.append(sum(b)&255)
  if damage:b[-1]^=1
  self.radio=bytes(b);self.call('RC_Analy')
 def pwm(self):return struct.unpack('<4I',self.u.mem_read(0x40000034,16))
 def leds(self):return struct.unpack('<4I',self.u.mem_read(0x40000434,16))
checks=[]
def check(name,cond):
 assert cond,name
 checks.append(name)
def armed():
 m=Machine();m.now(1000);m.call('hand_link_imu',1);m.call('hand_link_height',1,0);m.send(2,1)
 m.now(1600);m.call('hand_link_imu',1);m.send(1,2)
 check('arm accepted without height dwell',m.h('stage')==1 and m.get('ALL_flag','B')[0]&1)
 return m
# Supplied official AXF versus recompiled official motor law and mixer.
rng=random.Random(20261003)
for idx in range(80):
 a,b=Machine(True),Machine();flags=rng.choice([1,3,19]);thr=rng.choice([1101,1200,1500,1667,1800,2000]);outs=[rng.uniform(-200,200) for _ in range(3)];height=rng.uniform(-200,200)
 for m in (a,b):
  m.put('ALL_flag','B',0);m.call('MotorControl');m.put('ALL_flag','B',flags);m.rc(1500,1500,thr,1500)
  m.call('MotorControl');m.call('MotorControl');m.put('thr_hold','H',500)
  for p,v in zip(['pidRateX','pidRateY','pidRateZ'],outs):m.put(p,'f',v,offset=40)
  m.put('pidHeightRate','f',height,offset=40);m.call('MotorControl')
 check('official motor differential '+str(idx),a.pwm()==b.pwm())
# Exact native PID arithmetic across both compilers (within float precision).
for idx in range(40):
 a,b=Machine(True),Machine();values=[rng.uniform(-5,5) for _ in range(13)];values[7:9]=[100,-100];values[11:]=[800,-800]
 for m in (a,b):m.put('pidRateX','13f',*values);m.call('pidUpdate',m.sym['pidRateX'],fb(.003))
 av=a.get('pidRateX','13f');bv=b.get('pidRateX','13f')
 check('official PID differential '+str(idx),all(abs(x-y)<1e-4 for x,y in zip(av,bv)))
m=Machine();m.now(1000);m.send(1,1);check('new session cannot arm',m.h('active')==0)
m.send(2,2,damage=True);check('CRC damaged rejected',m.h('active')==0)
m.send(2,2);m.send(0,3,channels=[999,1500,1500,1500,2000,1000,1000,1000]);check('bad channels rejected',m.h('sequence')==2)
m.send(0,2);check('replay rejected',m.h('received')==1)
m=armed()
for t in range(1602,2600,3):m.tick(t,0)
check('armed idle outputs all zero',m.pwm()==(0,0,0,0) and m.h('collective')==0)
m.send(3,3);check('zero ground starts takeoff',m.h('stage')==2)
for t in range(2601,3600,3):m.tick(t,0)
check('takeoff reaches halfway thrust progressively near 1000ms',371<=m.h('collective')<=373 and m.h('stage')==2)
for t in range(3600,4400,3):m.tick(t,0)
check('ground zero does not stall faster takeoff ramp',473<=m.h('collective')<=475 and m.h('stage')==2)
m.send(3,4);check('takeoff retry does not reset ramp',473<=m.h('collective')<=475 and m.get('Remote','8H')[2]>1600)
for t in range(4401,4597,3):m.tick(t,0)
check('takeoff remains below cap immediately before 2000ms',498<=m.h('collective')<500)
m.tick(4599,0)
check('takeoff reaches capped thrust at the 2000ms scheduler boundary',m.h('collective')==500 and m.get('Remote','8H')[2]==1667 and m.h('stage')==2)
for t in range(4602,5202,3):m.tick(t,0)
check('takeoff holds cap while waiting for liftoff evidence',m.h('collective')==500 and m.get('Remote','8H')[2]==1667)
m.send(2,5);check('estop immediately clears PWM',m.pwm()==(0,0,0,0) and m.h('stage')==0)
m=armed();m.call('hand_link_height',1,50);m.send(3,3)
for t in range(1602,1902,3):m.tick(t,249)
check('restored takeoff hold needs 200mm relative to the captured ground',m.h('stage')==2 and m.get('ALL_flag','B')[0]&2==0 and m.h('ground_mm')==50)
for t in range(1902,2004,3):m.tick(t,250)
check('200mm height still requires the existing 100ms confirmation',m.h('stage')==2)
m.tick(2004,250)
check('200mm relative height engages closed-loop takeoff after confirmation',m.h('stage')==2 and m.get('ALL_flag','B')[0]&2 and m.get('Remote','8H')[2]==1500)
for t in range(2007,2043,3):m.tick(t,250)
check('auto target starts near measured height instead of channel 1500 or 1600',abs(m.get('pidHeightHigh','f')[0]-250)<1 and m.get('Remote','8H')[2]==1500)
m=armed();m.send(3,3)
for t in range(1602,2400,3):m.tick(t,300)
check('native height engaged during automatic ascent',m.h('stage')==2 and m.get('ALL_flag','B')[0]&2)
# Explicit post-arrival fixture for the existing gesture/fallback unit checks.
# The complete measured 1m arrival sequence is exercised in verify_takeoff_1m.py.
m.h('stage',3);m.put('takeoff_closed_loop','B',0);m.put('set_high','B',0)
m.send(0,4,channels=[1300,1500,1500,1500,2000,1000,1000,1000]);m.tick(2400,300)
check('left gesture crosses native flow threshold',m.get('Remote','8H')[0]==1200)
m.send(0,5,channels=[1500,1700,1500,1500,2000,1000,1000,1000]);m.tick(2403,300)
check('axis switch clears old flow direction',m.get('Remote','8H')[:2]==(1500,1800) and m.get('pidPosRateX','f')[0]==0)
# Match the actual current collective, not the neutral virtual channel (1500).
m.h('collective',520);m.put('ALL_flag','B',1);m.tick(2406,0)
check('official height exit transfers to held manual throttle',m.h('stage')==7 and abs(m.h('collective')-520)<=1)
m.send(0,6,channels=[1500,1500,1800,1500,2000,1000,1000,1000]);before=m.get('Remote','8H')[2];before_collective=m.h('collective')
for t in range(2409,2700,3):m.tick(t,0)
check('manual climb keeps the slower adjustment rate',6<=m.h('collective')-before_collective<=8)
# Neutral at entry may have been seen on the previous tick; reset simulates active up on loss.
m.h('neutral_seen',0)
for t in range(2700,3000,3):m.tick(t,0)
check('manual fallback waits for neutral before more climb',m.get('Remote','8H')[2]>=before and m.h('neutral_seen')==0)
m.send(4,7);before_collective=m.h('collective')
for t in range(3000,3999,3):m.tick(t,0)
check('manual landing keeps the slower descent rate',24<=before_collective-m.h('collective')<=26 and m.h('stage')==4)
for t in range(3999,19000,3):
 m.tick(t,0)
 if not(m.get('ALL_flag','B')[0]&1):break
check('manual landing reduces thrust and locks',m.h('stage')==0 and m.pwm()==(0,0,0,0))
m=armed();m.now(2201);m.call('hand_link_tick');check('600ms uplink watchdog locks',m.h('last_fault')==4 and m.pwm()==(0,0,0,0))
m.send(1,3);check('arm retry after timeout cannot arm',m.get('ALL_flag','B')[0]&1==0)
m=armed();m.now(1800);m.h('last_ms',1800);m.call('hand_link_tick');check('stale IMU locks',m.h('last_fault')==6)
m=armed();m.send(3,3)
for t in range(1602,16800,3):
 m.tick(t,0)
 if m.h('stage')==7:break
check('missing liftoff evidence holds manual thrust instead of cutting motors',m.h('stage')==7 and m.h('first_event')==11 and m.h('last_fault')==0 and m.h('collective')>=499 and (m.get('ALL_flag','B')[0]&1))
m=Machine();m.put('ALL_flag','B',0);m.put('LED','H',300)
m.u.mem_write(0x40000434,struct.pack('<4I',1000,1000,1000,1000))
m.now(300);m.call('PilotLED')
check('locked LED fully on at 300ms',struct.unpack('<4I',m.u.mem_read(0x40000434,16))==(0,0,0,0))
m.now(599);m.call('PilotLED')
check('locked LED period uses real milliseconds',struct.unpack('<4I',m.u.mem_read(0x40000434,16))==(0,0,0,0))
m.now(600);m.call('PilotLED')
check('locked LED off at 600ms',struct.unpack('<4I',m.u.mem_read(0x40000434,16))==(1000,1000,1000,1000))
# LED state follows the accepted owner and real link timeout, with no arm writes.
for source in ('HC3','native'):
 m=Machine();m.now(300);m.call('PilotLED');m.now(600);m.call('PilotLED')
 m.now(601)
 if source=='HC3':m.send(2,1,damage=True)
 else:m.legacy(damage=True)
 m.call('PilotLED')
 check(source+' bad packet cannot trigger connected LEDs',m.get('LED','H')[0]==300 and m.leds()==(1000,1000,1000,1000))
 if source=='HC3':m.send(2,1)
 else:m.legacy()
 m.call('PilotLED')
 check(source+' accepted connection starts synchronized fast blink',m.get('LED','H')[0]==100 and m.leds()==(0,0,0,0))
 m.now(700);m.call('PilotLED')
 check(source+' fast blink keeps the full on interval',m.leds()==(0,0,0,0))
 m.now(701);m.call('PilotLED')
 check(source+' fast blink turns all LEDs off at 100ms',m.leds()==(1000,1000,1000,1000))
 m.now(801);m.call('PilotLED')
 check(source+' fast blink turns all LEDs on at 200ms',m.leds()==(0,0,0,0))
 m.now(1201);m.call('PilotLED')
 check(source+' connected LED lasts through 600ms link boundary',m.get('LED','H')[0]==100)
 m.now(1202);m.call('hand_link_tick');m.call('PilotLED')
 check(source+' expired link returns to slow blink',m.get('LED','H')[0]==300 and m.get('ALL_flag','B')[0]&1==0 and m.pwm()==(0,0,0,0))
 m.now(1501);m.call('PilotLED');on=m.leds();m.now(1502);m.call('PilotLED')
 check(source+' disconnected LEDs use a 300ms interval',on==(0,0,0,0) and m.leds()==(1000,1000,1000,1000))
m=armed();m.now(1600);m.call('PilotLED')
check('successful arm immediately lights all LEDs with motors stopped',m.leds()==(0,0,0,0) and m.pwm()==(0,0,0,0))
for flags in (3,19):
 m.put('ALL_flag','B',flags);m.put('LED','B',4,offset=2);m.now(1601);m.call('PilotLED')
 check('armed assist mode '+str(flags)+' retains four solid LEDs',m.leds()==(0,0,0,0) and m.get('LED','B',offset=2)[0]==0)
m.send(2,3);m.call('PilotLED');m.now(1701);m.call('PilotLED')
check('explicit lock resumes fast blink while link is alive',m.get('ALL_flag','B')[0]&1==0 and m.get('LED','H')[0]==100 and m.leds()==(1000,1000,1000,1000) and m.pwm()==(0,0,0,0))
# Original remote through the real AA parser, unlock FSM and source handover.
m=Machine();m.now(1000);m.send(2,1);m.legacy(1000)
check('HC3 active owner excludes legacy packets',m.get('legacy_received','I')[0]==0)
m.now(1600);m.call('hand_link_tick');check('ownership retained until lease ends',m.h('owned')==1)
m.now(1601);m.call('hand_link_tick')
check('stopped HC3 releases ownership without power cycling',m.h('owned')==0 and m.h('active')==0 and m.h('last_fault')==0)
m.legacy(1900);m.legacy(1000)
check('handover cannot reuse an old high throttle unlock step',m.get('ALL_flag','B')[0]&1==0)
for _ in range(6):m.legacy(1900)
m.legacy(1000);m.legacy(1000)
check('original low high low unlock works after HC3 stop',m.get('ALL_flag','B')[0]&1==1)
m.call('PilotLED');check('native unlock immediately lights all four LEDs',m.leds()==(0,0,0,0))
m.legacy(1500,roll=1120,pitch=1800,yaw=1900,aux=2000)
check('native remote channels and AUX1 preserved',m.get('Remote','8H')[:5]==(1120,1800,1500,1900,2000) and m.get('ALL_flag','B')[0]&18==18)
m.send(2,2,session=999)
check('hand connect cannot take over an armed native remote',m.h('owned')==0 and m.get('ALL_flag','B')[0]&1==1)
m.now(1700)
for _ in range(510):m.call('RC_Analy')
check('loop speed no longer causes false native timeout',m.get('ALL_flag','B')[0]&1==1)
last=m.get('legacy_last_ms','I')[0];m.now(last+600);m.call('hand_link_tick')
check('native 600ms boundary remains valid',m.get('ALL_flag','B')[0]&1==1)
m.now(last+601);m.call('hand_link_tick')
check('native actual signal loss locks PWM and clears FSM',m.get('ALL_flag','B')[0]==0 and m.pwm()==(0,0,0,0) and m.get('legacy_active','I')[0]==0)
for _ in range(8):m.legacy(1900)
check('native reconnection at high throttle cannot unlock',m.get('ALL_flag','B')[0]&1==0)
count=m.get('legacy_received','I')[0];m.now(last+700);m.legacy(damage=True)
check('corrupt legacy packet does not refresh timer',m.get('legacy_received','I')[0]==count and m.get('legacy_last_ms','I')[0]==last+601)
# Fresh explicit hand connection is allowed while the native remote is locked.
m.send(2,3,session=999)
check('locked native remote can hand over to explicit HC3 connection',m.h('owned')==1 and m.get('legacy_active','I')[0]==0)
# Observe actual packet writes from the compiled radio scheduler; no hardware.
def poll_reply(m,t):
 m.now(t);m.call('hand_link_poll');m.now((t+1)&0xffffffff);m.call('hand_link_poll')
def crc16(raw):
 crc=0xffff
 for x in raw:
  crc^=x<<8
  for _ in range(8):crc=((crc<<1)^(0x1021 if crc&0x8000 else 0))&65535
 return crc
m=Machine();m.h('radio_ready',1)
for t in range(0,1001,50):poll_reply(m,t)
check('unconnected aircraft emits no telemetry',not m.radio_sent)
m.now(1100);m.legacy();poll_reply(m,1120)
check('native remote does not start HC telemetry',not m.radio_sent)
# Angle structure is roll, pitch, yaw; send negative pitch and positive roll.
for index,t in enumerate(range(2000,4101,50),1):
 m.now(t);m.call('hand_link_imu',1);m.put('Angle','3f',56.78,-12.34,0);m.send(2,index)
 poll_reply(m,t+20)
frames=m.radio_sent;tele=[(t,b) for t,b in frames if b[3]==0x83]
check('one reply per 50ms uplink remains',len(frames)==43 and all(b[0]-a[0]==50 for a,b in zip(frames,frames[1:])))
check('telemetry sent every 300ms in existing reply slot',len(tele)==7 and tele[0][0]==2320 and all(b[0]-a[0]==300 for a,b in zip(tele,tele[1:])))
check('telemetry signed centidegrees match aircraft Angle',all(-1235<=struct.unpack('>h',b[20:22])[0]<=-1233 and 5677<=struct.unpack('>h',b[22:24])[0]<=5679 for _,b in tele))
check('placeholder range invalid and CRC correct',all(b[24:27]==b'\xff\xff\x01' and int.from_bytes(b[30:32],'big')==crc16(b[:30]) for _,b in tele))
check('common reply fields retain locked DISARM confirmation',all(b[10:12]==b'\0\0' and b[16:18]==b'\0\2' for _,b in frames if b[3]<0x84))
check('ordinary status no longer publishes unvalidated angles',all(b[20:24]==bytes(4) for _,b in frames if b[3]==0x80))
for idx,(pitch,roll,imu_ok,imu_age) in enumerate(((float('nan'),0,1,0),(0,float('inf'),1,0),(181,0,1,0),(0,0,0,0),(0,0,1,101))):
 t=4400+idx*350;m.now(t);m.h('imu_ok',imu_ok);m.h('imu_ms',t+20-imu_age);m.put('Angle','3f',roll,pitch,0);m.send(2,100+idx);poll_reply(m,t+20)
 raw=m.radio_sent[-1][1]
 check('invalid or stale attitude sent as unavailable '+str(idx),raw[3]==0x83 and raw[20:27]==b'\x80\0\x80\0\xff\xff\0')
count=len(m.radio_sent);m.now(7000);m.call('hand_link_tick');poll_reply(m,7020)
check('expired session stops telemetry',len(m.radio_sent)==count and not m.h('active'))
m.now(7100);m.send(2,1,session=999);poll_reply(m,7120)
check('new connection starts telemetry timer anew',m.radio_sent[-1][1][3]!=0x83)
m=Machine();m.h('radio_ready',1);start=0xffffff00
for idx,delta in enumerate(range(0,701,50),1):
 t=(start+delta)&0xffffffff;m.now(t);m.call('hand_link_imu',1);m.send(2,idx);poll_reply(m,(t+20)&0xffffffff)
tele=[(t,b) for t,b in m.radio_sent if b[3]==0x83]
check('telemetry scheduler tolerates millisecond wrap',len(tele)==2 and ((tele[1][0]-tele[0][0])&0xffffffff)==300)
m=Machine();m.h('radio_ready',1)
for idx,t in enumerate(range(1000,6501,55),1):
 m.now(t);m.call('hand_link_imu',1);m.send(2,idx);poll_reply(m,t+20)
tele=[t for t,b in m.radio_sent if b[3]==0x83]
check('55ms control slots do not drift telemetry to 330ms',len(tele)==18 and abs((tele[-1]-tele[0])/(len(tele)-1)-300)<3)
check('slot jitter never causes telemetry catch-up burst',all(250<=b-a<=350 for a,b in zip(tele,tele[1:])))
# UART3 obstacle parser, DMA wrap, freshness and actual HC3 telemetry payload.
def obstacle_frame(cm):
 b=bytes.fromhex('5a5a4502')+struct.pack('>H',cm)
 return b+bytes([sum(b)&255])
def obstacle_feed(m,b):
 for x in b:m.call('obstacle_receive_byte',x)
m=Machine();m.now(1000)
for cm in (20,90,196,720):
 obstacle_feed(m,obstacle_frame(cm))
 check('obstacle valid cm '+str(cm),m.call('obstacle_distance_cm')==cm)
count=m.get('obstacle','7I')[1]
b=bytearray(obstacle_frame(100));b[-1]^=1;obstacle_feed(m,b)
check('obstacle checksum damage rejected',m.get('obstacle','7I')[1]==count and m.get('obstacle','7I')[2]==1)
obstacle_feed(m,b'\x00\x5a'+obstacle_frame(90)[:5]+obstacle_frame(196))
check('obstacle noise and truncated frame resynchronize',m.call('obstacle_distance_cm')==196)
for cm in (0,19,721,65535):
 obstacle_feed(m,obstacle_frame(cm))
 check('obstacle out of range '+str(cm),m.call('obstacle_distance_cm')==65535)
obstacle_feed(m,obstacle_frame(196));m.now(1600)
check('obstacle fresh at 600ms boundary',m.call('obstacle_distance_cm')==196)
m.now(1601);check('obstacle stale is unavailable',m.call('obstacle_distance_cm')==65535)
m.now(0xfffffff0);obstacle_feed(m,obstacle_frame(196));m.now(0x100)
check('obstacle freshness tolerates clock wrap',m.call('obstacle_distance_cm')==196)
m=Machine();m.now(1000)
data=b'\x00'*124+obstacle_frame(90)
m.u.mem_write(m.sym['rx_dma'],data[:128]);m.u.mem_write(0x40020034,struct.pack('<I',1))
m.call('obstacle_poll')
m.u.mem_write(m.sym['rx_dma'],data[128:]);m.u.mem_write(0x40020034,struct.pack('<I',125))
m.call('obstacle_poll')
check('obstacle DMA frame crosses circular buffer boundary',m.call('obstacle_distance_cm')==90)
m.u.mem_write(0x40004800,struct.pack('<I',8));m.call('obstacle_poll')
check('obstacle UART overrun invalidates reading',m.call('obstacle_distance_cm')==65535 and m.get('obstacle','7I')[3]==1)
m=Machine();m.h('radio_ready',1);m.now(1000);m.send(2,1);poll_reply(m,1020)
m.now(1300);obstacle_feed(m,obstacle_frame(196));m.send(2,2);poll_reply(m,1320)
raw=m.radio_sent[-1][1]
check('obstacle transmitted in real HC3 telemetry with valid bit and CRC',raw[3]==0x83 and raw[24:26]==b'\x00\xc4' and raw[26]&2 and int.from_bytes(raw[30:32],'big')==crc16(raw[:30]))
m.now(1950);m.send(2,3);poll_reply(m,1970);raw=m.radio_sent[-1][1]
check('stale obstacle clears HC3 distance valid bit',raw[3]==0x83 and raw[24:26]==b'\xff\xff' and raw[26]&2==0)
result={'passed':len(checks),'failed':0,'hardware_access':False,'tests':checks,'firmware_sha256':hashlib.sha256((ROOT/'Output/gcc/official_hand.template.bin').read_bytes()).hexdigest()}
(ROOT/'Output/gcc/verification.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k!='tests'}))
