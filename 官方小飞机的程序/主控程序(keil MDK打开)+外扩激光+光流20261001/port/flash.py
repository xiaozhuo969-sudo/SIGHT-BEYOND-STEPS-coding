#!/usr/bin/env python3
"""Flash the official build over J-Link, preserving the live calibration page.
Requires a locked aircraft; does not send wireless commands or arm motors.
"""
from pathlib import Path
import argparse,hashlib,json,os,struct,subprocess,tempfile,zipfile,datetime
ROOT=Path(__file__).resolve().parents[1];BUILD=ROOT/'Output/gcc'
TOOLS=Path.home()/'.cache/codex-tools/openocd'
def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--existing',choices=['recovered','official'],required=True)
 args=parser.parse_args()
 # Use the verified ELF symbol for this build; recovered is the previously deployed layout.
 if args.existing=='official':
  nm=subprocess.check_output(['arm-none-eabi-nm',str(BUILD/'official_hand.elf')],text=True)
  previous=json.loads((BUILD/'deployment.json').read_text())
  flags=previous.get('flags_address',0x2000074c)
 else:flags=0x20000024
 env=os.environ.copy();env['LD_LIBRARY_PATH']=str(TOOLS/'usr/lib/x86_64-linux-gnu')
 base=[str(TOOLS/'usr/bin/openocd'),'-s',str(TOOLS/'usr/share/openocd/scripts'),'-f','interface/jlink.cfg','-c','transport select swd','-f','target/stm32f1x.cfg','-c','adapter speed 1000','-c','gdb_port disabled','-c','tcl_port disabled','-c','telnet_port disabled']
 log=[]
 def run(commands):
  result=subprocess.run(base+[y for c in commands for y in ('-c',c)],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
  log.append(result.stdout);(BUILD/'flash.log').write_text('\n'.join(log));print(result.stdout,end='')
  if result.returncode:raise RuntimeError('OpenOCD failed; see Output/gcc/flash.log')
 with tempfile.TemporaryDirectory(prefix='official-flight-') as tmp:
  d=Path(tmp);before=d/'before.bin';image=d/'deploy.bin';after=d/'after.bin'
  guard=f'if {{([lindex [read_memory {flags:#x} 8 1] 0] & 1) != 0}} {{error "Aircraft is armed"}}'
  pwm='foreach v [read_memory 0x40000034 32 4] {if {$v != 0} {error "Motor PWM is nonzero"}}'
  run(['init',guard,pwm,'halt',f'dump_image {before} 0x08000000 0x20000','resume','shutdown'])
  prior=before.read_bytes();data=bytearray((BUILD/'official_hand.template.bin').read_bytes());assert len(prior)==len(data)==131072
  data[0x7c00:0x8000]=prior[0x7c00:0x8000];image.write_bytes(data)
  archive=BUILD/'pre_protection_fix.zip'
  stamp=datetime.datetime.now().strftime('%Y%m%d_%H%M%S')
  with zipfile.ZipFile(archive,'a',compression=zipfile.ZIP_DEFLATED) as z:z.writestr(f'before_official_flash/{stamp}.bin',prior)
  run(['init',guard,pwm,'reset halt',f'flash write_image erase {image} 0x08000000 bin',f'verify_image {image} 0x08000000 bin',f'dump_image {after} 0x08000000 0x20000','reset run','shutdown'])
  readback=after.read_bytes();assert readback==data,'Full readback mismatch'
  assert readback[0x7c00:0x8000]==prior[0x7c00:0x8000]
  result={'date':stamp,'base':'official supplied C source','readback_matches':True,'bytes':len(data),'calibration_page_preserved':True,'calibration_values':list(struct.unpack('<6h',prior[0x7c00:0x7c0c])),'before_sha256':hashlib.sha256(prior).hexdigest(),'deployed_sha256':hashlib.sha256(data).hexdigest(),'template_sha256':hashlib.sha256((BUILD/'official_hand.template.bin').read_bytes()).hexdigest(),'armed_or_takeoff_sent':False,'flags_address':next(int(x.split()[0],16) for x in subprocess.check_output(['arm-none-eabi-nm',str(BUILD/'official_hand.elf')],text=True).splitlines() if x.endswith(' B ALL_flag'))}
  (BUILD/'deployment.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n');print(json.dumps(result,ensure_ascii=False))
if __name__=='__main__':main()
