"""Actual ARM ELF landing regression, synthetic inputs; never accesses hardware."""
from pathlib import Path
import json,hashlib
src=Path(__file__).with_name('verify_takeoff_650.py')
ns={'__file__':str(src),'__name__':'landing_offline'}
exec(compile(src.read_text(encoding='utf-8').split('Machine = ScheduledMachine')[0],str(src),'exec'),ns)
Machine=ns['ScheduledMachine'];fb=ns['fb'];checks=[]
def check(n,c):
 assert c,n
 checks.append(n)
def airborne():
 m=Machine();m.now(1000);m.call('hand_link_imu',1);m.call('hand_link_height',1,0);m.send(2,1)
 m.now(1600);m.call('hand_link_imu',1);m.send(1,2);m.send(3,3)
 for t in range(1603,1801,3):m.tick(t,1000)
 # Post-ascent fixture; full ascent is covered by verify_takeoff_1m.py.
 m.h('stage',3);m.put('takeoff_closed_loop','B',0);m.put('set_high','B',0)
 for t in range(1801,1951,3):m.tick(t,1000)
 m.put('pidHeightRate','f',0)
 return m
def land(m):m.send(4,4)
def step(m,mm):
 t=(m.get('hand_milliseconds','I')[0]+3)&0xffffffff;m.tick(t,mm);return t
m=airborne();land(m);previous=0;trajectory=[]
for i in range(700):
 t=step(m,1000);v=m.get('landing_speed','f')[0]
 assert -150.001<=v<=0 and -.451<=v-previous<=.001,(previous,v)
 previous=v
 if i%100==0:trajectory.append({'ms':t,'velocity_request':v})
check('descent slews to150mm/s while still powered',m.h('stage')==4 and m.get('ALL_flag','B')[0]&3==3 and abs(v+150)<.01)
check('altitude controller consumes landing velocity',abs(m.get('pidHeightRate','f')[0]+150)<.01)
# Above threshold cannot cut, even when retained for seconds.
for i in range(150):step(m,201)
check('201mm keeps motors unlocked',m.h('stage')==4 and m.get('ALL_flag','B')[0]&1)
# A single low sample is insufficient, and a repeated LAND does not reset dwell.
step(m,190);check('one low frame cannot cut',m.h('stage')==4)
for i in range(150):step(m,300)
check('height rebound resets confirmation',m.h('stage')==4 and not m.get('landing_confirming','B')[0])
for i in range(12):step(m,190)
check('short low interval cannot cut',m.h('stage')==4)
seq=5
for i in range(100):
 if i%10==0:m.send(4,seq);seq+=1
 step(m,190)
 if not(m.get('ALL_flag','B')[0]&1):break
check('continuous low height locks despite repeated LAND packets',m.h('stage')==0 and m.pwm()==(0,0,0,0))
check('normal touchdown is not reported as tilt fault',m.h('reason')==0 and m.h('last_fault')==0)
m.send(0,seq,channels=[1700,1700,1800,1300,2000,1000,1000,1000]);step(m,190)
check('old control cannot restart after touchdown',m.h('stage')==0 and m.pwm()==(0,0,0,0))
# Explicit land during ascent overrides it and decelerates the prior climb demand.
m=airborne();m.h('stage',2);m.put('takeoff_closed_loop','B',1);m.put('pidHeightRate','f',250);land(m)
check('LAND clears automatic climb',not m.get('takeoff_closed_loop','B')[0])
for i in range(900):step(m,1000)
check('climbing transitions smoothly to descent',abs(m.get('landing_speed','f')[0]+150)<.01 and m.h('stage')==4)
# Invalid ranges cannot count as near-ground touchdown.
for invalid in [0,49,3500,65535,None]:
 m=airborne();land(m)
 for i in range(100):
  if invalid==0:m.put('VL53L01_high','I',0)
  step(m,invalid)
 check('invalid/stale '+str(invalid)+' never cuts from false touchdown',m.get('ALL_flag','B')[0]&1 and m.h('stage')==4)
# Raw low range but filtered height high does not qualify.
m=airborne();land(m)
for i in range(80):
 m.put('pidHeightHigh','f',900,offset=4);m.now(m.get('hand_milliseconds','I')[0]+3)
 m.h('last_ms',m.get('hand_milliseconds','I')[0]);m.call('hand_link_imu',1);m.call('hand_link_height',1,190);m.call('hand_link_tick')
check('raw-filter mismatch cannot cut',m.h('stage')==4)
# One sample repeatedly marked fresh still cannot complete the frame-count guard.
m=airborne();land(m);m.put('pidHeightHigh','f',190,offset=4);m.call('hand_link_height',1,190)
for i in range(90):
 now=m.get('hand_milliseconds','I')[0]+3;m.now(now);m.h('last_ms',now);m.h('height_ms',now)
 m.call('hand_link_imu',1);m.call('hand_link_tick')
check('one retained sample cannot satisfy three new frames',m.h('stage')==4)
for failure in ['disarm','lease','imu']:
 m=airborne();land(m)
 if failure=='disarm':m.send(2,5)
 else:
  now=m.get('hand_milliseconds','I')[0]+(601 if failure=='lease' else 101);m.now(now)
  if failure=='imu':m.h('last_ms',now)
  m.call('hand_link_tick')
 check(failure+' overrides landing immediately',m.pwm()==(0,0,0,0) and not(m.get('ALL_flag','B')[0]&1))
# Timer rollover and absolute ground reference.
m=airborne();land(m);m.h('ground_mm',100);now=0xffffff80;m.now(now);m.put('last_tick','I',now)
for i in range(100):
 step(m,250)
check('250mm is not interpreted as150mm above launch offset',m.h('stage')==4)
for i in range(110):step(m,190)
check('low-height confirmation works across clock wrap',m.h('stage')==0 and m.pwm()==(0,0,0,0))
# Existing gesture down unchanged: only explicit LAND uses the new controller.
m=airborne();m.send(0,4,channels=[1500,1500,1100,1500,2000,1000,1000,1000])
for i in range(10):step(m,1000)
check('gesture descent remains native350mm/s',m.get('pidHeightRate','f')[0]==-350)
report={'passed':len(checks),'failed':0,'checks':checks,'trajectory':trajectory,'hardware_access':False,'flight_dynamics_validated':False,'elf_sha256':hashlib.sha256((src.parents[1]/'Output/gcc/official_hand.elf').read_bytes()).hexdigest()}
(src.parents[1]/'Output/gcc/landing_verification.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k not in ('checks','trajectory')}))
