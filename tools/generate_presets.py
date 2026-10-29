#!/usr/bin/env python3
"""Generate LV2 State presets from the build-time catalog (no discovery)."""
import json
import re
from pathlib import Path
from urllib.parse import quote

PREFIX = '''@prefix lv2: <http://lv2plug.in/ns/lv2core#> .
@prefix pset: <http://lv2plug.in/ns/ext/presets#> .
@prefix state: <http://lv2plug.in/ns/ext/state#> .
@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .
'''


def read_catalog(path):
    fields = Path(path).read_bytes().decode('utf-8').split('\0')
    if fields[0] not in ('NORD-CATALOG-1', 'NORD-LIBRARY-2') or fields[-1] or (len(fields)-2) % 7:
        raise ValueError('Invalid static catalog')
    return [dict(zip(('id', 'original', 'name', 'category', 'display_category', 'format', 'path'), fields[i:i+7]))
            for i in range(1, len(fields)-1, 7)]


def generate(catalog, bundle, *, banks=False, categories=(), metadata=None, defaults_file=None):
    entries = read_catalog(catalog)
    presets = bundle / 'presets'
    if presets.is_symlink():
        raise ValueError('Presets directory must not be a symlink')
    presets.mkdir(parents=True, exist_ok=True)
    expected = set()
    manifest = PREFIX
    ids = set()
    bank_categories = sorted(set(categories) | {item['category'] for item in entries}) if banks else []
    def bank_uri(category):
        return 'urn:nord:bank:' + quote(category, safe='')
    for category in bank_categories:
        manifest += f'<{bank_uri(category)}> a pset:Bank ; lv2:appliesTo <urn:nord:instrument> ; rdfs:label {json.dumps(category, ensure_ascii=False)} .\n'
    defaults_file = defaults_file or bundle / 'tools' / 'synth-parameters.tsv'
    metadata = metadata or bundle / 'Nord_dsp.ttl'
    neutral=[]
    # Factory beta presets contain the COMPLETE canonical parameter set. This
    # avoids carrying another engine's Drive/OSC/MIDI ranges into the next sound.
    if not metadata.is_file():raise ValueError('Missing plugin metadata')
    for block in metadata.read_text().split('lv2:index ')[1:]:
        symbol=re.search(r'lv2:symbol "([^"]+)"',block)
        value=re.search(r'lv2:default ([\d.eE+-]+)',block)
        if symbol and value:neutral.append(f'[ lv2:symbol "{symbol[1]}" ; pset:value {value[1]} ]')
    neutral_ports=(' ;\n lv2:port '+',\n '.join(neutral)) if neutral else ''
    for item in entries:
        identity = item['id']
        if identity in ids:
            raise ValueError('Duplicate preset ID')
        ids.add(identity)
        if item['format'] != 'pure' and (item['format'] not in ('npno','nsmp4') or not (bundle / item['path']).is_file()):
            raise ValueError(f"Missing or unsupported instrument: {item['path']}")
        filename = f'nord-{identity}.ttl'
        expected.add(filename)
        uri = f'urn:nord:preset:{identity}'
        label = item['display_category'] + ' - ' + item['name']
        ports = neutral_ports
        bank = f' ;\n pset:bank <{bank_uri(item["category"])}>' if banks else ''
        ttl = PREFIX + f'''<{uri}> a pset:Preset ;
 lv2:appliesTo <urn:nord:instrument> ;
 rdfs:label {json.dumps(label, ensure_ascii=False)} ;
 state:state [ <urn:nord:instrument#instrument_id> "{identity}" ]{ports}{bank} .
'''
        if (presets / filename).is_symlink():
            (presets / filename).unlink()
        temporary = presets / (filename + '.tmp')
        temporary.write_text(ttl, encoding='utf-8')
        temporary.replace(presets / filename)
        manifest += f'<{uri}> a pset:Preset ; lv2:appliesTo <urn:nord:instrument> ; rdfs:seeAlso <presets/{filename}> .\n'
    for stale in presets.glob('nord-*.ttl'):
        if stale.name not in expected:
            stale.unlink()
    return manifest
