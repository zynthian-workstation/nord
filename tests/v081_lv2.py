"""Small real LV2 host used for descriptor/state and end-to-end audio evidence."""
import ctypes as C
import struct,time,re,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from generate_presets import read_catalog
P=C.c_void_p; U=C.c_uint32
class Feature(C.Structure): _fields_=[('uri',C.c_char_p),('data',P)]
MapFn=C.CFUNCTYPE(U,P,C.c_char_p)
UnmapFn=C.CFUNCTYPE(P,P,U)
class Unmap(C.Structure): _fields_=[('handle',P),('unmap',UnmapFn)]
class Map(C.Structure): _fields_=[('handle',P),('map',MapFn)]
ScheduleFn=C.CFUNCTYPE(C.c_int,P,U,P)
class Option(C.Structure): _fields_=[('context',U),('subject',U),('key',U),('size',U),('type',U),('value',P)]
class Worker(C.Structure): _fields_=[('handle',P),('schedule',ScheduleFn)]
Instantiate=C.CFUNCTYPE(P,P,C.c_double,C.c_char_p,C.POINTER(C.POINTER(Feature)))
Connect=C.CFUNCTYPE(None,P,U,P); Action=C.CFUNCTYPE(None,P); Run=C.CFUNCTYPE(None,P,U); Extension=C.CFUNCTYPE(P,C.c_char_p)
class Descriptor(C.Structure): _fields_=[('uri',C.c_char_p),('instantiate',Instantiate),('connect',Connect),('activate',Action),('run',Run),('deactivate',Action),('cleanup',Action),('extension',Extension)]
Retrieve=C.CFUNCTYPE(P,P,U,C.POINTER(C.c_size_t),C.POINTER(U),C.POINTER(U))
Store=C.CFUNCTYPE(C.c_int,P,U,P,C.c_size_t,U,U)
Restore=C.CFUNCTYPE(C.c_int,P,Retrieve,P,U,C.POINTER(C.POINTER(Feature)))
Save=C.CFUNCTYPE(C.c_int,P,Store,P,U,C.POINTER(C.POINTER(Feature)))
class State(C.Structure):_fields_=[('save',Save),('restore',Restore)]
class Host:
 def __init__(self,bundle,name,rate=44100,block=256,panel=None):
  self.bundle=Path(bundle).resolve();self.rate=rate;self.block=block;self.frames=0;self.urids={};self.saved={};self.statebuffers={};self.ui_atoms=[]
  self.mapfn=MapFn(lambda _,s:self.map(s.decode()));self.mapper=Map(None,self.mapfn)
  self.unmap_buffers={}
  def unmap(_,urid):
   if urid not in self.unmap_buffers:
    key=next((key for key,value in self.urids.items() if value==urid),None)
    if key is None:return None
    self.unmap_buffers[urid]=C.create_string_buffer(key.encode())
   return C.addressof(self.unmap_buffers[urid])
  self.unmapfn=UnmapFn(unmap);self.unmapper=Unmap(None,self.unmapfn)
  self.schedule=ScheduleFn(lambda *_:1);self.worker=Worker(None,self.schedule)
  self.maxblock=C.c_int(block);self.srate=C.c_float(rate);self.options=(Option*3)(Option(0,0,self.map('http://lv2plug.in/ns/ext/buf-size#maxBlockLength'),4,self.map('http://lv2plug.in/ns/ext/atom#Int'),C.cast(C.pointer(self.maxblock),P)),Option(0,0,self.map('http://lv2plug.in/ns/ext/parameters#sampleRate'),4,self.map('http://lv2plug.in/ns/ext/atom#Float'),C.cast(C.pointer(self.srate),P)),Option())
  self.fs=[Feature(b'http://lv2plug.in/ns/ext/urid#unmap',C.cast(C.pointer(self.unmapper),P)),Feature(b'http://lv2plug.in/ns/ext/options#options',C.cast(self.options,P)),Feature(b'http://lv2plug.in/ns/ext/urid#map',C.cast(C.pointer(self.mapper),P)),Feature(b'http://lv2plug.in/ns/ext/worker#schedule',C.cast(C.pointer(self.worker),P))]
  self.features=(C.POINTER(Feature)*5)(*(C.pointer(x) for x in self.fs),None)
  self.module=C.CDLL(str(self.bundle/(name+'_dsp.so')),mode=C.RTLD_LOCAL);self.module.lv2_descriptor.restype=C.POINTER(Descriptor)
  self.d=self.module.lv2_descriptor(0).contents;self.uri=self.d.uri.decode()
  text=(self.bundle/(name+'_dsp.ttl')).read_text();self.ports={};self.controls={}
  for b in text.split('lv2:index ')[1:]:
   index=int(b.split(';')[0]);symbol=re.search(r'lv2:symbol "([^"]+)"',b);default=re.search(r'lv2:default ([\d.eE+-]+)',b)
   if symbol and default:self.ports[symbol[1]]=index;self.controls[symbol[1]]=C.c_float(float(default[1]))
  self.instance=self.d.instantiate(C.byref(self.d),rate,(str(self.bundle)+'/').encode(),self.features)
  assert self.instance,'instantiate'
  self.left=(C.c_float*block)();self.right=(C.c_float*block)();self.input=C.create_string_buffer(8192);self.output=C.create_string_buffer(8192)
  for i,b in enumerate((self.left,self.right,self.input,self.output)):self.d.connect(self.instance,i,C.cast(b,P))
  for symbol,value in self.controls.items():self.d.connect(self.instance,self.ports[symbol],C.cast(C.pointer(value),P))
  self.all_controls=self.controls
  if panel:
   names={index:symbol for symbol,index in self.ports.items()};logical={};ports={}
   for line in (self.bundle/'tools/panel-controls.tsv').read_text().splitlines():
    kind,symbol,param=line.split('\t')
    if kind==panel:
     physical=int(param)+4;logical[symbol]=self.all_controls[names[physical]];ports[symbol]=physical
   self.controls=logical;self.ports=ports
  self.state=C.cast(self.d.extension(b'http://lv2plug.in/ns/ext/state#interface'),C.POINTER(State)).contents
  self.d.activate(self.instance)
 def map(self,s):
  if s not in self.urids:self.urids[s]=len(self.urids)+1
  return self.urids[s]
 def set(self,**values):
  for k,v in values.items():self.controls[k].value=v
 def restore(self,identity):
  key=self.map(self.uri+'#instrument_id');buffer=C.create_string_buffer(identity.encode()+b'\0');self.statebuffers[key]=buffer
  def retrieve(_,k,size,typ,flags):
   if k not in self.statebuffers:return None
   b=self.statebuffers[k];size[0]=len(b)-1;typ[0]=self.map('http://lv2plug.in/ns/ext/atom#String');flags[0]=3;return C.addressof(b)
  callback=Retrieve(retrieve);assert self.state.restore(self.instance,callback,None,0,self.features)==0
 def save(self):
  result={}
  def store(_,key,value,size,typ,flags):result[key]=C.string_at(value,size);return 0
  callback=Store(store);assert self.state.save(self.instance,callback,None,0,self.features)==0
  return result
 def run(self,midi=()):
  data=bytearray(struct.pack('IIII',8+24*len(midi),self.map('http://lv2plug.in/ns/ext/atom#Sequence'),0,0))
  for event in midi:
   payload=bytes(event);data+=struct.pack('qII',0,len(payload),self.map('http://lv2plug.in/ns/ext/midi#MidiEvent'))+payload+bytes(8-len(payload))
  for atom in self.ui_atoms:
   data+=struct.pack('q',0)+atom+bytes((-len(atom))%8)
  self.ui_atoms.clear();struct.pack_into('I',data,0,len(data)-8)
  C.memmove(self.input,bytes(data),len(data));struct.pack_into('I',self.output,0,8192)
  self.d.run(self.instance,self.block);self.frames+=self.block
  return list(self.left)
 def load(self,identity):
  self.restore(identity)
  for _ in range(10000):
   if max(map(abs,self.run([(0x90,60,100)])))>1e-8:break
   time.sleep(.005)
  else:raise AssertionError('load timeout')
  self.run([(0xb0,120,0)])
  for _ in range(4):self.run()
 def close(self):
  if self.instance:self.d.deactivate(self.instance);self.d.cleanup(self.instance);self.instance=None
if __name__=='__main__':
 bundle=Path(sys.argv[1]);entries=read_catalog(bundle/'catalog.nord');h=Host(bundle,'Nord')
 assert h.uri=='urn:nord:instrument' and not h.module.lv2_descriptor(1)
 assert len(h.controls)==125
 for fmt in ['npno','nsmp4','npno']:
  entry=next(x for x in entries if x['format']==fmt);h.load(entry['id'])
  assert any(entry['id'].encode() in value for value in h.save().values())
  print('Same NORD instance',fmt,'PCM and State PASS')
 saved=h.save();values={k:v.value for k,v in h.controls.items()};h.close()
 h=Host(bundle,'Nord');h.set(**values);h.load(entry['id']);assert h.save()==saved;h.close()
 print('One descriptor, static 125 controls, NPNO/NSMP4/NPNO switching and recreation PASS')
