"""Fetch the locked Butano revision and apply the checked FLASH save patch."""
from pathlib import Path, PurePosixPath
import argparse
import hashlib
import json
import os
import shutil
import tarfile
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cache', type=Path, default=ROOT / '.cache/dependencies')
    args = parser.parse_args()
    lock_file = ROOT / 'dependencies.lock.json'
    patch_file = ROOT / 'dependencies/butano-patches.json'
    lock = json.loads(lock_file.read_text(encoding='utf-8'))['butano']
    state = {'lock_sha256': digest(lock_file), 'patch_sha256': digest(patch_file)}
    destination = ROOT / 'vendor/butano'
    marker = destination / '.dependency.json'
    if destination.exists():
        if (marker.is_file() and json.loads(marker.read_text()) == state
                and (destination / 'butano/butano.mak').is_file()):
            print('Butano dependency ready:', lock['revision'])
            return
        raise SystemExit('Unrecognized or outdated vendor/butano; move it aside, then rerun this script.')

    args.cache.mkdir(parents=True, exist_ok=True)
    archive = args.cache / ('butano-' + lock['revision'] + '.tar.gz')
    if not archive.exists():
        temporary = archive.with_suffix('.partial')
        print('Downloading Butano', lock['revision'], flush=True)
        urllib.request.urlretrieve(lock['url'], temporary)
        if digest(temporary) != lock['sha256']:
            raise SystemExit('Butano archive checksum mismatch: ' + str(temporary))
        temporary.replace(archive)
    if digest(archive) != lock['sha256']:
        raise SystemExit('Butano archive checksum mismatch: ' + str(archive))

    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.butano-', dir=destination.parent) as temporary:
        stage = Path(temporary) / 'butano'
        stage.mkdir()
        with tarfile.open(archive, 'r:gz') as source:
            for member in source.getmembers():
                parts = PurePosixPath(member.name).parts
                if not parts or parts[0] != 'butano-' + lock['revision']:
                    raise SystemExit('Unexpected archive root: ' + member.name)
                relative = PurePosixPath(*parts[1:])
                if not parts[1:]:
                    continue
                if relative.is_absolute() or '..' in relative.parts:
                    raise SystemExit('Unsafe archive path: ' + member.name)
                if relative.parts[0] not in ('butano', 'common', 'credits', 'licenses', 'LICENSE', 'README.md'):
                    continue
                target = stage.joinpath(*relative.parts)
                if member.isdir():
                    target.mkdir(parents=True, exist_ok=True)
                elif member.isfile():
                    target.parent.mkdir(parents=True, exist_ok=True)
                    with source.extractfile(member) as input_file, target.open('wb') as output:
                        shutil.copyfileobj(input_file, output)
                    target.chmod(member.mode & 0o777)
                else:
                    raise SystemExit('Unsupported archive member: ' + member.name)

        for patch in json.loads(patch_file.read_text(encoding='utf-8')):
            relative = PurePosixPath(patch['path'])
            if relative.is_absolute() or '..' in relative.parts:
                raise SystemExit('Unsafe patch path')
            target = stage.joinpath(*relative.parts)
            if digest(target) != patch['source_sha256']:
                raise SystemExit('Patch source checksum mismatch: ' + str(relative))
            text = target.read_text(encoding='utf-8')
            if text.count(patch['old']) != 1:
                raise SystemExit('Patch context mismatch: ' + str(relative))
            target.write_bytes(text.replace(patch['old'], patch['new']).encode('utf-8'))
            if digest(target) != patch['result_sha256']:
                raise SystemExit('Patch result checksum mismatch: ' + str(relative))
        (stage / '.dependency.json').write_text(json.dumps(state, sort_keys=True) + '\n')
        os.replace(stage, destination)
    print('Installed Butano:', lock['revision'])


if __name__ == '__main__':
    main()
