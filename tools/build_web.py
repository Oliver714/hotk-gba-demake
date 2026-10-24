"""Build a self-contained itch.io HTML ZIP around the current GBA ROM."""
from pathlib import Path, PurePosixPath
import argparse
import hashlib
import json
import shutil
import subprocess
import tempfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def sha256(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(chunk)
    return result.hexdigest()


def build(rom, output, zip_path):
    lock = json.loads((ROOT / 'web-runtime.lock.json').read_text(encoding='utf-8'))
    cache = ROOT / '.cache/web'
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / ('EmulatorJS-' + lock['version'] + '.7z')
    if not archive.is_file():
        print('Downloading pinned EmulatorJS runtime (first build only)', flush=True)
        temporary = archive.with_suffix('.partial')
        with urllib.request.urlopen(lock['url'], timeout=120) as source, temporary.open('wb') as target:
            shutil.copyfileobj(source, target)
        if sha256(temporary) != lock['sha256']:
            raise SystemExit('EmulatorJS archive checksum mismatch')
        temporary.replace(archive)
    if sha256(archive) != lock['sha256']:
        raise SystemExit('EmulatorJS archive checksum mismatch')
    seven = shutil.which('7z') or shutil.which('7zz') or shutil.which('7za')
    tar = shutil.which('tar')
    if seven:
        listing = subprocess.check_output([seven, 'l', '-slt', str(archive)], text=True, encoding='utf-8')
        names = []
        for entry in listing.split('\n\n'):
            fields = dict(line.split(' = ', 1) for line in entry.splitlines() if ' = ' in line)
            name = fields.get('Path', '')
            if name and name != str(archive) and fields.get('Folder') != '+' and not fields.get('Attributes', '').startswith('D'):
                names.append(name)
    elif tar and 'bsdtar' in subprocess.check_output([tar, '--version'], text=True).lower():
        names = subprocess.check_output([tar, '-tf', str(archive)], text=True, encoding='utf-8').splitlines()
    else:
        raise SystemExit('Install 7-Zip (Ubuntu: sudo apt-get install p7zip-full), or use Windows/macOS bsdtar.')
    selected = []
    allowed_cores = {'data/cores/mgba-wasm.data', 'data/cores/mgba-legacy-wasm.data', 'data/cores/reports/mgba.json'}
    for name in names:
        path = PurePosixPath(name)
        if path.is_absolute() or '..' in path.parts:
            raise SystemExit('Unsafe runtime archive member')
        if name == 'LICENSE' or (name.startswith('data/') and not name.endswith('/') and
                                (not name.startswith('data/cores/') or name in allowed_cores)):
            selected.append(name)
    with tempfile.TemporaryDirectory(prefix='hotk-web-') as temporary:
        stage = Path(temporary)
        if seven:
            subprocess.run([seven, 'x', '-y', '-o' + str(stage), str(archive), *selected], check=True, stdout=subprocess.DEVNULL)
        else:
            subprocess.run([tar, '-xf', str(archive), '-C', str(stage), *selected], check=True)
        # Upstream checks its CDN only on localhost. Keep preview fully local,
        # including the equivalent source alongside the patched minified file.
        for relative in ['data/emulator.min.js', 'data/src/emulator.js']:
            file = stage / relative
            source = file.read_text(encoding='utf-8')
            old = 'https://cdn.emulatorjs.org/stable/data/version.json'
            if source.count(old) != 1:
                raise SystemExit('Unexpected EmulatorJS update-check patch context')
            file.write_text(source.replace(old, 'data/version.json'), encoding='utf-8')
        # Older bundled cores do not reliably implement IDBFS autoPersist.
        # Commit the core's save buffer explicitly on every scheduled save.
        patches = {
            'data/src/GameManager.js': (
                '//this.FS.syncfs(false, () => {});',
                'this.FS.syncfs(false, error => { if(error) console.error("Save persistence failed", error); });'),
            'data/emulator.min.js': (
                'saveSaveFiles(){this.functions.saveSaveFiles(),this.EJS.callEvent("saveSaveFiles",this.getSaveFile(!1))}',
                'saveSaveFiles(){this.functions.saveSaveFiles(),this.EJS.callEvent("saveSaveFiles",this.getSaveFile(!1)),this.FS.syncfs(false,e=>{if(e)console.error("Save persistence failed",e)})}')
        }
        for relative, (old, new) in patches.items():
            file = stage / relative
            source = file.read_text(encoding='utf-8')
            if source.count(old) != 1:
                raise SystemExit('Unexpected EmulatorJS save-sync patch context')
            file.write_text(source.replace(old, new), encoding='utf-8')
        for relative, old, new in [
            ('data/src/GameManager.js', '{ autoPersist: true }', '{ autoPersist: false }'),
            ('data/emulator.min.js', '{autoPersist:!0}', '{autoPersist:!1}'),
            ('data/src/GameManager.js', 'this.FS.unmount("/data/saves");', '// Keep the IDB mount alive until pending save commits finish.'),
            ('data/emulator.min.js', 'this.FS.unmount("/data/saves"),setTimeout', 'setTimeout')
        ]:
            file = stage / relative
            source = file.read_text(encoding='utf-8')
            if source.count(old) != 1:
                raise SystemExit('Unexpected EmulatorJS IDBFS mount patch context')
            file.write_text(source.replace(old, new), encoding='utf-8')
        (stage / 'licenses').mkdir(exist_ok=True)
        (stage / 'LICENSE').replace(stage / 'licenses/EmulatorJS-GPL-3.0.txt')
        shutil.copytree(ROOT / 'web', stage, dirs_exist_ok=True)
        shutil.copy2(rom, stage / 'game.gba')
        shutil.copy2(ROOT / 'docs/screenshots/title.png', stage / 'poster.png')
        shutil.copy2(ROOT / 'THIRD_PARTY_NOTICES.md', stage / 'THIRD_PARTY_NOTICES.md')
        shutil.copy2(ROOT / 'LICENSE', stage / 'licenses/PROJECT-GPL-3.0.txt')
        for folder in [ROOT / 'assets/fonts', ROOT / 'vendor/butano/licenses']:
            for file in folder.rglob('*'):
                if file.is_file() and file.suffix in ('.md', '.txt'):
                    target = stage / 'licenses' / folder.name / file.relative_to(folder)
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(file, target)
        shutil.copy2(ROOT / 'vendor/butano/LICENSE', stage / 'licenses/Butano-LICENSE.txt')
        manifest = {'rom_sha256': sha256(rom), 'emulatorjs_version': lock['version'],
                    'runtime_sha256': lock['sha256'], 'core': 'mgba', 'threads': False,
                    'patches': ['local-version-check', 'explicit-idb-save-sync']}
        (stage / 'web-build.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
        (stage / 'licenses/WEB-RUNTIME-SOURCES.txt').write_text(
            'EmulatorJS: ' + lock['source'] + '\n'
            'EmulatorJS core build sources: https://github.com/EmulatorJS/cores\n'
            'mGBA source (MPL-2.0): https://github.com/libretro/mgba\n'
            'RetroArch source (GPL-3.0): https://github.com/libretro/RetroArch\n'
            'The unmodified core archives include license.txt and core.json, exposed by the runtime.\n'
            'Game source: https://github.com/Oliver714/hotk-gba-demake\n', encoding='utf-8')
        files = sorted(file for file in stage.rglob('*') if file.is_file())
        assert len(files) < 1000, 'itch.io file count limit exceeded'
        assert (stage / 'index.html').is_file()
        output.mkdir(parents=True, exist_ok=True)
        shutil.copytree(stage, output, dirs_exist_ok=True)
        zip_path.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as bundle:
            for file in files:
                bundle.write(file, file.relative_to(stage).as_posix())
        print('Web bundle:', zip_path, 'files:', len(files), 'bytes:', zip_path.stat().st_size)
    return zip_path


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom', type=Path, default=ROOT / 'dist/sango_gba.gba')
    parser.add_argument('--output', type=Path, default=ROOT / 'dist/web')
    parser.add_argument('--zip', type=Path, default=ROOT / 'release/hotk-gba-demake-web.zip')
    args = parser.parse_args()
    if not args.rom.is_file():
        parser.error('Build the ROM with bash build.sh first.')
    build(args.rom, args.output, args.zip)
