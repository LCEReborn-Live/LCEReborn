"""Import original localized trophy metadata and PNGs from an extracted local TRP.

Input XML must already be decrypted. Existing achievements and language keys
are retained; the 17 TU80 entries use an appended, fixed ID range.
"""
import argparse
import json
from pathlib import Path
import xml.etree.ElementTree as ET

from import_common_update import integer, string, read_archive, write_archive, read_languages

LOCALES = {
    'ja-JP': 0, 'en-EN': 1, 'en-GB': 20, 'fr-FR': 2, 'es-ES': 3, 'de-DE': 4,
    'it-IT': 5, 'nl-NL': 6, 'pt-PT': 7, 'ru-RU': 8, 'ko-KR': 9, 'zh-CHT': 10,
    'zh-HANT': 10, 'zh-CN': 11, 'zh-CHS': 11, 'fi-FI': 12, 'sv-SV': 13,
    'sv-SE': 13, 'da-DA': 14, 'da-DK': 14, 'no-NO': 15, 'nb-NO': 15,
    'pl-PL': 16, 'pt-BR': 17, 'tr-TR': 19,
}


def encode_languages(languages):
    ids = sorted(next(iter(languages.values())))
    sections = {}
    for name, values in languages.items():
        if set(values) != set(ids):
            raise ValueError('inconsistent localization IDs')
        sections[name] = (integer(2) + b'\x01' + string(name) + integer(len(ids))
                          + b''.join(string(values[key]) for key in ids))
    return (integer(2) + integer(len(sections)) + b'\x01' + integer(len(ids))
            + b''.join(integer(key) for key in ids)
            + b''.join(string(name) + integer(len(data)) for name, data in sections.items())
            + b''.join(sections.values()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--trophies', type=Path, required=True)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    archive = read_archive(args.archive.read_bytes())
    languages = read_languages(archive[b'languages.loc'])
    default = ET.parse(args.trophies / 'TROP.xml').getroot()
    expected = {int(t.get('id')): t for t in default.findall('trophy')}
    report = []
    for name, values in languages.items():
        locale = LOCALES.get(name.decode())
        path = args.trophies / f'TROP_{locale:02}.xml' if locale is not None else None
        root = ET.parse(path).getroot() if path is not None and path.is_file() else default
        trophies = {int(t.get('id')): t for t in root.findall('trophy')}
        for trophy_id in range(94, 111):
            trophy = trophies.get(trophy_id, expected[trophy_id])
            for offset, field in enumerate(('name', 'detail')):
                key = 0x7f008000 + (trophy_id - 94) * 2 + offset
                value = trophy.findtext(field).encode('utf-8')
                if key in values and values[key] != value:
                    raise ValueError('localization ID collision')
                values[key] = value
    for trophy_id in range(94, 111):
        icon = (args.trophies / f'TROP{trophy_id:03}.PNG').read_bytes()
        if not icon.startswith(b'\x89PNG\r\n\x1a\n'):
            raise ValueError('invalid trophy PNG')
        archive[f'Graphics\\Achievements\\TU80_TROP{trophy_id:03}.png'.encode()] = icon
        report.append(dict(trophy_id=trophy_id, game_id=trophy_id + 3,
                           name=expected[trophy_id].findtext('name'),
                           description=expected[trophy_id].findtext('detail'),
                           trigger_implemented=trophy_id in (94, 95, 96, 100, 101, 102, 104)))
    archive[b'languages.loc'] = encode_languages(languages)
    assert read_languages(archive[b'languages.loc']) == languages
    payload = write_archive(archive)
    assert read_archive(payload) == archive
    args.output.write_bytes(payload)
    args.output.with_suffix('.achievements.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(f'Imported {len(report)} trophies, {len(languages)} locales')


if __name__ == '__main__':
    main()
