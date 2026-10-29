"""Run the actual installed Oram controller/transport with generated LV2 labels."""
import ast,json,logging,math,re,sys,time,types,traceback
from pathlib import Path
root=Path(__file__).resolve().parents[1];fixture=root/'tests/fixtures/zynthian-oram-pi-37b776a';bundle=Path(sys.argv[1]);text=(bundle/'Nord_dsp.ttl').read_text()
def selected(path,classname,methods,namespace):
 tree=ast.parse(path.read_text());cls=next(n for n in tree.body if isinstance(n,ast.ClassDef) and n.name==classname)
 if methods is not None:cls.body=[n for n in cls.body if isinstance(n,ast.FunctionDef) and n.name in methods]
 cls.bases=[];cls.decorator_list=[];ns=dict(namespace);exec(compile(ast.fix_missing_locations(ast.Module(body=[cls],type_ignores=[])),str(path),'exec'),ns);return ns[classname]
Controller=selected(fixture/'controller.py.txt','zynthian_controller',None,dict(logging=logging,math=math,time=time.time))
metadata={};rows=[]
for block in text.split('lv2:index ')[1:]:
 index=int(block.split(';')[0]);m=re.search(r'lv2:symbol "(pitch_bend_(?:up|down))"',block)
 if not m:continue
 symbol=m[1];points=[dict(value=float(value),label=label) for label,value in re.findall(r'rdfs:label\s+"([^"]+)" ;\s*rdf:value ([\d.eE+-]+)',block)]
 assert len(points)==25,(symbol,points)
 get=lambda name:float(re.search('lv2:'+name+r' ([\d.eE+-]+)',block)[1])
 metadata[index]=dict(symbol=symbol,name='Pitch Bend Range '+symbol.rsplit('_',1)[1],group_symbol='general',group_name='General',group_display_priority=1,display_priority=1,value=get('default'),range={'min':get('minimum'),'max':get('maximum')},scale_points=points,is_toggled=False,is_trigger=False,is_integer=True,is_logarithmic=False,is_path=False,not_on_gui=False)
Engine=selected(fixture/'jalv.py.txt','zynthian_engine_jalv',{'get_lv2_controllers_dict','send_controller_value','proc_parse_ctrl_value'},dict(logging=logging,traceback=traceback,zynthian_controller=Controller,zynthian_lv2=types.SimpleNamespace(get_plugin_ports=lambda uri:metadata)))
e=Engine();e.plugin_url='urn:nord:instrument';e.plugin_name='Nord';e.dsp56300_plugins={};e.commands=[];e.proc_cmd=e.commands.append;e.lv2_zctrl_dict=e.get_lv2_controllers_dict()
for ordinal,symbol in enumerate(['pitch_bend_up','pitch_bend_down'],2):
 c=e.lv2_zctrl_dict[symbol];assert c.midi_cc is None and c.graph_path is None
 for tick in c.ticks:
  c.set_value(tick,False);expected=f'{int(tick):+d}' if symbol.endswith('_up') else str(int(tick));assert c.get_value2label()==expected
 c.set_value(2 if symbol.endswith('_up') else -2,False)
 # Native feedback indices are Jalv control ordinals (not LV2 port indices).
 e.proc_parse_ctrl_value(f'{ordinal}#{symbol}={c.value}')
 assert c.graph_path==ordinal
 c.nudge(10 if symbol.endswith('_up') else -6)
 value=12 if symbol.endswith('_up') else -8
 assert c.value==value and e.commands[-1]==f'set {ordinal} {value:.6f}'
 # Before any feedback, Oram correctly falls back to exact symbol assignment.
 c.graph_path=None;c.set_value(3 if symbol.endswith('_up') else -3)
 assert e.commands[-1]==f'{symbol}={c.value:.6f}'
 rows.append(dict(symbol=symbol,minimum=c.value_min,maximum=c.value_max,labels=c.labels,ordinal=ordinal,commands=e.commands[-2:]))
print(json.dumps(rows,indent=2));print('Installed Oram: signed labels, encoder nudges and native transport PASS')
