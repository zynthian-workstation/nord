from pathlib import Path
import shutil,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from generate_presets import generate
source,destination=map(Path,sys.argv[1:])
shutil.copytree(source,destination,dirs_exist_ok=True)
entries=[['npno-test','Piano.npno','Test Piano','Grand Piano','Grand Piano','npno','fixture.npno'],
 ['nsmp4-test','Sample.nsmp4','Test Sample','Synth Lead','Synth Lead','nsmp4','fixture.nsmp4'],
 ['builtin:pure','Pure','Pure','Pure','Pure','pure','']]
(destination/'catalog.nord').write_bytes(('NORD-LIBRARY-2\0'+'\0'.join(x for e in entries for x in e)+'\0').encode())
# Empty files suffice for preset metadata existence checks; never decoded.
for name in ('fixture.npno','fixture.nsmp4'):(destination/name).write_bytes(b'')
manifest=generate(destination/'catalog.nord',destination)
base=(destination/'manifest.ttl').read_text()
(destination/'manifest.ttl').write_text(base+'\n'+manifest)

