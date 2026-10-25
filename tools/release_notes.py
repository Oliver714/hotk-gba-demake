"""Extract matching Chinese and English changelog entries for a tagged release."""
import argparse
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def extract_entry(path, tag):
    """Keep subheadings and lists, stopping at the next level-two heading."""
    entries = []
    current = None
    fence = None
    for line in path.read_text(encoding='utf-8-sig').splitlines():
        marker = re.match(r'^\s{0,3}(`{3,}|~{3,})(.*)$', line)
        if fence:
            if marker and marker[1][0] == fence[0] and len(marker[1]) >= len(fence) and not marker[2].strip():
                fence = None
        elif marker:
            fence = marker[1]
        else:
            heading = re.match(r'^##\s+(.+?)\s*#*\s*$', line)
            if heading:
                if current is not None:
                    entries.append(current)
                title = heading[1]
                version = re.match(r'\[([^\]]+)\]|(\S+)', title)
                current = [version[1] or version[2], []]
                continue
        if current is not None:
            current[1].append(line)
    if current is not None:
        entries.append(current)
    matches = [body for version, body in entries if version == tag]
    if len(matches) != 1:
        raise ValueError(f'{path.name}: expected one entry for {tag}, found {len(matches)}')
    body = '\n'.join(matches[0]).strip()
    if not re.sub(r'<!--.*?-->', '', body, flags=re.S).strip():
        raise ValueError(f'{path.name}: entry for {tag} is empty')
    return body


def render_notes(root, tag):
    if not re.fullmatch(r'v[0-9][A-Za-z0-9._+-]*', tag):
        raise ValueError('Expected a version tag such as v0.2.0 or v0.3.0-rc.1')
    chinese = extract_entry(root / 'CHANGELOG.md', tag)
    english = extract_entry(root / 'CHANGELOG.en.md', tag)
    return f'## 简体中文\n\n{chinese}\n\n## English\n\n{english}\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tag', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        notes = render_notes(ROOT, args.tag)
    except (ValueError, OSError) as error:
        parser.exit(1, f'Release notes: {error}\n')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(notes, encoding='utf-8')
    print(f'Release notes for {args.tag}: {args.output}')


if __name__ == '__main__':
    main()
