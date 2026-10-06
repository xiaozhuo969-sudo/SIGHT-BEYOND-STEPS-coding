"""Create a byte-verified ASCII build snapshot for Windows ESP binutils.

Directory junctions are insufficient: IDF resolves them back to Chinese paths.
Only files recorded in our previous snapshot can be removed from staging.
"""
import argparse, hashlib, json, shutil
from pathlib import Path

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source-root',type=Path,required=True)
parser.add_argument('--destination',type=Path,required=True)
args=parser.parse_args()
source=args.source_root.resolve();dest=args.destination.resolve()
allowed=Path('D:/Espressif/sources').resolve()
if not dest.is_relative_to(allowed) or dest==allowed or not str(dest).isascii():
    raise SystemExit('Staging must be an ASCII subdirectory of D:/Espressif/sources')
dest.mkdir(parents=True,exist_ok=True)
files={}
hand=source/'手控制器'
for name in ('CMakeLists.txt','sdkconfig','sdkconfig.defaults','dependencies.lock','partitions.csv'):
    p=hand/name
    if p.is_file(): files['hand/'+name]=p
for directory,relative in ((hand/'main','hand/main'),(hand/'components','hand/components'),
                            (hand/'managed_components','hand/managed_components'),(source/'shared','shared')):
    if not directory.is_dir():raise SystemExit(f'Missing source directory: {directory}')
    for p in directory.rglob('*'):
        if p.is_file() and not any(x in ('.git','__pycache__','build') for x in p.relative_to(directory).parts):
            files[relative+'/'+p.relative_to(directory).as_posix()]=p
manifest=dest/'snapshot.json'
previous=json.loads(manifest.read_text(encoding='utf-8'))['files'] if manifest.exists() else {}
for relative in previous.keys()-files.keys():
    stale=(dest/relative).resolve()
    if not stale.is_relative_to(dest):raise SystemExit('Unsafe old snapshot path')
    if stale.is_file():stale.unlink()
hashes={}
for relative,p in sorted(files.items()):
    target=dest/relative
    if not target.resolve().is_relative_to(dest):raise SystemExit('Unsafe staging path')
    target.parent.mkdir(parents=True,exist_ok=True)
    digest=hashlib.sha256(p.read_bytes()).hexdigest()
    if not target.exists() or hashlib.sha256(target.read_bytes()).hexdigest()!=digest:
        shutil.copy2(p,target)
    if hashlib.sha256(target.read_bytes()).hexdigest()!=digest:raise SystemExit(f'Copy mismatch: {relative}')
    hashes[relative]=digest
manifest.write_text(json.dumps({'source_root':str(source),'files':hashes},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(f'Verified {len(hashes)} staged files: {dest}')
