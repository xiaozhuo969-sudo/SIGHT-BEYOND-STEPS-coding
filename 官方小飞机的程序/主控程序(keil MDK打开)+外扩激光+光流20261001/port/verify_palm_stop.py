"""Controller regression on actual ELF in Unicorn; no hardware access."""
from pathlib import Path
import json, hashlib
src=Path(__file__).with_name('verify_flow_recovery.py')
ns={'__file__':str(src),'__name__':'palm_stop_offline'}
exec(compile(src.read_text(encoding='utf-8').split('m=prepared()')[0],str(src),'exec'),ns)
prepared=ns['prepared']
checks=[]
def check(name,condition):
    assert condition,name
    checks.append(name)

def advance(m,t,axis,command,velocity,position=0):
    m.sample(t)
    m.rc(1800 if axis=='x' and command<0 else 1200 if axis=='x' and command>0 else 1500,
         1800 if axis=='y' and command>0 else 1200 if axis=='y' and command<0 else 1500,1500,1500)
    m.put('flow_'+axis+'_vel_lpf_i','f',velocity)
    m.put('flow_'+axis+'_lpf_att_i','f',position)
    m.control()

evidence=[]
for axis in ('x','y'):
 for sign in (-1,1):
    m=prepared();rate='pidPosRate'+axis.upper();position='pidPosition'+axis.upper()
    for i in range(84):advance(m,240+i*24,axis,sign,sign)
    before=m.get(rate,'13f')
    outputs=[]
    for i in range(42):
        advance(m,2256+i*24,axis,0,sign,sign*i)
        output=m.get(rate,'f',offset=40)[0];outputs.append(output)
        check(f'{axis}/{sign} brakes residual velocity tick {i}',output*sign<0)
        check(f'{axis}/{sign} no transition saturation {i}',abs(output)<5)
    check(f'{axis}/{sign} hold target captured once',abs(m.get(position,'f')[0])<1e-5)
    evidence.append({'axis':axis,'sign':sign,'pre_rate_out':before[10],'first_brake_out':outputs[0],'next_brake_out':outputs[1]})
    # The stop with zero measured velocity has no inherited forward integral.
    m=prepared()
    for i in range(84):advance(m,240+i*24,axis,sign,sign)
    advance(m,2256,axis,0,0)
    check(f'{axis}/{sign} stopped does not keep driving',abs(m.get(rate,'f',offset=40)[0])<1e-5)
    # Preserve learned load compensation; discard only motion-acquired history.
    m=prepared();m.put(rate,'f',12.5,offset=12)
    for i in range(84):advance(m,240+i*24,axis,sign,sign)
    advance(m,2256,axis,0,0)
    check(f'{axis}/{sign} pre-gesture hold trim preserved',abs(m.get(rate,'f',offset=12)[0]-12.5)<1e-4)
    # A release on one axis must engage its hold while the other keeps moving.
    m=prepared();m.sample(240);m.rc(1200,1800,1500,1500);m.control()
    m.sample(264);m.rc(1500,1800,1500,1500);m.put('flow_x_vel_lpf_i','f',1);m.control()
    check(f'independent axis release {axis}/{sign}',m.get('pidPosRateX','f',offset=40)[0]<0 and m.get('pidPosRateY','f')[0]==15)

m=prepared()
for i in range(2500):advance(m,240+i*24,'y',1,0)
check('60s translation integral bounded',abs(m.get('pidPosRateY','f',offset=12)[0]*.04)<=3.00001)
advance(m,60240,'y',0,0)
check('60s translation cannot contaminate neutral',abs(m.get('pidPosRateY','f',offset=40)[0])<1e-5)
# Translation reverse must not create a derivative command-step spike.
advance(m,60264,'y',1,0);advance(m,60288,'y',-1,0)
check('reverse command no derivative spike',abs(m.get('pidPosRateY','f',offset=40)[0])<3)
# Position correction signs and persistent capture over repeated neutral ticks.
for sign in (-1,1):
 m=prepared();target=m.targets()[1]
 for i in range(42):
    m.sample(240+i*24);m.put('flow_y_lpf_att_i','f',target+sign*20);m.control()
 check('hold corrects displacement '+str(sign),m.get('pidPosRateY','f',offset=40)[0]*sign<0)
 check('hold does not chase displaced position '+str(sign),m.targets()[1]==target)
# Loss of source must erase the saved trim and old target too.
m=prepared();m.put('pidPosRateY','f',50,offset=12)
advance(m,240,'y',1,1);m.sample(264,quality=0);m.control()
check('bad source removes all horizontal output',m.zero_angles())
for t in range(270,721,6):
 m.sample(t)
 if t%24==0:m.estimate();m.rc(1500,1500,1500,1500);m.control()
check('recovery does not resurrect stored trim',m.zero_angles())
# Disarm never acquires motor output, including after motion.
m=prepared();advance(m,240,'y',1,1);m.put('ALL_flag','B',0);m.control();m.call('MotorControl')
check('disarm motors stay zero',m.pwm()==(0,0,0,0))
check('disarm clears horizontal integral',m.get('pidPosRateY','f',offset=12)[0]==0)
report={'passed':len(checks),'failed':0,'actual_ELF':True,'hardware_access':False,
 'elf_sha256':hashlib.sha256((src.parents[1]/'Output/gcc/official_hand.elf').read_bytes()).hexdigest(),
 'evidence':evidence,'checks':checks}
(src.parents[1]/'Output/gcc/palm_stop_verification.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k!='checks'}))
