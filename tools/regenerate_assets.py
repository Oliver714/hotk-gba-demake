"""Explicit asset groups; normal builds never rerasterize bundled resources."""
from pathlib import Path
import argparse,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
groups={
 'battle':['import_battle_sprites','import_packed_sprites','import_zoom_sprites','import_additive_palette'],
 'effects':['import_battle_effects','import_effect_spans','import_additive_palette'],
 'ui':['import_silk_ui','import_additive_palette'],
 'font':['import_font'],
 'music':['import_music'],
}
p=argparse.ArgumentParser(description=__doc__);p.add_argument('group',choices=groups);a=p.parse_args()
for name in groups[a.group]:subprocess.run([sys.executable,str(ROOT/'tools'/(name+'.py'))],cwd=ROOT,check=True)
