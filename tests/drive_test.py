"""Real LV2 sine: Drive adds progressively stronger harmonics, preserving headroom."""
from pathlib import Path
import json,math,sys
import numpy as np
from v081_lv2 import Host
bundle=Path(sys.argv[1]);rows=[]
for rate in (44100,48000):
    h=Host(bundle,'Nord',rate);h.load('builtin:pure')
    for mode in (0,1):
        levels=[]
        for drive in (0,.25,.5,.75,1):
            h.set(pure_osc1_waveform=0,stage_drive=drive,synth_filter_mode=mode,synth_cutoff_hz=12000)
            h.run([(0xb0,120,0),(0xb0,121,0)])
            for _ in range(8):h.run()
            while h.frames%16:h.run()
            out=[]
            for i in range(math.ceil(rate*1.2/h.block)):
                out+=h.run([(0x90,69,127)] if i==0 else [])
            a=np.asarray(out);assert np.isfinite(a).all() and max(abs(a))<1
            x=a[int(.5*rate):int(rate)];s=abs(np.fft.rfft(x*np.hanning(len(x))));f=np.fft.rfftfreq(len(x),1/rate)
            harmonics=[float(max(s[abs(f-440*k)<5])) for k in range(1,20)]
            thd=math.sqrt(sum(v*v for v in harmonics[1:]))/harmonics[0];levels.append(thd)
            rows.append(dict(rate=rate,filter=mode,drive=drive,thd=thd,peak=float(max(abs(a))),rms=float(np.sqrt(np.mean(a*a)))))
        assert levels[0]<1e-4 and all(b>a for a,b in zip(levels,levels[1:])) and levels[-1]>.25,levels
    h.close()
Path(sys.argv[2]).write_text(json.dumps(rows,indent=2)+'\n')
print('Drive five levels, two rates, filter bypass/active, monotonic THD and no clipping PASS')
