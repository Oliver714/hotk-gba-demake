"""Verify GBA header/checksum, ROM cartridge size and create a padded release ROM."""
from pathlib import Path
import hashlib
import json
import struct
import argparse
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--output', default='sango_gba.gba', help='Release filename in dist')
args = parser.parse_args()
assert Path(args.output).name == args.output, 'Expected a filename'

root = Path(__file__).resolve().parents[1]
rom = (root/'sango_gba.gba').read_bytes()
assert 192 <= len(rom) <= 32*1024*1024, 'ROM exceeds the normal 32 MiB GBA address space'
assert rom[0xb2] == 0x96, 'Invalid fixed header value'
assert rom[0xbd] == (-sum(rom[0xa0:0xbd])-0x19)&255, 'Invalid GBA header complement'
assert rom[0xac:0xb0] == b'SGQ1'
assert b'FLASH1M_V' in rom, 'Missing emulator 128 KiB flash detection tag'
size = 1024*1024
while size < len(rom):
    size *= 2
dist = root/'dist'
dist.mkdir(exist_ok=True)
release = rom + b'\xff'*(size-len(rom))
file = dist/args.output
for companion in ('SourceHanMono-LICENSE.md', 'SourceHanMono-SOURCE.md',
                  'ZLabsPixel-OFL.txt', 'ZLabsPixel-SOURCE.md'):
    source = root/'assets'/'fonts'/companion
    (dist/companion).write_bytes(source.read_bytes())
report = {'raw_bytes':len(rom), 'cartridge_bytes':size, 'limit_bytes':32*1024*1024,
          'sha256':hashlib.sha256(release).hexdigest(), 'title':rom[0xa0:0xac].decode('ascii'),
          'code':rom[0xac:0xb0].decode('ascii'), 'header_checksum_valid':True,
          'flash1m_tag_present':True, 'toolchain_lock_sha256':hashlib.sha256((root/'toolchain.lock.json').read_bytes()).hexdigest()}
try:
    report['source_commit']=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,stderr=subprocess.DEVNULL,text=True).strip()
except (OSError,subprocess.CalledProcessError):
    report['source_commit']=None
elf_file = root/'sango_gba.elf' 
if elf_file.exists():
    elf = elf_file.read_bytes()
    assert elf[:6] == b'\x7fELF\x01\x01', 'Expected little-endian ELF32'
    section_offset = struct.unpack_from('<I',elf,32)[0]
    section_size, count = struct.unpack_from('<HH',elf,46)
    totals = {'ewram':0,'iwram':0}
    for i in range(count):
        _,_,flags,address,_,length,_,_,_,_ = struct.unpack_from('<10I',elf,section_offset+i*section_size)
        if flags & 2 and length:
            if 0x02000000 <= address < 0x02040000:
                assert address+length <= 0x02040000, 'EWRAM section exceeds 256 KiB'
                totals['ewram'] += length
            elif 0x03000000 <= address < 0x03008000:
                assert address+length <= 0x03006000, 'IWRAM leaves less than 8 KiB for stack'
                totals['iwram'] += length
    report.update(ewram_allocated_bytes=totals['ewram'], iwram_allocated_bytes=totals['iwram'],
                  ewram_limit_bytes=256*1024, iwram_limit_bytes=32*1024, flash_bytes=128*1024)
file.write_bytes(release) # Publish only after every memory/header gate passes.
(dist/'build_report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report,indent=2))
