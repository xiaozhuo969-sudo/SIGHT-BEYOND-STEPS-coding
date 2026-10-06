"""Execute 1m automatic ascent in the actual ARM ELF; no hardware I/O.
Height samples below are synthetic state-machine inputs, not a flight model.
"""
from pathlib import Path
import json,hashlib
src=Path(__file__).with_name('verify_takeoff_650.py')
ns={'__file__':str(src),'__name__':'one_metre_offline'}
exec(compile(src.read_text(encoding='utf-8').split('Machine = ScheduledMachine')[0],str(src),'exec'),ns)
Machine=ns['ScheduledMachine'];fb=ns['fb'];checks=[]
def check(name,ok):
 assert ok,name
 checks.append(name)
def start(ground=0):
 m=Machine();m.now(1000);m.call('hand_link_imu',1);m.call('hand_link_height',1,ground);m.send(2,1)
 m.now(1600);m.call('hand_link_imu',1);m.send(1,2);m.send(3,3)
 return m
def climbing(ground=0):
 m=start(ground)
 for t in range(1603,1751,3):m.tick(t,ground+220)
 check('200mm acquires closed loop while still TAKEOFF '+str(ground),m.h('stage')==2 and m.get('ALL_flag','B')[0]==19 and m.get('takeoff_closed_loop','B')[0]==1)
 return m
m=climbing();trajectory=[];previous=m.get('takeoff_height_target','f')[0]
for i in range(4000):
 t=m.get('hand_milliseconds','I')[0]+3
 mm=max(220,int(m.get('takeoff_height_target','f')[0]-15))
 m.tick(t,mm);target=m.get('takeoff_height_target','f')[0]
 assert previous<=target<=1000 and target-previous<=.601,(previous,target)
 assert -.001<=m.get('takeoff_climb_speed','f')[0]<=200.001
 assert -150.001<=m.get('pidHeightRate','f')[0]<=250.001
 assert all(0<=x<=1000 for x in m.pwm())
 if i%100==0:trajectory.append({'t_ms':t,'range_mm':mm,'target_mm':target,'stage':m.h('stage')})
 previous=target
 if m.h('stage')==3:break
check('default reaches HOLD only near one metre',m.h('stage')==3 and 950<=m.h('height_mm')<=1050)
check('automatic target fixed at1000mm',m.get('pidHeightHigh','f')[0]==1000)
check('trajectory is bounded and slew limited',i<4000)
check('ascent timeout not raised on normal profile',m.h('first_event')==0)
# After arrival the existing up gesture continues to climb; neutral recaptures.
t=m.get('hand_milliseconds','I')[0]
m.send(0,4,channels=[1500,1500,1800,1500,2000,1000,1000,1000])
for j in range(1,21):m.tick(t+j*3,1000+j*5)
check('up gesture after arrival selects native250mm/s',m.get('pidHeightRate','f')[0]==250)
m.send(0,5,channels=[1500,1500,1500,1500,2000,1000,1000,1000])
for j in range(21,51):m.tick(t+j*3,1100)
check('palm/neutral holds new height instead of returning to1m',m.get('pidHeightHigh','f')[0]>1050 and m.get('takeoff_closed_loop','B')[0]==0)
# Real altitude must settle; a spoofed early near-target sample cannot finish.
m=climbing();t=m.get('hand_milliseconds','I')[0]
for j in range(1,101):m.tick(t+j*3,1000)
check('early1000mm does not bypass planned trajectory',m.h('stage')==2 and m.get('takeoff_height_target','f')[0]<1000)
m.put('takeoff_height_target','f',1000);m.put('takeoff_climb_speed','f',0)
for j in range(101,251):m.tick(t+j*3,900)
check('being below950mm cannot declare target reached',m.h('stage')==2)
# Setpoint freeze on invalid/stale ranging, no catch-up jump when it recovers.
m=climbing();t=m.get('hand_milliseconds','I')[0];target=m.get('takeoff_height_target','f')[0]
m.tick(t+101,None)
check('stale range freezes target and speed',m.get('takeoff_height_target','f')[0]==target and m.get('takeoff_climb_speed','f')[0]==0)
m.tick(t+104,220)
check('range recovery resumes without accumulated step',0<m.get('takeoff_height_target','f')[0]-target<.01)
t+=104;target=m.get('takeoff_height_target','f')[0];m.tick(t+3,3500)
check('out-of-range pauses automatic target',m.get('takeoff_height_target','f')[0]==target)
# Failures and landing override the ascent; no restart via duplicate takeoff.
for kind in ['disarm','lease','imu','land','height_loss']:
 m=climbing();t=m.get('hand_milliseconds','I')[0]
 if kind=='disarm':m.send(2,4)
 elif kind=='lease':m.now(t+601);m.call('hand_link_tick')
 elif kind=='imu':m.now(t+101);m.h('last_ms',t+101);m.call('hand_link_tick')
 elif kind=='land':m.send(4,4)
 else:m.put('ALL_flag','B',1);m.tick(t+3,220)
 check(kind+' clears automatic ascent',m.get('takeoff_closed_loop','B')[0]==0)
 if kind in ['disarm','lease','imu']:check(kind+' keeps all motor outputs zero',m.pwm()==(0,0,0,0) and m.get('ALL_flag','B')[0]&1==0)
 elif kind=='land':
  m.tick(t+3,220);m.tick(t+6,220)
  check('landing uses closed-loop descent',m.h('stage')==4 and m.get('Remote','8H')[2]==1100 and m.get('ALL_flag','B')[0]&2)
 else:check('range-loss retains existing manual fallback',m.h('stage')==7 and m.get('ALL_flag','B')[0]&1)
m=climbing();t=m.get('hand_milliseconds','I')[0];target=m.get('takeoff_height_target','f')[0];m.send(3,4)
check('TAKEOFF retry cannot reset active climb',m.get('takeoff_height_target','f')[0]==target and m.get('takeoff_closed_loop','B')[0])
# Auto-target timeout stops climbing and reports failure, without cutting motors.
m=climbing();t=m.get('hand_milliseconds','I')[0];m.h('stage_ms',t-14999 & 0xffffffff);m.tick(t+3,220)
check('timeout exits to current-height hold with explicit reason',m.h('stage')==3 and m.h('reason')==11 and m.get('takeoff_closed_loop','B')[0]==0 and m.get('ALL_flag','B')[0]&2)
m=climbing();t=m.get('hand_milliseconds','I')[0];m.h('stage_ms',t-14999 & 0xffffffff);m.tick(t+3,3500)
check('timeout with invalid height uses existing fallback',m.h('stage')==7 and m.get('takeoff_closed_loop','B')[0]==0)
# Default target is absolute downward range to the shared flat floor.
m=climbing(100);m.put('takeoff_height_target','f',999.99);m.put('takeoff_climb_speed','f',10);t=m.get('hand_milliseconds','I')[0];m.tick(t+3,1000)
check('ground offset does not add100mm to floor target',m.get('takeoff_height_target','f')[0]==1000)
# During automatic ascent, normal hand channels cannot inject horizontal motion.
m=climbing();t=m.get('hand_milliseconds','I')[0]
m.send(0,4,channels=[1700,1700,1800,1300,2000,1000,1000,1000]);m.tick(t+3,220)
check('automatic ascent stays horizontally neutral',m.get('Remote','8H')[:4]==(1500,1500,1500,1500))
report={'passed':len(checks),'failed':0,'hardware_access':False,'actual_ELF':True,'synthetic_height_inputs':True,'flight_dynamics_validated':False,'trajectory':trajectory,'checks':checks,
 'elf_sha256':hashlib.sha256((src.parents[1]/'Output/gcc/official_hand.elf').read_bytes()).hexdigest()}
(src.parents[1]/'Output/gcc/takeoff_1m_verification.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k not in ('checks','trajectory')}))
