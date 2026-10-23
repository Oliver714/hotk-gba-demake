"""Install hash-locked Wonderful packages (Linux x86_64). No rolling updates."""
from pathlib import Path
import argparse,hashlib,json,os,platform,subprocess,urllib.request
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--prefix',type=Path,default=Path('/opt/wonderful'))
p.add_argument('--cache',type=Path,default=ROOT/'toolchain-cache')
p.add_argument('--verify-only',action='store_true')
a=p.parse_args()
if platform.system()!='Linux' or platform.machine() not in ('x86_64','AMD64'):
    p.error('The lock is for Linux x86_64; Windows users should use WSL.')
lock=json.loads((ROOT/'toolchain.lock.json').read_text())
a.cache.mkdir(parents=True,exist_ok=True)
for package in lock['packages']:
    file=a.cache/package['filename']
    if not file.exists():
        print('Download',file.name,flush=True)
        temporary=file.with_name(file.name+'.partial')
        urllib.request.urlretrieve(package['url'],temporary)
        temporary.replace(file)
    actual=hashlib.sha256(file.read_bytes()).hexdigest()
    if actual!=package['sha256']:raise SystemExit('Checksum mismatch: '+str(file))
    if not a.verify_only:
        a.prefix.mkdir(parents=True,exist_ok=True)
        subprocess.run(['tar','--extract','--xz','--file',str(file),'--directory',str(a.prefix),'--no-same-owner','--exclude=.PKGINFO','--exclude=.BUILDINFO','--exclude=.MTREE'],check=True)
print('Verified',len(lock['packages']),'locked packages')
