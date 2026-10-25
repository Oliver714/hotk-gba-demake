"""Package a built ROM, build report and license notices for GitHub Release."""
from pathlib import Path
import argparse,hashlib,shutil,zipfile
from build_web import build as build_web
ROOT=Path(__file__).resolve().parents[1]
out=ROOT/'release';out.mkdir(exist_ok=True)
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--language', choices=('zh','en','all'), default='zh')
args=parser.parse_args()
if args.language == 'all':
    import subprocess,sys
    for language in ('zh','en'):
        subprocess.run([sys.executable,__file__,'--language',language],check=True)
    files=[out/name for name in ('sango_gba.gba','sango-gba.zip','hotk-gba-demake-web.zip',
                                 'sango_gba_en.gba','sango-gba-en.zip','hotk-gba-demake-web-en.zip')]
    (out/'SHA256SUMS.txt').write_text(''.join(hashlib.sha256(f.read_bytes()).hexdigest()+'  '+f.name+'\n' for f in files),encoding='utf-8')
    raise SystemExit(0)
suffix='_en' if args.language == 'en' else ''
label='-en' if args.language == 'en' else ''
rom=ROOT/('dist/sango_gba'+suffix+'.gba')
if not rom.is_file():raise SystemExit('Run bash build.sh first')
shutil.copy2(rom,out/rom.name)
with zipfile.ZipFile(out/('sango-gba'+label+'.zip'),'w',zipfile.ZIP_DEFLATED) as archive:
    for f in [rom,ROOT/('dist/build_report'+suffix+'.json'),ROOT/'LICENSE',ROOT/'README.md',ROOT/'THIRD_PARTY_NOTICES.md',ROOT/'docs/ASSET_PROVENANCE.md']:
        archive.write(f,f.name)
    for folder in [ROOT/'assets/fonts',ROOT/'vendor/butano/licenses']:
        for f in folder.rglob('*'):
            if f.is_file() and f.suffix in ('.md','.txt'):archive.write(f,f.relative_to(ROOT))
    archive.write(ROOT/'vendor/butano/LICENSE','vendor/butano/LICENSE')
web_zip = build_web(rom, ROOT/('dist/web'+label), out/('hotk-gba-demake-web'+label+'.zip'), args.language)
files=[out/rom.name,out/('sango-gba'+label+'.zip'),web_zip]
(out/'SHA256SUMS.txt').write_text(''.join(hashlib.sha256(f.read_bytes()).hexdigest()+'  '+f.name+'\n' for f in files),encoding='utf-8')
print('Release assets:',out)
