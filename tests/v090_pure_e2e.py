"""Real LV2 MIDI/control-port capture. Numerical evidence, no auditory approval."""
from v081_lv2 import Host,read_catalog
from pathlib import Path
import sys,json,math,time,tempfile,os,shutil
import numpy as np
bundle=Path(sys.argv[1]).resolve();report=Path(sys.argv[2]);results=[]
def record(name,**data):
 results.append(dict(test=name,**data));report.write_text(json.dumps(results,indent=2)+'\n')
def rms(a):return float(np.sqrt(np.mean(np.asarray(a)**2)))
def diff(a,b):return rms(np.asarray(a)-b)/max(rms(a),1e-12)
def render(values=None,note=60,rate=48000,block=256,duration=.8,changes=(),events=(),identity='builtin:pure'):
 h=Host(bundle,'Nord',rate,block);h.load(identity);h.set(**(values or {}));out=[]
 for i in range(math.ceil(duration*rate/block)):
  t=i*block/rate;prev=(i-1)*block/rate
  for at,v in changes:
   if prev<at<=t:h.set(**v)
  midi=[(0x90,note,100)] if i==0 else []
  for at,e in events:
   if prev<at<=t:midi+=e
  out+=h.run(midi)
 saved=h.save();h.close();a=np.asarray(out);assert np.isfinite(a).all();return a,saved

def f0(a,rate,expected):
 x=a[int(.25*rate):];n=len(x);s=np.abs(np.fft.rfft(x*np.hanning(n)));freq=np.fft.rfftfreq(n,1/rate)
 inds=np.where((freq>expected*.85)&(freq<expected*1.15))[0];i=int(inds[np.argmax(s[inds])]);v=np.log(np.maximum(s[i-1:i+2],1e-20));d=.5*(v[0]-v[2])/(v[0]-2*v[1]+v[2]);return (i+d)*rate/n
# Catalog without any instruments, including loadCatalog fallback, real LV2 playback.
with tempfile.TemporaryDirectory(prefix='nord-pure-empty-') as d:
 path=Path(d)/'catalog.nord';path.write_bytes(b'NORD-LIBRARY-2\0');old=os.environ.get('NORD_LIBRARY_CATALOG');os.environ['NORD_LIBRARY_CATALOG']=str(path)
 a,_=render();assert rms(a)>.01
 if old is None:del os.environ['NORD_LIBRARY_CATALOG']
 else:os.environ['NORD_LIBRARY_CATALOG']=old
 record('empty_catalog_builtin_audio',rms=rms(a))
h=Host(bundle,'Nord');assert len(h.controls)==125 and not h.module.lv2_descriptor(1)
symbols=[s for s in h.controls if s.startswith('pure_')];assert len(symbols)==11
record('ABI',uri=h.uri,plugins=1,controls=125,pure_ports={s:h.ports[s] for s in symbols});h.close()
for rate,block in [(44100,255),(48000,256)]:
 for note in [36,60,84]:
  a,_=render(note=note,rate=rate,block=block);expected=440*2**((note-69)/12);hz=f0(a,rate,expected);assert abs(1200*math.log2(hz/expected))<3
  record('saw_pitch',rate=rate,block=block,note=note,hz=hz,expected=expected,peak=float(max(abs(a))))
 base,_=render(rate=rate,block=block)
 # Each of the exact 11 public endpoints exercised through LV2 ports.
 for symbol,value,setup in [
  ('pure_osc1_waveform',0,{}),('pure_osc1_octave',1,{}),('pure_osc1_semitone',7,{}),('pure_osc1_detune_cents',50,{}),('pure_osc1_level',.5,{}),
  ('pure_osc2_enabled',1,{}),('pure_osc2_waveform',0,{'pure_osc2_enabled':1}),('pure_osc2_octave',1,{'pure_osc2_enabled':1}),('pure_osc2_semitone',7,{'pure_osc2_enabled':1}),('pure_osc2_detune_cents',0,{'pure_osc2_enabled':1}),('pure_osc2_level',.5,{'pure_osc2_enabled':1})]:
  a,_=render(setup,rate=rate,block=block);b,_=render(dict(setup,**{symbol:value}),rate=rate,block=block);delta=diff(a,b);assert delta>.005,(symbol,delta);record('control_audio',symbol=symbol,rate=rate,difference=delta)
  if symbol=='pure_osc1_level':assert abs(rms(b)/rms(a)-.5)<.001
 for osc in [1,2]:
  setup={'pure_osc1_level':0,'pure_osc2_enabled':1,'pure_osc2_detune_cents':0} if osc==2 else {}
  for control,value,semi in [('octave',-1,-12),('semitone',7,7),('detune_cents',50,.5)]:
   a,_=render(dict(setup,**{f'pure_osc{osc}_{control}':value}),rate=rate,block=block);expected=261.625565*2**(semi/12);hz=f0(a,rate,expected);assert abs(1200*math.log2(hz/expected))<3;record('osc_transpose',osc=osc,control=control,value=value,rate=rate,hz=hz)
 off,_=render({'pure_osc2_waveform':6,'pure_osc2_octave':2,'pure_osc2_detune_cents':-100,'pure_osc2_level':0},rate=rate,block=block);assert np.array_equal(base,off)
 silent,_=render({'pure_osc1_level':0},rate=rate,block=block);assert max(abs(silent))==0
 bothzero,_=render({'pure_osc1_level':0,'pure_osc2_enabled':1,'pure_osc2_level':0},rate=rate,block=block);assert max(abs(bothzero))==0
 record('osc2_off_invariance_and_levels_zero',rate=rate,bitwise_equal=True)
 for wave in range(7):
  a,_=render({'pure_osc1_waveform':wave},rate=rate,block=block);assert rms(a)>.001 and max(abs(a))<1
  x=a[int(.2*rate):];f=np.fft.rfftfreq(len(x),1/rate);power=abs(np.fft.rfft(x*np.hanning(len(x))))**2
  high=float(sum(power[f>rate*.2])/sum(power));record('waveform',wave=wave,rate=rate,rms=rms(a),high_band_ratio=high,dc=float(np.mean(x)))
  if wave==0:assert high<1e-6
  if wave==6:assert .45<high<.8
 # Sine makes the switch-induced step independently measurable from natural saw edges.
 pure={'pure_osc1_waveform':0,'pure_osc2_waveform':0,'pure_osc2_detune_cents':0}
 a,_=render(pure,rate=rate,block=block,changes=[(.2,{'pure_osc2_enabled':1}),(.5,{'pure_osc2_enabled':0})]);step=float(max(abs(np.diff(a))));assert step<.02;record('osc2_toggle_ramp',rate=rate,max_step=step)
 a,_=render({'pure_osc1_waveform':0},rate=rate,block=block,changes=[(.2,{'pure_osc1_waveform':3})]);ix=math.ceil(.2*rate/block)*block;local=float(max(abs(np.diff(a[ix:ix+int(.0003*rate)]))));assert local<.03;record('waveform_crossfade',rate=rate,first_0_3ms_step=local)
 # Continuous MIDI pitch reaches both oscillators separately.
 for osc in [1,2]:
  setup={'pure_osc1_waveform':0,'pure_osc2_waveform':0,'pure_osc2_detune_cents':0,'pure_osc1_level':int(osc==1),'pure_osc2_enabled':int(osc==2)}
  points=[]
  for raw in [8192,10000,12000,16383]:
   a,_=render(setup,rate=rate,block=block,events=[(.1,[(0xe0,raw&127,raw>>7)])]);expected=261.625565*2**(((raw-8192)/8192*2)/12);hz=f0(a,rate,expected);assert abs(1200*math.log2(hz/expected))<3;points.append(hz)
  record('continuous_bend',osc=osc,rate=rate,hz=points)
# Mixer linearity, separate OSC2 gain, deterministic beats and pitch controllers.
a,_=render();b,_=render({'pure_osc1_level':0,'pure_osc2_enabled':1,'pure_osc2_semitone':7});c,_=render({'pure_osc2_enabled':1,'pure_osc2_semitone':7})
assert rms(c-a-b)<1e-7
half,_=render({'pure_osc1_level':0,'pure_osc2_enabled':1,'pure_osc2_semitone':7,'pure_osc2_level':.5});assert rms(half-b*.5)<1e-7
record('independent_mix_and_osc2_gain',sum_error=rms(c-a-b),level_error=rms(half-b*.5))
for cents in [0,7]:
 a,_=render({'pure_osc1_waveform':0,'pure_osc2_enabled':1,'pure_osc2_waveform':0,'pure_osc2_detune_cents':cents},duration=4)
 chunks=[rms(x) for x in np.array_split(a[4800:],40)];spread=max(chunks)-min(chunks)
 if cents==0:assert spread<.003
 else:assert spread>.05
 record('beating',cents=cents,envelope_spread=spread)
for source,event in [(7,(0xb0,1,127)),(8,(0xd0,127)),(9,(0xa0,60,127))]:
 setup={'synth_mod_01_source':source,'synth_mod_01_target':1,'synth_mod_01_depth':.05,'pure_osc2_enabled':1}
 a,_=render(setup);b,_=render(setup,events=[(.1,[event])]);assert diff(a,b)>.1;record('pitch_modulation_controller',source=source,difference=diff(a,b))
a,_=render(events=[(.1,[(0xb0,123,0)])]);assert max(abs(a[24000:]))==0;record('all_notes_off',tail_peak=0)
# Existing SCXT controls affect Pure audio through actual LV2 ports.
base,_=render()
for values in [ {'synth_amp_attack_ms':500}, {'synth_amp_decay_ms':100,'synth_amp_sustain':.2}, {'synth_filter_mode':1,'synth_cutoff_hz':400}, {'synth_filter_mode':1,'synth_cutoff_hz':400,'synth_feg_depth_semitones':48,'synth_feg_sustain':1}, {'synth_lfo1_depth':1,'synth_mod_01_source':1,'synth_mod_01_target':1,'synth_mod_01_depth':.03}, {'synth_unison_voices':4,'synth_unison_detune_cents':20,'synth_unison_spread':1} ]:
 a,_=render(values);delta=diff(base,a);assert delta>.001;record('SCXT_ports',values=values,difference=delta)
a,_=render({'synth_amp_release_ms':1000},events=[(.3,[(0x80,60,0)])]);b,_=render({'synth_amp_release_ms':5},events=[(.3,[(0x80,60,0)])]);assert rms(a[24000:])>rms(b[24000:])+1e-4;record('AMP_release',difference=diff(a,b))
for osc in [1,2]:
 setup={'pure_osc1_level':int(osc==1),'pure_osc2_enabled':int(osc==2),'pure_osc2_detune_cents':0,'synth_voice_mode':2,'synth_glide_mode':1}
 ev=[(.2,[(0x90,67,100)]),(.5,[(0x80,67,0)])];a,_=render(setup,events=ev);b,_=render(dict(setup,synth_glide_ms=500),events=ev);assert diff(a,b)>.1;record('glide_legato',osc=osc,difference=diff(a,b))
# Voice management/stress, sustain and release to silence.
for mode in [0,1,2]:
 events=[(.1,[(0xb0,64,127),(0x90,67,100)]),(.2,[(0x80,60,0),(0x80,67,0)]),(.4,[(0xb0,64,0)]),(.6,[(0xb0,120,0)])]
 a,_=render({'synth_voice_mode':mode,'synth_polyphony':2,'pure_osc2_enabled':1},events=events,duration=1);assert max(abs(a[36000:]))==0;record('voice_modes_sustain',mode=mode,tail_peak=0)
# Host-owned control state + plugin State recreate exactly (deterministic tonal waves).
persist={'pure_osc2_enabled':1,'pure_osc1_waveform':1,'pure_osc2_waveform':3,'pure_osc2_semitone':7,'synth_filter_mode':1,'synth_cutoff_hz':1700,'synth_resonance':.2}
a,sa=render(persist);b,sb=render(persist);assert np.array_equal(a,b) and sa==sb;record('state_and_ports_recreation',bitwise_equal=True)
print(f'{len(results)} Pure real LV2 checks PASS')
