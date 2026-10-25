"""Compile-time localization: translate C++ literals without runtime lookups."""
from pathlib import Path
import hashlib
import json
import re

ROOT = Path(__file__).resolve().parents[1]
TOKENS = re.compile(r'''//[^\n]*|/\*[\s\S]*?\*/|'(?:\\.|[^'\\])*'|"(?:\\.|[^"\\])*"''')
CJK = re.compile(r'[\u3000-\u9fff]')
FORMATS = re.compile(r'%(?!%)(?:[-+ #0]*\d*(?:\.\d+)?(?:hh|ll|[hlzjt])?[diuoxXfFeEgGaAcsp])')


def decode_literal(token):
    raw = token[1:-1]
    raw = re.sub(r'(?:\\x[0-9a-fA-F]{2})+', lambda m: bytes(
        int(value, 16) for value in re.findall(r'\\x([0-9a-fA-F]{2})', m[0])
    ).decode('utf-8', errors='strict'), raw)
    raw = re.sub(r'\\u([0-9a-fA-F]{4})|\\U([0-9a-fA-F]{8})',
                 lambda m: chr(int(m[1] or m[2], 16)), raw)
    return raw.replace('\\n', '\n').replace('\\t', '\t').replace('\\"', '"').replace('\\\\', '\\')


def text_id(source):
    return 'text_' + hashlib.sha256(source.encode('utf-8')).hexdigest()[:16]


def inventory(root=ROOT):
    rows = {}
    files = sorted((root / 'src').glob('*.cpp')) + sorted((root / 'include').glob('*.h'))
    files += [root / 'generated/game_data.cpp']
    for file in files:
        source = file.read_text(encoding='utf-8')
        for match in TOKENS.finditer(source):
            if not match[0].startswith('"'):
                continue
            value = decode_literal(match[0])
            if CJK.search(value):
                line = source[:match.start()].count('\n') + 1
                rows.setdefault(value, []).append(f'{file.relative_to(root).as_posix()}:{line}')
    return rows


def load_catalog():
    data = json.loads((ROOT / 'locales/en.json').read_text(encoding='utf-8'))
    for source, english in data['strings'].items():
        if not isinstance(english, str) or not english.isascii():
            raise ValueError('English must be ASCII: ' + source)
        if FORMATS.findall(source) != FORMATS.findall(english):
            raise ValueError('Format placeholders changed: ' + source)
    return data


def translate_cpp(source, strings, strict=True):
    missing = set()
    def replace(match):
        token = match[0]
        if not token.startswith('"'):
            return token
        value = decode_literal(token)
        if value in strings:
            return json.dumps(strings[value], ensure_ascii=True)
        if CJK.search(value):
            missing.add(value)
        return token
    result = TOKENS.sub(replace, source)
    if strict and missing:
        raise ValueError('Missing English text: ' + repr(sorted(missing)))
    return result, missing


if __name__ == '__main__':
    catalog = load_catalog()
    rows = inventory()
    missing = {source: contexts for source, contexts in rows.items() if source not in catalog['strings']}
    output = ROOT / '.cache/localization'
    output.mkdir(parents=True, exist_ok=True)
    (output / 'missing.json').write_text(json.dumps(missing, ensure_ascii=False, indent=2), encoding='utf-8')
    print(f'English text coverage: {len(rows)-len(missing)}/{len(rows)}; missing: {len(missing)}')
    print('Missing text report:', output / 'missing.json')
