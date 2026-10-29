"""Canonical beta ABI: verify every exported range, page, priority and preset."""
from pathlib import Path
import re,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from control_layout import read_layout
from generate_presets import read_catalog
bundle=Path(sys.argv[1]);text=(bundle/'Nord_dsp.ttl').read_text();rows=read_layout(bundle/'tools/control-layout.tsv')
assert len(rows)==125 and len({r['group'] for r in rows})==36
ports={int(m[1]):m[2] for m in re.finditer(r'lv2:index (\d+) ;(.*?)(?=\n    \])',text,re.S)}
assert set(ports)==set(range(129))
for row in rows:
 port=ports[row['index']+4];assert f'lv2:symbol "{row["symbol"]}"' in port
 for name in ['minimum','maximum','default']:
  value=float(re.search('lv2:'+name+r' ([\d.eE+-]+)',port)[1]);assert abs(value-row[name])<max(1e-5,abs(value)*1e-6),(row,name,value)
 assert f'pprops:displayPriority {10000-row["index"]}' in port
 assert f'lv2:displayPriority {10000-row["index"]}' in port
 assert f'#portGroup_nord_group_{row["group"]:02d}>' in port
 assert 'notOnGUI' not in port and 'NonAutomatable' not in port
for group in range(36):
 assert f'<urn:nord:instrument#portGroup_nord_group_{group:02d}>\n    a pg:InputGroup, param:ControlGroup' in text
expected={'General':['gain','transpose','pitch_bend_up','pitch_bend_down'],'Filter':['synth_filter_mode','synth_cutoff_hz','synth_resonance','stage_drive']}
for title,symbols in expected.items():assert [r['symbol'] for r in rows if r['title']==title]==symbols
assert [(r['minimum'],r['maximum'],r['default']) for r in rows if r['symbol'] in ('pitch_bend_up','pitch_bend_down')]==[(0,24,2),(-24,0,-2)]
entries=read_catalog(bundle/'catalog.nord');assert {e['format'] for e in entries}=={'npno','nsmp4','pure'}
for e in entries:
 preset=(bundle/'presets'/f'nord-{e["id"]}.ttl').read_text();values=re.findall(r'lv2:symbol "([^"]+)" ; pset:value ([\d.eE+-]+)',preset)
 assert len(values)==125 and dict(values).keys()=={r['symbol'] for r in rows}
 for row in rows:assert abs(float(dict(values)[row['symbol']])-row['default'])<1e-5
print('125 canonical endpoints, 36 dense functional pages, full presets, both priority spellings PASS')
