#!/usr/bin/env python3
"""Build the supplied official C sources with GCC; no reconstructed flight code."""
from pathlib import Path
import hashlib,json,re,subprocess,xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[1]
BUILD=ROOT/'Output/gcc'
INCLUDES=[ROOT/x for x in ('StartUp','Lib/inc','Lib/src','USER','MATH','CONFIG','HARDWARE','HAL','USB_hid','USB_library','port')]
def run(a):
 p=subprocess.run([str(x) for x in a],capture_output=True,text=True,errors='replace')
 if p.returncode:print(p.stdout+p.stderr);raise SystemExit(p.returncode)
 return p.stdout+p.stderr
def main():
 BUILD.mkdir(parents=True,exist_ok=True)
 aliases=BUILD/'include_case';aliases.mkdir(exist_ok=True)
 headers={f.name.lower():f for d in INCLUDES for f in d.glob('*.h')}
 # Windows Keil includes are case insensitive; retain source bytes and alias
 # only differently capitalized include spellings for Linux GCC.
 for d in INCLUDES:
  for src in [*d.glob('*.c'),*d.glob('*.h')]:
   for name in re.findall(rb'#\s*include\s*"([^"/]+)"',src.read_bytes()):
    name=name.decode('ascii');target=headers.get(name.lower())
    if target and name!=target.name:
     (aliases/name).write_text('#include "'+str(target)+'"\n')
 sources=[]
 for f in ET.parse(ROOT/'mini.uvprojx').findall('.//File'):
  name=f.findtext('FilePath','').replace('\\','/')
  if name.lower().endswith('.c') and not name.endswith('core_cm3.c'):sources.append(ROOT/name)
 sources += [ROOT/'port/startup_gcc.c']
 adapter=ROOT/'HAL/hand_link.c'
 if adapter.exists() and adapter not in sources:sources.append(adapter)
 common=['-mcpu=cortex-m3','-mthumb','-mfloat-abi=soft','-std=c99','-Os','-g3','-ffunction-sections','-fdata-sections','-fno-strict-aliasing','-ffp-contract=off','-fcommon','-DSTM32F10X_MD','-DUSE_STDPERIPH_DRIVER','-Werror=implicit-function-declaration','-Werror=incompatible-pointer-types','-Wno-attributes','-Wno-unused-parameter']
 for d in [*INCLUDES,aliases]:common+=['-I',str(d)]
 objects=[];manifest=[];commands=[];logs=[]
 for i,src in enumerate(sources):
  obj=BUILD/(str(i)+'_'+src.stem+'.o');cmd=['arm-none-eabi-gcc',*common,'-c',src,'-o',obj]
  logs.append(run(cmd));objects.append(obj);commands.append({'directory':str(ROOT),'file':str(src),'arguments':[str(x) for x in cmd]})
  manifest.append({'file':str(src.relative_to(ROOT)),'sha256':hashlib.sha256(src.read_bytes()).hexdigest()})
 elf=BUILD/'official_hand.elf'
 logs.append(run(['arm-none-eabi-gcc','-mcpu=cortex-m3','-mthumb','-mfloat-abi=soft','-nostartfiles','-Wl,--gc-sections','-Wl,-Map='+str(BUILD/'official_hand.map'),'-T',ROOT/'port/stm32f103.ld',*objects,'-Wl,--start-group','-lm','-lc','-lgcc','-lnosys','-Wl,--end-group','-o',elf]))
 run(['arm-none-eabi-objcopy','-O','binary','--gap-fill','0xff','--pad-to','0x08020000',elf,BUILD/'official_hand.template.bin'])
 run(['arm-none-eabi-objcopy','-O','ihex',elf,BUILD/'official_hand.hex'])
 (BUILD/'compile_commands.json').write_text(json.dumps(commands,ensure_ascii=False,indent=2)+'\n')
 (BUILD/'source_manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
 logs.append(run(['arm-none-eabi-size',elf]));(BUILD/'build.log').write_text('\n'.join(logs));print(logs[-1])
if __name__=='__main__':main()
