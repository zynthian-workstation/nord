"""Actual LV2 >=15s renders with changing host block size and controls on held voices."""
from v081_lv2 import Host
from pathlib import Path
import sys,math,json
rows=[]
for rate in [44100,48000]:
 h=Host(sys.argv[1],'Nord',rate,1024);h.load('builtin:pure');h.set(gain=.01,synth_polyphony=16,synth_unison_voices=4,pure_osc2_enabled=1)
 frames=0;i=0;peak=0
 while frames<rate*15:
  h.block=[1,17,64,255,1024][i%5]
  if i and i%100==0:h.set(pure_osc1_waveform=(i//100)%7,pure_osc2_waveform=(i//100+2)%7,pure_osc2_enabled=(i//100)%2)
  midi=[(0x90,36+n,100) for n in range(16)] if i==0 else []
  if i%80==0:
   raw=8192+int(4000*math.sin(i/800));midi.append((0xe0,raw&127,raw>>7))
  a=h.run(midi)[:h.block];right=list(h.right)[:h.block];assert all(math.isfinite(v) for v in a+right);peak=max(peak,max(map(abs,a+right)));frames+=h.block;i+=1
 h.run([(0xb0,120,0)]);h.run();assert max(map(abs,list(h.left)[:h.block]))==0
 h.close();assert peak<1;rows.append(dict(rate=rate,frames=frames,seconds=frames/rate,blocks=i,block_sizes=[1,17,64,255,1024],voices=64,peak=peak,finite=True,all_sound_off_silence=True))
Path(sys.argv[2]).write_text(json.dumps(rows,indent=2)+'\n');print('Real LV2 >=15s, changing blocks 1/17/64/255/1024, rates 44.1/48k, 64 voices, held-note waveform/OSC2/bend automation, finite/no clipping/zombies PASS')
