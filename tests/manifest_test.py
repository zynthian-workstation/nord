"""Reject release/version mismatches and unsafe Manager directory contracts."""
import copy
import importlib.util
import json
from pathlib import Path
import shutil
import tempfile
root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('validate',root/'ci/validate.py')
v=importlib.util.module_from_spec(spec);spec.loader.exec_module(v)
m=v.manifest(root,json.loads((root/'workstation.json').read_text())['version'])
assert len(m['instruments']['directories'])==18
assert {'Organ','Orquestral','Guitar_Plucked','Voices_Choir','E Grand Piano'} <= set(m['instruments']['directories'])
with tempfile.TemporaryDirectory() as directory:
    temp=Path(directory)
    for p in ('CMakeLists.txt','src/plugin/NordPlugin.cpp','dependencies.lock.json','integrations/zynthian/engine.json'):
        (temp/p).parent.mkdir(parents=True,exist_ok=True);shutil.copy2(root/p,temp/p)
    def rejected(data,tag=None):
        (temp/'workstation.json').write_text(json.dumps(data))
        try:v.manifest(temp,tag)
        except (ValueError,KeyError,TypeError):return
        raise AssertionError((data,tag))
    for value in ['/tmp','../escape','safe/../../escape','a/../b','a//b','a/./b','a\\b','','x\x00y','file\nname','C:bad']:
        data=copy.deepcopy(m);data['instruments']['directories']=[value];rejected(data)
    rejected(m,'v'+m['version']);rejected(m,'9999.0.0')
    for value in ('v1.0.0','01.0.0','1.0','1.0.0-01'):
        data=copy.deepcopy(m);data['version']=value;rejected(data)
print('Manifest identity, SemVer, immutable lock, traversal and tag mismatch PASS')
