"""Measure complete MJPEG frames from a real device (close other viewers first)."""
import argparse,json,time,urllib.request,io
from PIL import Image
p=argparse.ArgumentParser();p.add_argument('url');p.add_argument('--seconds',type=float,default=12);p.add_argument('--size',default='qvga');p.add_argument('--quality',default='fine',choices=['fast','fine']);p.add_argument('--output');p.add_argument('--sample-dir');a=p.parse_args()
from urllib.parse import urlsplit
base=a.url.rstrip('/');u=urlsplit(base);opener=urllib.request.build_opener(urllib.request.ProxyHandler({}))
start=time.monotonic();first=None;count=total=0;samples=[]
with opener.open(f'{u.scheme}://{u.hostname}:81/stream?size={a.size}&quality={a.quality}',timeout=5) as response:
 while time.monotonic()-start<a.seconds:
  length=None
  while True:
   line=response.readline()
   if not line:raise RuntimeError('Stream ended')
   if line.lower().startswith(b'content-length:'):length=int(line.split(b':')[1])
   if line==b'\r\n' and length is not None:break
  frame=response.read(length)
  if len(frame)!=length:raise RuntimeError('Truncated JPEG')
  if not frame.startswith(b'\xff\xd8') or not frame.endswith(b'\xff\xd9'):raise RuntimeError('Invalid JPEG markers')
  with Image.open(io.BytesIO(frame)) as image:
   image.load()
   if image.size != ((640,480) if a.size=='vga' else (320,240)):raise RuntimeError('Wrong image size')
  now=time.monotonic()
  if first is None:first=now
  count+=1;total+=length
  if count%20==1 and a.sample_dir:
   from pathlib import Path
   folder=Path(a.sample_dir);folder.mkdir(parents=True,exist_ok=True);(folder/f'frame-{count}.jpg').write_bytes(frame)
  if count%20==0:
   with opener.open(base+'/api/status',timeout=3) as r:samples.append(json.load(r))
elapsed=time.monotonic()-first
result={'resolution':a.size,'quality':a.quality,'frames':count,'elapsed_after_first_s':round(elapsed,2),'fps':round((count-1)/elapsed,2),'jpeg_kib_average':round(total/count/1024,2),'status_samples':samples}
print(json.dumps(result,ensure_ascii=False,indent=2))
if a.output:
 from pathlib import Path
 Path(a.output).write_text(json.dumps(result,ensure_ascii=False,indent=2))
