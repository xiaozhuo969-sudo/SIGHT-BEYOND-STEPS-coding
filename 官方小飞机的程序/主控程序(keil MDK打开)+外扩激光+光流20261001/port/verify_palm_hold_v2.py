"""Actual-ELF stop-anchor and hold-gain comparisons; no hardware access."""
from pathlib import Path
import json,hashlib
from elftools.elf.elffile import ELFFile
src=Path(__file__).with_name('verify_flow_recovery.py')
ns={'__file__':str(src),'__name__':'hold_v2_offline'}
exec(compile(src.read_text(encoding='utf-8').split('m=prepared()')[0],str(src),'exec'),ns)
checks=[]
def check(name,ok):
 assert ok,name
 checks.append(name)
def prepared(old=False):
 m=ns['FlowMachine']()
 if old:
  m.u.mem_write(0x20000000,bytes(0x10000))
  with Path(r'D:/Espressif/releases/palm-stop-fix-20261007/official_hand.elf').open('rb') as stream:
   e=ELFFile(stream);m.sym={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols() if s['st_value']}
   for seg in e.iter_segments():
    if seg['p_type']=='PT_LOAD':m.u.mem_write(seg['p_vaddr'],seg.data())
  m.call('pid_param_Init');m.call('hand_link_init');m.put('ALL_flag','B',19);m.rc(1500,1500,1500,1500)
 for t in range(0,217,6):
  m.sample(t)
  if t==216:m.put('flow_data','ff',125,-100,offset=4)
  if t%24==0:m.estimate()
 for _ in range(3):m.control()
 return m

evidence=[]
for channels in [(1500,1800,1500,1500),(1800,1500,1500,1500),(1500,1500,1500,1900)]:
 a,b=prepared(),prepared(True)
 for m in [a,b]:
  m.put('pidPosRateX','f',5,offset=12);m.put('pidPosRateY','f',12.5,offset=12)
  for i in range(42):
   m.sample(240+i*24);m.rc(*channels)
   m.put('flow_x_lpf_att_i','f',100+i);m.put('flow_y_lpf_att_i','f',-80+i);m.control()
  m.sample(1248);m.rc(1500,1500,1500,1500);m.control()
 evidence.append({'command':channels,'previous_targets':b.targets(),'new_targets':a.targets()})
 check('both stop targets capture current '+str(channels),a.targets()==[141,-39])
 check('previous firmware reproduces stale cross-axis target '+str(channels),b.targets()!=[141,-39])
 check('restore both pre-translation trims '+str(channels),abs(a.get('pidPosRateX','f',offset=12)[0]-5)<1e-5 and abs(a.get('pidPosRateY','f',offset=12)[0]-12.5)<1e-5)
 check('no derivative kick on cross-axis recapture '+str(channels),abs(a.get('pidPosRateX','f',offset=40)[0]-.2)<1e-5 and abs(a.get('pidPosRateY','f',offset=40)[0]-.5)<1e-5)
 count=a.get('flow_target_captures','H')[0]
 for i in range(1,43):
  a.sample(1248+i*24);a.put('flow_x_lpf_att_i','f',141+i*.1);a.put('flow_y_lpf_att_i','f',-39-i*.1);a.control()
 check('continuous hold never follows drift '+str(channels),a.targets()==[141,-39] and a.get('flow_target_captures','H')[0]==count)
# Same measurement error: increase only hold P, preserve integral and derivative.
responses=[]
for axis in ('x','y'):
 for sign in (-1,1):
  a,b=prepared(),prepared(True);rate='pidPosRate'+axis.upper()
  for m in [a,b]:
   m.sample(240);m.put('flow_'+axis+'_vel_lpf_i','f',sign)
   m.put(rate,'f',-sign,offset=8);m.control()
  old=b.get(rate,'f',offset=40)[0];new=a.get(rate,'f',offset=40)[0]
  check('hold opposes measured velocity '+axis+str(sign),new*sign<0)
  check('hold P increased exactly by .15 per velocity unit '+axis+str(sign),abs(new-old+.15*sign)<1e-5)
  responses.append({'axis':axis,'velocity':sign,'old_rate_output':old,'new_rate_output':new})
  # Steady position displacement likewise needs a stronger return command.
  a,b=prepared(),prepared(True)
  for m in [a,b]:
   m.sample(240);m.put('flow_'+axis+'_lpf_att_i','f',m.targets()[0 if axis=='x' else 1]+sign*20)
   m.put(rate,'f',-.4*sign,offset=8);m.control()
  check('position correction retains sign and increases '+axis+str(sign),a.get(rate,'f',offset=40)[0]*sign<b.get(rate,'f',offset=40)[0]*sign<0)
# Motion profile including cross-axis correction remains identical to v1.
a,b=prepared(),prepared(True)
for i in range(84):
 for m in [a,b]:
  m.sample(240+i*24);m.rc(1500,1800,1500,1500)
  m.put('flow_x_lpf_att_i','f',100+i*.2);m.put('flow_y_vel_lpf_i','f',1);m.control()
 check('gesture outputs unchanged '+str(i),a.angles()==b.angles())
# Large position/speed errors never bypass existing attitude limits.
m=prepared()
for i in range(84):
 m.sample(240+i*24);m.put('flow_x_lpf_att_i','f',100+10000);m.put('flow_y_lpf_att_i','f',-80-10000)
 m.put('flow_x_vel_lpf_i','f',-100);m.put('flow_y_vel_lpf_i','f',100);m.control()
 check('large-error bounded '+str(i),all(abs(x)<=15 for x in m.angles()))
# Quality dropout and disarm erase the dual-axis stored trim.
m=prepared();m.put('pidPosRateX','f',50,offset=12);m.sample(240);m.rc(1500,1800,1500,1500);m.control()
m.sample(264,quality=0);m.control()
check('loss clears full-stop memory',m.get('flow_stop_trim','2f')==(0,0) and m.get('flow_was_moving','B')[0]==0)
report={'passed':len(checks),'failed':0,'hardware_access':False,'actual_ELF':True,'evidence':evidence,'responses':responses,'checks':checks,
 'elf_sha256':hashlib.sha256((src.parents[1]/'Output/gcc/official_hand.elf').read_bytes()).hexdigest()}
(src.parents[1]/'Output/gcc/palm_hold_v2_verification.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k!='checks'}))
