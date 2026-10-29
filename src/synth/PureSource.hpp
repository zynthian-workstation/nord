#pragma once
#include "SynthParameters.hpp"
#include "SurgeModernDpw.hpp"
#include "sst/basic-blocks/dsp/FastMath.h"
#include "sst/basic-blocks/dsp/RNG.h"
namespace nord::synth {
// Two independent phases and per-wave crossfade weights, no PCM or dynamic storage.
struct PureOscillator {
 double phase=0;
 std::array<float,7> weights{},from{};
 int waveform=2;unsigned fade=0;
 sst::basic_blocks::dsp::RNG rng{1};
 void reset(uint32_t seed,int wave) noexcept {phase=0;waveform=wave;weights.fill(0);weights[wave]=1;from=weights;fade=0;rng.reseed(seed?seed:1);}
 float process(double hz,double rate,int wave) noexcept {
  const unsigned length=std::max(1u,unsigned(rate*.005));
  if(wave!=waveform){from=weights;waveform=wave;fade=length;}
  if(fade){float t=1-float(--fade)/length;for(int i=0;i<7;++i)weights[i]=from[i]*(1-t)+(i==waveform?t:0);}
  double dp=std::clamp(hz/rate,1.e-8,.5),p=phase;float result=0;
  for(int w=0;w<7;++w)if(weights[w]>0){double v=0;
   switch(w){
    // SineOscillator mode zero, legacy scalar phase path, shared Surge SST fastsin.
    case 0:v=sst::basic_blocks::dsp::fastsin(float((p-.5)*6.283185307179586));break;
    case 1:v=surge::dpwMultiComp<surge::momt_triangle>(p,dp,false);break;
    case 2:v=surge::dpwSawComp(p,dp,false);break;
    case 3:v=surge::dpwMultiComp<surge::momt_square>(p,dp,false);break;
    case 4:case 5:{double offset=1-(w==4?1./3:.1),q=p+offset;q-=(q>=1);v=.5*(surge::dpwSawComp(q,dp,false)-surge::dpwSawComp(p,dp,false));break;}
    // SurgeStorage::rand_pm1/noiseRng uniform path, with voice-local deterministic RNG.
    case 6:v=rng.unifPM1();break;
   }result+=float(v)*weights[w];
  }
  phase+=dp;phase-=(phase>=1);return result;
 }
};
struct PureSource {
 PureOscillator osc1,osc2;
 float osc2Ramp=0;
 void reset(uint32_t seed,const SynthParameters&p) noexcept {osc1.reset(seed,int(p[pure_osc1_waveform]));osc2.reset(seed^0x9e3779b9,int(p[pure_osc2_waveform]));osc2Ramp=p[pure_osc2_enabled];}
 float process(float midiPitch,double rate,const SynthParameters&p) noexcept {
  auto hz=[&](int first){return 440.*std::exp2((midiPitch-69+12*p[first+1]+p[first+2]+p[first+3]/100)/12.);};
  float a=osc1.process(hz(pure_osc1_waveform),rate,int(p[pure_osc1_waveform]))*p[pure_osc1_level];
  const float step=1.f/float(rate*.005),target=p[pure_osc2_enabled];
  // OSC2 starts at its frozen phase (zero on Note On); no EG retrigger.
  osc2Ramp+=std::clamp(target-osc2Ramp,-step,step);
  float b=0;if(osc2Ramp>0)b=osc2.process(hz(pure_osc2_waveform),rate,int(p[pure_osc2_waveform]))*p[pure_osc2_level]*osc2Ramp;
  return .5f*(a+b); // Static headroom, independent of Enabled/Levels.
 }
};
}
