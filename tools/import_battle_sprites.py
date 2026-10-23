"""Import battle sprites and generated walk cycles into the GBA palette."""
from pathlib import Path
import colorsys
import re
import sys
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
default_source = ROOT / 'assets/battle/battle-atlas-v14.png'
default_walk = ROOT / 'assets/battle/battle-walk-v4.png'
default_cavalry_walk = ROOT / 'assets/battle/battle-cavalry-walk-v5.png'
source_path = Path(sys.argv[1]) if len(sys.argv) > 1 else default_source
walk_path = Path(sys.argv[2]) if len(sys.argv) > 2 else default_walk
cavalry_walk_path = Path(sys.argv[3]) if len(sys.argv) > 3 else default_cavalry_walk
source_path = source_path if source_path.is_absolute() else ROOT / source_path
walk_path = walk_path if walk_path.is_absolute() else ROOT / walk_path
cavalry_walk_path = cavalry_walk_path.resolve()
production_source = (source_path.resolve() == default_source.resolve() and
                     walk_path.resolve() == default_walk.resolve())
version = 'v19' if production_source else source_path.stem.rsplit('-', 1)[-1]
source_version = source_path.stem.rsplit('-', 1)[-1]


def alpha_cutout(image):
    pixels = image.load()
    for y in range(image.height):
        for x in range(image.width):
            r, g, b, a = pixels[x, y]
            pixels[x, y] = (r, g, b, 255) if a >= 110 else (0, 0, 0, 0)
    return image


def load_base_frames():
    source = Image.open(source_path).convert('RGBA')
    width, height = source.size
    columns = [round(width * i / 4) for i in range(5)]
    row_bounds = [(0, 0, 512), (1, 512, height)] if source_version in ('v14', 'v15') else [(0, 0, height // 2), (1, height // 2, height)]
    result = []
    for row, (row_id, top, bottom) in enumerate(row_bounds):
        sprite_w, sprite_h = ((56, 48), (28, 30))[row]
        teams = []
        for team in range(2):
            frames = []
            for col in range(4):
                crop = source.crop((columns[col], top, columns[col + 1], bottom))
                bbox = crop.getchannel('A').getbbox()
                if not bbox:
                    raise ValueError(f'Empty sprite in {source_path.name}, row {row_id}, cell {col}')
                crop = crop.crop(bbox)
                crop = alpha_cutout(crop)
                bbox = crop.getchannel('A').getbbox()
                crop = crop.crop(bbox)
                crop.thumbnail((sprite_w, sprite_h), Image.LANCZOS)
                canvas = Image.new('RGBA', (sprite_w, sprite_h), (0, 0, 0, 0))
                canvas.paste(crop, ((sprite_w - crop.width) // 2, sprite_h - crop.height))
                pixels = canvas.load()
                for y in range(sprite_h):
                    for x in range(sprite_w):
                        r, g, b, a = pixels[x, y]
                        if a < 110:
                            pixels[x, y] = (0, 0, 0, 0)
                            continue
                        hue, saturation, value = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
                        if source_version in ('v14', 'v15') and team == 0 and (hue <= .045 or hue >= .97) and saturation >= .38:
                            r, g, b = [round(c * 255) for c in colorsys.hsv_to_rgb(.60, saturation, value)]
                        elif team == 1 and b > r * 1.15 and b > g * 1.08:
                            r, g, b = [round(c * 255) for c in colorsys.hsv_to_rgb(.01, saturation, value)]
                        pixels[x, y] = (r, g, b, 255)
                frames.append(canvas)
            teams.append(frames)
        result.append(teams)
    return result


def load_walk_frames():
    sheet = Image.open(walk_path).convert('RGBA')
    width, height = sheet.size
    # Generated sheets do not reliably have equal-height rows. Extract the
    # five complete connected silhouettes in each column instead of slicing
    # through horses' legs at height / 5.
    columns = []
    for col in range(4):
        cell = sheet.crop((round(width * col / 4), 0, round(width * (col + 1) / 4), height))
        cw, ch = cell.size
        remaining = bytearray(alpha >= 110 for alpha in cell.getchannel('A').getdata())
        components = []
        for start in range(cw * ch):
            if not remaining[start]:
                continue
            remaining[start] = 0
            pending = [start]
            points = []
            left, top, right, bottom = cw, ch, 0, 0
            while pending:
                point = pending.pop()
                points.append(point)
                x, y = point % cw, point // cw
                left, top = min(left, x), min(top, y)
                right, bottom = max(right, x + 1), max(bottom, y + 1)
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        nx, ny = x + dx, y + dy
                        if 0 <= nx < cw and 0 <= ny < ch and remaining[ny * cw + nx]:
                            remaining[ny * cw + nx] = 0
                            pending.append(ny * cw + nx)
            if len(points) > 500:
                components.append((points, (left, top, right, bottom)))
        components = sorted(components, key=lambda entry: len(entry[0]), reverse=True)[:5]
        if len(components) != 5:
            raise ValueError(f'Expected five complete walk sprites in column {col}')
        sprites = []
        for points, bbox in sorted(components, key=lambda entry: entry[1][1]):
            crop = cell.crop(bbox)
            mask = bytearray(crop.width * crop.height)
            for point in points:
                mask[(point // cw - bbox[1]) * crop.width + point % cw - bbox[0]] = 255
            crop.putalpha(Image.frombytes('L', crop.size, bytes(mask)))
            sprites.append((crop, bbox))
        columns.append(sprites)
    result = []
    for row in range(5):
        bounds = [columns[col][row][1] for col in range(4)]
        union = (min(box[0] for box in bounds), min(box[1] for box in bounds),
                 max(box[2] for box in bounds), max(box[3] for box in bounds))
        sprite_w, sprite_h = (56, 48) if row == 0 else (28, 30)
        scale = min((sprite_w - 2) / (union[2] - union[0]), (sprite_h - 1) / (union[3] - union[1]))
        frames = []
        for col, bbox in enumerate(bounds):
            crop = columns[col][row][0]
            resized = crop.resize((max(1, round(crop.width * scale)), max(1, round(crop.height * scale))), Image.LANCZOS)
            resized = alpha_cutout(resized)
            tight = resized.getchannel('A').getbbox()
            resized = resized.crop(tight)
            canvas = Image.new('RGBA', (sprite_w, sprite_h), (0, 0, 0, 0))
            anchor_x = (union[0] + union[2]) / 2
            destination_x = round(sprite_w / 2 + (bbox[0] + bbox[2]) / 2 * scale - anchor_x * scale - resized.width / 2)
            destination_x = max(0, min(sprite_w - resized.width, destination_x))
            canvas.paste(resized, (destination_x, sprite_h - resized.height), resized)
            frames.append(canvas)
        result.append(frames)
    return result


def load_cavalry_walk_frames():
    sheet = Image.open(cavalry_walk_path).convert('RGBA')
    width, height = sheet.size
    crops = []
    for col in range(4):
        cell = sheet.crop((round(width * col / 4), 0, round(width * (col + 1) / 4), height))
        alpha = cell.getchannel('A').point(lambda value: 255 if value >= 110 else 0)
        bbox = alpha.getbbox()
        if not bbox:
            raise ValueError(f'Empty cavalry walk frame in column {col}')
        crop = cell.crop(bbox)
        crops.append(crop)
    scale = min(54 / max(crop.width for crop in crops), 47 / max(crop.height for crop in crops))
    frames = []
    for crop in crops:
        resized = crop.resize((max(1, round(crop.width * scale)), max(1, round(crop.height * scale))), Image.LANCZOS)
        resized = alpha_cutout(resized)
        canvas = Image.new('RGBA', (56, 48), (0, 0, 0, 0))
        canvas.paste(resized, ((56 - resized.width) // 2, 47 - resized.height), resized)
        frames.append(canvas)
    return frames


def recolored_copy(image):
    copy = image.copy()
    pixels = copy.load()
    for y in range(copy.height):
        for x in range(copy.width):
            r, g, b, a = pixels[x, y]
            if not a:
                continue
            hue, saturation, value = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
            if .53 <= hue <= .72 and saturation >= .28:
                r, g, b = [round(c * 255) for c in colorsys.hsv_to_rgb(.01, saturation, value)]
            pixels[x, y] = (r, g, b, a)
    return copy


base = load_base_frames()
walk = load_walk_frames()
cavalry_walk = load_cavalry_walk_frames()
# Keep original idle/attack poses; insert four generated gait frames.
cavalry = []
for team in range(2):
    gait = [recolored_copy(frame) for frame in cavalry_walk] if team else cavalry_walk
    cavalry.append([base[0][team][0], *gait, base[0][team][2], base[0][team][3]])
infantry = [[[base[1][team][archetype], *([recolored_copy(frame) for frame in walk[archetype + 1]] if team else walk[archetype + 1])]
             for archetype in range(4)] for team in range(2)]

all_images = [frame for team in cavalry for frame in team]
all_images += [frame for team in infantry for archetype in team for frame in archetype]
samples = []
for image in all_images:
    samples.extend(pixel[:3] for pixel in image.getdata() if pixel[3])
sample_image = Image.new('RGB', (len(samples), 1))
sample_image.putdata(samples)
palette_image = sample_image.quantize(colors=96, dither=Image.NONE)
raw_palette = palette_image.getpalette()[:96 * 3]
palette = [tuple(raw_palette[i:i + 3]) for i in range(0, len(raw_palette), 3)]

output_dir = ROOT / 'generated' if production_source else ROOT / 'assets/battle' / f'candidate-{version}'
output_dir.mkdir(parents=True, exist_ok=True)
(output_dir / 'battle_palette.inc').write_text(
    '\n'.join('bn::color(' + ','.join(str(round(c * 31 / 255)) for c in rgb) + '),' for rgb in palette) + '\n',
    encoding='ascii')

lines = [f'// Generated by tools/import_battle_sprites.py from {source_path.name} + {walk_path.name} + {cavalry_walk_path.name}.',
         '#include "battle_sprites.h"', 'namespace sg {']
preview = Image.new('RGBA', (448, 5 * 60), (0, 0, 0, 0))
frames_by_kind = [('cavalry', cavalry, 56, 48), ('infantry', infantry, 28, 30)]


def encode(image):
    data = []
    quantized = Image.new('RGBA', image.size, (0, 0, 0, 0))
    for y in range(image.height):
        for x in range(image.width):
            r, g, b, a = image.getpixel((x, y))
            index = 0 if not a else 16 + min(range(len(palette)), key=lambda i: sum((v - p) ** 2 for v, p in zip((r, g, b), palette[i])))
            data.append(index)
            if index:
                quantized.putpixel((x, y), (*palette[index - 16], 255))
    return data, quantized


for kind, teams, sprite_w, sprite_h in frames_by_kind:
    if kind == 'cavalry':
        encoded_teams = []
        for team, frames in enumerate(teams):
            encoded_frames = []
            for frame_index, image in enumerate(frames):
                data, quantized = encode(image)
                quantized.save(ROOT / f'assets/battle/cavalry_sprites-{version}-{team}-{frame_index}.png')
                preview.paste(quantized, (team * 224 + (frame_index % 4) * 56, (frame_index // 4) * 52))
                encoded_frames.append('{' + ','.join(map(str, data)) + '}')
            encoded_teams.append('{' + ','.join(encoded_frames) + '}')
        lines.append(f'alignas(4) const uint8_t cavalry_sprites[2][7][{sprite_w * sprite_h}] = {{' + ','.join(encoded_teams) + '};')
    else:
        encoded_teams = []
        for team, archetypes in enumerate(teams):
            encoded_archetypes = []
            for archetype, frames in enumerate(archetypes):
                encoded_frames = []
                for frame_index, image in enumerate(frames):
                    data, quantized = encode(image)
                    quantized.save(ROOT / f'assets/battle/infantry_sprites-{version}-{team}-{archetype}-{frame_index}.png')
                    preview.paste(quantized, (team * 224 + archetype * 56 + 14, 104 + frame_index * 34))
                    encoded_frames.append('{' + ','.join(map(str, data)) + '}')
                encoded_archetypes.append('{' + ','.join(encoded_frames) + '}')
            encoded_teams.append('{' + ','.join(encoded_archetypes) + '}')
        lines.append(f'alignas(4) const uint8_t infantry_sprites[2][4][5][{sprite_w * sprite_h}] = {{' + ','.join(encoded_teams) + '};')

lines.append('}')
(output_dir / 'battle_sprites.cpp').write_text('\n'.join(lines) + '\n', encoding='utf-8')
preview.resize((896, 600), Image.NEAREST).save(ROOT / f'assets/battle/preview-{version}.png')
print(f'Imported {source_path.name} + {walk_path.name} + {cavalry_walk_path.name}: 4-frame cavalry/infantry gait, 96 character colors')
