"""Execute flight ELF offline; no probe, radio or motor access."""
from pathlib import Path
import json, struct, hashlib
src=Path(__file__).with_name('verify.py')
ns={'__file__':str(src),'__name__':'flow_recovery_offline'}
exec(compile(src.read_text(encoding='utf-8').split('checks=[]')[0],str(src),'exec'),ns)
Machine,fb=ns['Machine'],ns['fb']
from unicorn import UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R2,UC_ARM_REG_PC,UC_ARM_REG_LR
checks=[]
def check(name,ok):
 assert ok,name
 checks.append(name)
class FlowMachine(Machine):
 def __init__(self):
  super().__init__();self.packet=bytes([1,144,0,0,80]);self.fail=False
  def iic(u,a,n,d):
   if a==(self.sym['IIC_read_Bytes']&~1):
    if not self.fail:u.mem_write(u.reg_read(UC_ARM_REG_R2),self.packet)
    u.reg_write(UC_ARM_REG_R0,1 if self.fail else 0)
    u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
  self.u.hook_add(UC_HOOK_CODE,iic)
  self.put('ALL_flag','B',19);self.rc(1500,1500,1500,1500)
 def sample(self,t,height=400,quality=80,dx=0,dy=0,fail=False):
  self.now(t);self.fail=fail;self.packet=struct.pack('>H',height)+bytes([dx&255,dy&255,quality]);self.call('flow_get_data')
 def control(self):self.call('FlowPidControl',fb(.024))
 def estimate(self):self.call('flow_data_sins')
 def ready(self):return self.call('flow_position_ready')!=0
 def targets(self):return [self.get(n,'f')[0] for n in ['pidPositionX','pidPositionY']]
 def angles(self):return [self.get(n,'f')[0] for n in ['pidRoll','pidPitch']]
 def zero_angles(self):return all(abs(x)<1e-5 for x in self.angles())
def prepared():
 m=FlowMachine();check('cold estimate not usable',not m.ready())
 for t in range(0,217,6):
  m.sample(t)
  if t==216:m.put('flow_data','ff',125,-100,offset=4)
  if t%24==0:m.estimate()
  if t<200:check('recovery dwell gates initial estimate '+str(t),not m.ready())
 check('fresh stable estimate usable after dwell',m.ready())
 for _ in range(3):m.control()
 check('capture nonzero current position',m.targets()==[100,-80] and m.zero_angles())
 return m
m=prepared()
for t in range(222,1217,6):
 m.sample(t)
 if t%24==0:m.estimate();m.control()
check('valid stationary estimate preserves target without false correction',m.targets()==[100,-80] and m.zero_angles())
for name,h,q in [('quality',400,30),('low_height',79,80),('zero_height',0,80),('high_height',3500,80)]:
 m=prepared();m.sample(222,h,q);check(name+' immediately invalidates',not m.ready());m.control()
 check(name+' clears horizontal targets and commands',m.targets()==[0,0] and m.zero_angles())
 check(name+' does not change height lock or unlock',m.get('ALL_flag','B')[0]==19)
 for t in range(228,445,6):
  m.sample(t)
  if t%24==0:
   m.estimate();m.control()
   if t<440:check(name+' holds level during recovery '+str(t),not m.ready() and m.zero_angles())
 # Recovery starts at the first estimator tick (240 ms).
 for t in range(450,673,6):
  m.sample(t)
  if t%24==0:m.estimate();m.control()
 check(name+' recaptures new frame without old target',m.ready() and m.targets()==[0,0] and m.zero_angles())
 for t in range(678,6673,6):
  m.sample(t)
  if t%24==0:m.estimate();m.control()
 check(name+' neutral remains neutral for six seconds',m.zero_angles() and m.targets()==[0,0])
m=prepared();m.sample(222,quality=30);m.sample(228);m.control()
check('bad frame between estimator ticks cannot be hidden by next good frame',not m.ready() and m.zero_angles())
m=prepared();m.sample(222,fail=True);m.estimate();m.control()
check('one I2C failure retains fresh valid estimate',m.ready() and m.targets()==[100,-80] and m.zero_angles())
m=prepared();m.now(316);check('freshness at 100ms',m.ready());m.now(317);check('stale at 101ms',not m.ready());m.estimate();m.control()
check('stale data cannot create zero-position correction',m.zero_angles() and m.targets()==[0,0] and m.get('ALL_flag','B')[0]==19)
m=prepared()
for i in range(11):m.sample(222+i*6,fail=True)
check('existing consecutive I2C failure vertical exit retained',m.get('ALL_flag','B')[0]==1)
m=FlowMachine();m.put('ALL_flag','B',1);m.put('pidRoll','f',4);m.put('pidPitch','f',-3);m.control()
check('manual attitude commands not overwritten when flow mode off',m.angles()==[4,-3])
m=FlowMachine();m.put('ALL_flag','B',0);m.control();check('locked controller stays locked',m.get('ALL_flag','B')[0]==0)
m=FlowMachine()
for i in range(0,241,6):
 m.sample((0xffffff80+i)&0xffffffff)
 if i%24==0:m.estimate()
check('recovery dwell tolerates millisecond wrap',m.ready())
m=FlowMachine()
for t in range(0,217,6):
 m.sample(t,quality=31,height=80)
 if t%24==0:m.estimate()
check('quality31 and height80 are usable input boundaries',m.ready())
# A moving target after recovery still commands correction; the fix must not
# accidentally disable horizontal control for healthy samples.
m=prepared();m.put('flow_x_lpf_att_i','f',110);m.put('flow_y_lpf_att_i','f',-90);m.control()
check('healthy displaced estimate still generates signed correction',m.angles()[0]<0 and m.angles()[1]<0)
# Recovery at a nonzero physical tilt seeds position and velocity consistently.
m=prepared();m.sample(222,quality=30);m.control();m.put('Angle','3f',3,-4,0)
for t in range(228,697,6):
 m.sample(t)
 if t%24==0:m.estimate();m.control()
check('tilted stationary recovery produces no false velocity',abs(m.get('flow_x_vel_lpf_i','f')[0])<1e-5 and abs(m.get('flow_y_vel_lpf_i','f')[0])<1e-5)
check('tilted stationary recovery recaptures compensated position',m.ready() and m.zero_angles() and abs(m.targets()[0])>1)
# Any new invalid frame restarts the stability dwell, even when it arrives
# between position-control ticks.
m=prepared();m.sample(222,quality=30);m.control()
for t in range(228,409,6):
 m.sample(t)
 if t%24==0:m.estimate();m.control()
m.sample(414,quality=30);m.sample(420);m.estimate();m.control()
m.sample(450);m.estimate();m.control()
check('second glitch restarts recovery dwell',not m.ready() and m.zero_angles())
# Native altitude/attitude integrators are outside the reset scope.
m=prepared();m.put('pidHeightRate','f',23,offset=12);m.put('pidRateY','f',7,offset=12)
m.sample(222,quality=30);m.control()
check('horizontal invalidation leaves vertical and rate integrals untouched',m.get('pidHeightRate','f',offset=12)[0]==23 and m.get('pidRateY','f',offset=12)[0]==7)
result={'passed':len(checks),'failed':0,'hardware_access':False,'actual_ELF':True,'tests':checks,'elf_sha256':hashlib.sha256((src.parents[1]/'Output/gcc/official_hand.elf').read_bytes()).hexdigest()}
(src.parents[1]/'Output/gcc/flow_recovery_verification.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print(json.dumps({k:v for k,v in result.items() if k!='tests'}))
