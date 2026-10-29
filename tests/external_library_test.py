import sys, pathlib, tempfile, subprocess, os, shutil, hashlib
sys.path.insert(0,str(pathlib.Path(__file__).parents[1]/'tools'))
from generate_presets import read_catalog as read_all_catalog, generate
def read_catalog(path):return [x for x in read_all_catalog(path) if x['format']!='pure']
from assemble_bundle import assemble_external
# Small structurally valid fixtures, no real PCM decode required for discovery.
def fixture(path,value):
    path.parent.mkdir(parents=True,exist_ok=True)
    data=bytearray(4260);data[:4]=b'CBIN';data[8:12]=b'npno';data[16:20]=b'CNSP'
    for off,v in [(0x61e,1),(0x620,2),(0x622+120,2)]:data[16+off:18+off]=v.to_bytes(2,'big')
    data[-1]=value;path.write_bytes(data)
with tempfile.TemporaryDirectory(prefix='nord-external-') as d:
    d=pathlib.Path(d);root=d/'instruments';root.mkdir();catalog=d/'catalog.nord'
    def refresh():
        subprocess.run([sys.argv[1],str(root),str(catalog),'--external'],check=True,capture_output=True)
        return read_catalog(catalog)
    assert refresh()==[]
    denied=d/'denied';denied.mkdir();denied.chmod(0)
    # CI containers run as root, which bypasses Unix mode bits. Exercise this
    # rejection as nobody there, preserving the permission assertion.
    credentials={}
    if os.geteuid()==0:
        d.chmod(0o777);credentials={'user':65534,'group':65534}
    result=subprocess.run([sys.argv[1],str(denied),str(d/'denied.nord'),'--external'],capture_output=True,**credentials)
    d.chmod(0o700)
    denied.chmod(0o700)
    assert result.returncode!=0 and not (d/'denied.nord').exists()
    result=subprocess.run([sys.argv[1],str(d/'missing'),str(d/'missing.nord'),'--external'],capture_output=True)
    assert result.returncode!=0

    fixture(root/'Organ/A XL 1.0.NPNO',1)
    fixture(root/'Future/New Category/Árvore 日本.npno',2)
    (root/'invalid.npno').write_bytes(b'bad')
    (root/'unknown.nsmp4').write_bytes(b'unsupported')
    first=refresh();assert len(first)==2
    assert all(pathlib.Path(i['path']).is_absolute() and i['format']=='npno' for i in first)
    assert {i['display_category'] for i in first}=={'Organ','Future'}
    data=catalog.read_bytes();assert refresh()==first and catalog.read_bytes()==data
    # No JSON engine associations; category Organ still maps NPNO to piano.
    original=root/'Organ/A XL 1.0.NPNO';identity=next(i['id'] for i in first if i['category']=='Organ')
    moved=root/'New Category/A XL 1.0.npno';moved.parent.mkdir();original.rename(moved)
    assert identity in {i['id'] for i in refresh()}
    # Equal size+mtime replacement still invalidates via ctime/inode.
    st=moved.stat();data=bytearray(moved.read_bytes());data[-1]=99;moved.write_bytes(data)
    os.utime(moved,ns=(st.st_atime_ns,st.st_mtime_ns))
    assert identity not in {i['id'] for i in refresh()}
    fixture(root/'Copy.npno',99);entries=refresh();assert len(entries)==2 # identical copy deduplicates
    bundle=d/'Nord.lv2';bundle.mkdir();real=pathlib.Path(sys.argv[2]);shutil.copy2(real/'Nord_dsp.ttl',bundle/'Nord_dsp.ttl');shutil.copytree(real/'tools',bundle/'tools');generate(catalog,bundle)
    assert len(list((bundle/'presets').glob('*.ttl')))==3
    # Regenerate actual bundle metadata independently of the real compiled binary.
    real=pathlib.Path(sys.argv[2]);shutil.copy2(real/'Nord_dsp.so',bundle/'Nord_dsp.so')
    (bundle/'manifest.ttl').write_text('<urn:nord:instrument> a <http://lv2plug.in/ns/lv2core#Plugin> .\n')
    digest=hashlib.sha256((bundle/'Nord_dsp.so').read_bytes()).hexdigest()
    assemble_external(pathlib.Path(sys.argv[1]),root,bundle)
    fixture(root/'Category Added Later/New.npno',100)
    assemble_external(pathlib.Path(sys.argv[1]),root,bundle)
    assert len(read_catalog(bundle/'catalog.nord'))==3
    assert hashlib.sha256((bundle/'Nord_dsp.so').read_bytes()).hexdigest()==digest
    (root/'Category Added Later/New.npno').unlink()
    moved.unlink();(root/'Copy.npno').unlink();assert len(refresh())==1
    # Removal and metadata update require no compiler or DSP changes.
    print('External categories, cached identities, replacement, rename, unsupported/invalid, presets PASS')
