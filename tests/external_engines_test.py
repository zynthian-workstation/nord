"""Optional real-instrument regression; supply a bundle with an external catalog."""
from v090_pure_e2e import *
# On one actual LV2 instance switch Pure / real NSMP4 / real NPNO / Pure.
h=Host(bundle,'Nord');entries=read_catalog(bundle/'catalog.nord')
for identity in ['builtin:pure',next(x['id'] for x in entries if 'SineSaw Lead_NW2 CL' in x['name']),next(x['id'] for x in entries if 'CP80' in x['name']),'builtin:pure']:
 h.load(identity);a=h.run([(0x90,60,100)]);a+=sum([h.run() for _ in range(10)],[]);assert max(map(abs,a))>1e-8;record('switch_source',identity=identity)
h.close()
# NSMP4 independent of all Pure parameters.
id=next(x['id'] for x in entries if 'SineSaw Lead_NW2 CL' in x['name']);a,_=render(identity=id);b,_=render({'pure_osc1_waveform':6,'pure_osc2_enabled':1,'pure_osc2_octave':2},identity=id);assert np.array_equal(a,b);record('NSMP4_pure_ports_invariance',bitwise_equal=True)
print(f'{len(results)} Pure real LV2 checks PASS')
