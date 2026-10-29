#!/usr/bin/env python3
"""Validate the public installation contract and a staged Linux LV2 bundle."""
import argparse
import ctypes
import json
import os
from pathlib import Path, PurePosixPath
import re
import struct
import subprocess
import sys

SEMVER = r'(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-(?:0|[1-9]\d*|\d*[A-Za-z-][0-9A-Za-z-]*)(?:\.(?:0|[1-9]\d*|\d*[A-Za-z-][0-9A-Za-z-]*))*)?(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?'

def check(value, message):
    if not value:
        raise ValueError(message)

def manifest(root, tag=None):
    m = json.loads((root/'workstation.json').read_text())
    check(type(m['schema_version']) is int and m['schema_version'] == 1, 'schema_version')
    for key, expected in {'id':'nord', 'name':'Nord', 'repository':'zynthian-workstation/nord',
                          'type':'lv2', 'category':'Workstation',
                          'description':'Uma releitura dos timbres do Nord, usando o Nord Stage 4 como inspiração'}.items():
        check(m[key] == expected, key)
    check(re.fullmatch(SEMVER, m['version']), 'version must be SemVer without v')
    if tag is not None:
        check(tag == m['version'], 'tag differs from workstation.json version')
    cmake = (root/'CMakeLists.txt').read_text()
    version = re.search(r'project\(Nord VERSION ([0-9.]+)', cmake).group(1)
    check(version == m['version'], 'CMake project version differs from manifest')
    plugin = (root/'src/plugin/NordPlugin.cpp').read_text()
    check('d_version('+','.join(version.split('.'))+')' in plugin.replace(' ', ''), 'DSP version differs')
    check(m['plugin'] == {'uri':'urn:nord:instrument', 'bundle':'Nord.lv2',
          'install_path':'/zynthian/zynthian-plugins/lv2/Nord.lv2'}, 'plugin installation identity')
    check(m['platforms'] == [{'os':'linux', 'architecture':'aarch64', 'target':'zynthian-oram',
                            'asset':'Nord-LV2-{version}-linux-aarch64.tar.gz'}], 'release platform/asset')
    i = m['instruments']
    check(i['base_path'] == '/zynthian/zynthian-my-data/soundfonts/nord', 'instrument installation root')
    check(i['formats'] == ['npno', 'nsmp4'], 'supported formats')
    check(isinstance(i['directories'], list) and i['directories'], 'directories')
    check(len(i['directories']) == len(set(i['directories'])), 'duplicate directories')
    for d in i['directories']:
        check(isinstance(d,str) and d and '\\' not in d and '\x00' not in d, 'invalid directory')
        path = PurePosixPath(d)
        check(not path.is_absolute() and all(p not in ('', '.', '..') for p in d.split('/')),
              'directories must be relative and contain no traversal')
        check(not any(ord(c) < 32 for c in d) and ':' not in d, 'malformed directory')
    check(m['integration'] == {'host':'zynthian-oram', 'required':True,
          'components':['engine-registration','dynamic-parameter-pages']}, 'Oram integration')
    engine = json.loads((root/'integrations/zynthian/engine.json').read_text())
    check(engine['URL'] == m['plugin']['uri'] and engine['CAT'] == m['category'], 'Oram registration')
    lock = json.loads((root/'dependencies.lock.json').read_text())
    check(lock['schema_version'] == 2 and len(lock['components']) == 7, 'dependency lock')
    for c in lock['components']:
        check(re.fullmatch('[0-9a-f]{40}', c['commit']) and re.fullmatch('[0-9a-f]{64}',c['archive_sha256']), 'immutable dependency')
        check(c['archive_url'] == c['url'].replace('https://github.com/', 'https://codeload.github.com/')+'/tar.gz/'+c['commit'], 'archive/commit mismatch')
    return m

def bundle(root, path, arch):
    m = manifest(root)
    check(path.name == 'Nord.lv2', 'bundle name')
    for p in path.rglob('*'):
        check(not p.is_symlink(), f'bundle symlink: {p}')
        check(p.suffix.lower() not in ('.npno','.nsmp4','.wav','.aiff','.log','.cpp','.o','.a'), f'unexpected content: {p}')
        check(p.name not in ('instruments','build','__pycache__'), f'development/sample directory: {p}')
    required = ['manifest.ttl','Nord_dsp.ttl','Nord_ui.ttl','Nord_dsp.so','Nord_ui.so','catalog.nord',
                'workstation.json','tools/nord_catalog','tools/nord_host.py','tools/nord_pages.py',
                'tools/configure_zynthian_nord_ui.py','tools/engine.json','tools/refresh_library.py',
                'tools/assemble_bundle.py','tools/generate_presets.py','tools/control_layout.py',
                'licenses/Nord-GPL3.txt','licenses/dependencies.lock.json']
    for f in required:
        check((path/f).is_file(), 'missing bundle component: '+f)
    check(json.loads((path/'workstation.json').read_text()) == m, 'bundled manifest differs')
    for f in ('nord_host.py','nord_pages.py','configure_zynthian_nord_ui.py','engine.json'):
        check((path/'tools'/f).read_bytes() == (root/'integrations/zynthian'/f).read_bytes(), 'Oram payload differs')
    check(os.access(path/'tools/nord_catalog',os.X_OK), 'catalog must be executable')
    expected_machine = {'aarch64':183, 'x86_64':62}[arch]
    for f in ('Nord_dsp.so','Nord_ui.so','tools/nord_catalog'):
        binary = path/f
        h = binary.read_bytes()[:20]
        check(h[:6] == b'\x7fELF\x02\x01' and struct.unpack_from('<H',h,18)[0] == expected_machine, f'wrong ELF architecture: {f}')
        dyn = subprocess.check_output(['readelf','-d',binary],text=True)
        check('(RPATH)' not in dyn and '(RUNPATH)' not in dyn, f'nonportable runtime paths: {f}')
        deps = subprocess.run(['ldd',binary],capture_output=True,text=True,check=True).stdout
        check('not found' not in deps, f'unresolved shared libraries: {f}\n{deps}')
        print(f'{f}:\n{deps.strip()}')
    # Parse every Turtle file and load its resources through the real LV2 host library.
    import rdflib
    graph = rdflib.Graph()
    for ttl in path.rglob('*.ttl'):
        graph.parse(ttl,format='turtle')
    lv = ctypes.CDLL('liblilv-0.so.0')
    ptr = ctypes.c_void_p
    signatures = {
        'lilv_world_new': (ptr, []), 'lilv_world_free': (None, [ptr]),
        'lilv_new_uri': (ptr, [ptr,ctypes.c_char_p]),
        'lilv_world_load_bundle': (None,[ptr,ptr]),
        'lilv_world_get_all_plugins': (ptr,[ptr]),
        'lilv_plugins_get_by_uri': (ptr,[ptr,ptr]),
        'lilv_plugin_get_num_ports': (ctypes.c_uint32,[ptr]),
        'lilv_node_free': (None,[ptr])}
    for name,(result,args) in signatures.items():
        fn=getattr(lv,name);fn.restype=result;fn.argtypes=args
    world=lv.lilv_world_new()
    location=lv.lilv_new_uri(world,(path.resolve().as_uri()+'/').encode())
    uri=lv.lilv_new_uri(world,m['plugin']['uri'].encode())
    try:
        lv.lilv_world_load_bundle(world,location)
        plugin=lv.lilv_plugins_get_by_uri(lv.lilv_world_get_all_plugins(world),uri)
        check(plugin and lv.lilv_plugin_get_num_ports(plugin)==129,'LV2 identity/ports')
    finally:
        lv.lilv_node_free(uri);lv.lilv_node_free(location);lv.lilv_world_free(world)
    sys.path.insert(0,str(root/'tools'))
    from generate_presets import read_catalog
    entries = read_catalog(path/'catalog.nord')
    check(len(entries) == 1 and entries[0]['id'] == 'builtin:pure', 'release must contain Pure only')
    check(len(list((path/'presets').glob('*.ttl'))) == 1, 'unexpected user/sample preset')
    subprocess.run([path/'tools/nord_catalog','--control-layout',os.devnull],check=True)
    print('LV2 bundle, Turtle, URI, 129 ports, Pure preset, Oram and ELF validation PASS')

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('command',choices=['manifest','bundle'])
    p.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[1])
    p.add_argument('--tag')
    p.add_argument('--bundle',type=Path)
    p.add_argument('--architecture',choices=['aarch64','x86_64'])
    a = p.parse_args()
    if a.command == 'manifest':
        m = manifest(a.root,a.tag)
        print(f"workstation.json {m['version']} PASS")
    else:
        check(a.bundle is not None and a.architecture is not None, '--bundle and --architecture required')
        bundle(a.root,a.bundle,a.architecture)

if __name__ == '__main__':
    try:
        main()
    except (ValueError,KeyError,TypeError,AttributeError,subprocess.CalledProcessError) as e:
        print(f'Validation failed: {e}',file=sys.stderr)
        sys.exit(1)
