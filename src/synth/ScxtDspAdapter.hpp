#pragma once
// Original algorithms are included from the SHA-locked SST dependencies of SCXT.
// The combined DSP is GPL-3.0-or-later; see README.md (licenses and source provenance).
#include "SynthParameters.hpp"
#include "scxt-port/mod_curves.h"
#include <limits>
#include <functional>
#include <mutex>
#include "sst/basic-blocks/modulators/AHDSRShapedSC.h"
#include "sst/basic-blocks/modulators/SimpleLFO.h"
#include "sst/basic-blocks/mod-matrix/ModMatrix.h"
#include "sst/basic-blocks/dsp/PanLaws.h"
#include "sst/filters++.h"
#include "sst/waveshapers.h"
namespace nord::synth {
struct ScxtRate {
 double sampleRate=44100,sampleRateInv=1./44100,samplerate=44100;
 sst::basic_blocks::tables::TwoToTheXProvider power;
 ScxtRate(){power.init();}
 void setRate(double rate)noexcept{sampleRate=samplerate=rate;sampleRateInv=1/rate;}
 float envelope_rate_linear_nowrap(float f){return float(16*sampleRateInv*power.twoToThe(-f));}
};
using ScxtEnvelopeAlgorithm=sst::basic_blocks::modulators::AHDSRShapedSC<ScxtRate,16,sst::basic_blocks::modulators::TwentyFiveSecondExp,true>;
struct ScxtEnvelope {
 struct Warm {Warm(){static std::once_flag once;std::call_once(once,[]{ScxtEnvelopeAlgorithm::initializeLuts();});}} warm;
 ScxtRate rate;ScxtEnvelopeAlgorithm algorithm{&rate};
 static float timeParameter(float ms)noexcept {
  using R=sst::basic_blocks::modulators::TwentyFiveSecondExp;
  return ms<=0?0:float((std::log(std::min(ms,25000.f)-R::C)-R::A)/(R::B-R::A));
 }
 void trigger(float attack)noexcept {algorithm.resetCurrent();algorithm.attackFromWithDelay(0,0,timeParameter(attack));}
 void process(bool gate,float attack,float decay,float sustain,float release)noexcept {
  float duration=algorithm.stage==ScxtEnvelopeAlgorithm::s_attack?attack:algorithm.stage==ScxtEnvelopeAlgorithm::s_decay?decay:algorithm.stage==ScxtEnvelopeAlgorithm::s_release?release:0;
  float mul=duration>25000?25000/duration:1;
  algorithm.processBlockWithDelayAndRateMul(0,timeParameter(attack),0,timeParameter(decay),sustain,timeParameter(release),0,0,0,mul,gate,true);
 }
};
struct ScxtLfo {
 ScxtRate rate;sst::basic_blocks::dsp::RNG rng{1};
 using Algorithm=sst::basic_blocks::modulators::SimpleLFO<ScxtRate,16>;
 Algorithm algorithm{&rate,rng};
 static int shape(int wave)noexcept {constexpr int modes[]={Algorithm::SINE,Algorithm::TRI,Algorithm::RAMP,Algorithm::PULSE,Algorithm::SMOOTH_NOISE,Algorithm::SH_NOISE,Algorithm::DOWN_RAMP};return modes[std::clamp(wave,0,6)];}
 void trigger(int wave,float phase,uint32_t seed)noexcept {rng.reseed(seed);algorithm.restartRandomSequence(0);algorithm.attack(shape(wave));algorithm.applyPhaseOffset(phase);}
 void process(float hz,int wave)noexcept {algorithm.process_block(std::log2(std::clamp(hz,.001f,640.f)),0,shape(wave),false,1,0);}
};
struct ScxtMatrixConfig {
 using SourceIdentifier=int;using TargetIdentifier=int;using CurveIdentifier=uint32_t;using RoutingExtraPayload=int;
 static constexpr bool IsFixedMatrix=true,ProvidesNonZeroTargetBases=true;
 static constexpr size_t FixedMatrixSize=19;
 static bool supportsLag(int){return true;}
 static std::function<float(float)> getCurveOperator(uint32_t id){return scxt::modulation::ModulationCurves::getCurveOperator(id);}
};
struct ScxtModMatrix {
 using Algorithm=sst::basic_blocks::mod_matrix::FixedMatrix<ScxtMatrixConfig>;
 using Table=sst::basic_blocks::mod_matrix::FixedLengthRoutingTable<ScxtMatrixConfig>;
 Algorithm algorithm;Table table;std::array<float,11> sources{};std::array<float,8> bases{};
 std::array<float,72> lastRoutes{};double lastRate=0;
 ScxtModMatrix(){scxt::modulation::ModulationCurves::initializeCurves();for(int i=0;i<11;++i)algorithm.bindSourceValue(i,sources[i]);for(int i=0;i<8;++i)algorithm.bindTargetBaseValue(i,bases[i]);}
 void configure(const SynthParameters& p,double rate,bool force=false)noexcept {
  bool changed=force||rate!=lastRate;for(size_t i=0;i<72;++i)changed=changed||lastRoutes[i]!=p[mod_01_source+i];if(!changed)return;
  for(size_t i=0;i<72;++i)lastRoutes[i]=p[mod_01_source+i];lastRate=rate;
  constexpr float scales[]={0,24,60,1,24,1,4,4};constexpr int vias[]={0,7,5,8};
  for(int i=0;i<18;++i){size_t pos=mod_01_source+4*i;int source=int(p[pos]),target=int(p[pos+1]),via=int(p[pos+2]);auto&r=table.routes[i];r.source=source;r.target=target;r.sourceVia=via?std::optional<int>(vias[via]):std::nullopt;r.curve=std::nullopt;r.depth=p[pos+3]*scales[target];r.active=source!=0&&target!=0;r.sourceLagMS=source>=5?5:0;r.sourceViaLagMS=via?5:0;}
  // Fixed wheel vibrato source is supplied by the shared SST sine LFO.
  // An explicit wheel source/via route owns CC1 and disables this fallback.
  bool explicitWheel=false;for(unsigned i=0;i<18;++i){unsigned at=mod_01_source+4*i;explicitWheel|=p[at]!=0&&p[at+1]!=0&&p[at+3]!=0&&(p[at]==7||p[at+2]==1);}
  auto&wheel=table.routes[18];wheel.source=10;wheel.target=1;wheel.sourceVia=7;wheel.curve=std::nullopt;
  wheel.depth=.5f;wheel.active=!explicitWheel;wheel.sourceLagMS=0;wheel.sourceViaLagMS=5;
  algorithm.prepare(table,rate,16);
 }
 float value(int target)const noexcept{return algorithm.getTargetValue(target);}
};
struct ScxtFastSvf {
 sst::filtersplusplus::Filter algorithm;int mode=0;double sampleRate=44100;
 ScxtFastSvf(){prepare(44100);}
 void prepare(double rate){sampleRate=rate;algorithm.setSampleRateAndBlockSize(rate,16);algorithm.setFilterModel(sst::filtersplusplus::FilterModel::CytomicSVF);algorithm.setStereo();for(int i=1;i<=4;++i)configure(i);mode=0;algorithm.reset();}
 void configure(int selected)noexcept {
  using P=sst::filtersplusplus::Passband;
  algorithm.setPassband(selected==1?P::LP:selected==2?P::HP:selected==3?P::BP:P::HighShelf);
  [[maybe_unused]]bool ok=algorithm.prepareInstance();mode=selected;
 }
 void reset()noexcept{algorithm.reset();mode=0;}
 void process(float*l,float*r,int selected,float cutoff,float resonance,float shelfAmp=1)noexcept {
  if(!selected){if(mode)reset();return;}if(selected!=mode)configure(selected);
  float note=12*std::log2(std::clamp(cutoff,20.f,float(std::min(20000.,.45*sampleRate)))/440.f);
  algorithm.makeCoefficients(0,note,std::clamp(resonance,0.f,1.f),shelfAmp);algorithm.copyCoefficientsFromVoiceToVoice(0,1);algorithm.freezeCoefficientsFor(2);algorithm.freezeCoefficientsFor(3);algorithm.prepareBlock();
  for(int i=0;i<16;++i)algorithm.processStereoSample(l[i],r[i],l[i],r[i]);algorithm.concludeBlock();
 }
};
struct ScxtDspAdapter {
 ScxtEnvelope amp,filterEg;ScxtLfo lfo1,lfo2;ScxtModMatrix matrix;ScxtFastSvf filter,bright;float brightWet=0,driveAmount=0;float lastPhase1=0,lastPhase2=0;
 void setRate(double rate){amp.rate.setRate(rate);filterEg.rate.setRate(rate);lfo1.rate.setRate(rate);lfo2.rate.setRate(rate);filter.prepare(rate);bright.prepare(rate);}
 void trigger(const SynthParameters&p,uint32_t seed,const ScxtLfo&free1,const ScxtLfo&free2)noexcept {
  amp.trigger(p[amp_attack_ms]);filterEg.trigger(p[feg_attack_ms]);
  lfo1.trigger(p[stage_lfo_down]?6:int(p[lfo1_wave]),p[lfo1_phase],seed);lfo2.trigger(int(p[lfo2_wave]),p[lfo2_phase],seed+1);
  lastPhase1=p[lfo1_phase];lastPhase2=p[lfo2_phase];
  if(p[lfo1_trigger])lfo1.algorithm.phase=std::fmod(free1.algorithm.phase+p[lfo1_phase],1.);
  if(p[lfo2_trigger])lfo2.algorithm.phase=std::fmod(free2.algorithm.phase+p[lfo2_phase],1.);
  filter.reset();bright.reset();brightWet=p[stage_bright];driveAmount=p[stage_drive];matrix.sources.fill(0);matrix.configure(p,amp.rate.sampleRate,true);
 }
 // Per-voice color/Drive precedes the selectable filter, even when it is bypassed.
 void tonal(float*l,float*r,const SynthParameters&p)noexcept {
  if(!p[stage_bright]&&brightWet<1e-7f&&!p[stage_drive]&&driveAmount<1e-7f)return;
  alignas(16)float bl[16],br[16];std::copy_n(l,16,bl);std::copy_n(r,16,br);
  // Original Fast SVF high shelf; six dB is a functional adaptation, not Nord firmware equivalence.
  if(p[stage_bright]||brightWet>1e-6f)bright.process(bl,br,4,1800.f,0.f,std::pow(10.f,6.f/40));
  const float smooth=1-std::exp(-1.f/float(.005*amp.rate.sampleRate));
  sst::waveshapers::QuadWaveshaperState ws{};
  for(int i=0;i<16;++i){
   brightWet+=smooth*(p[stage_bright]-brightWet);driveAmount+=smooth*(p[stage_drive]-driveAmount);
   l[i]+=brightWet*(bl[i]-l[i]);r[i]+=brightWet*(br[i]-r[i]);
   if(driveAmount>1e-7f){alignas(16)float result[4];float drive=std::exp2(4*driveAmount);
    auto input=SIMD_MM(set_ps)(0.f,0.f,r[i],l[i]);auto output=sst::waveshapers::TANH(&ws,input,SIMD_MM(set1_ps)(drive));SIMD_MM(store_ps)(result,output);
    l[i]+=driveAmount*(result[0]/std::sqrt(drive)-l[i]);r[i]+=driveAmount*(result[1]/std::sqrt(drive)-r[i]);
   }
  }
 }
 void modulators(const SynthParameters&p,bool gate)noexcept {
  if(p[lfo1_phase]!=lastPhase1){lfo1.algorithm.applyPhaseOffset(p[lfo1_phase]);lastPhase1=p[lfo1_phase];}
  if(p[lfo2_phase]!=lastPhase2){lfo2.algorithm.applyPhaseOffset(p[lfo2_phase]);lastPhase2=p[lfo2_phase];}
  amp.process(gate,p[amp_attack_ms],p[amp_decay_ms],p[amp_sustain],p[amp_release_ms]);
  filterEg.process(gate,p[feg_attack_ms],p[feg_decay_ms],p[feg_sustain],p[feg_release_ms]);
  lfo1.process(p[lfo1_rate_hz]*std::exp2(matrix.value(6)),p[stage_lfo_down]?6:int(p[lfo1_wave]));lfo2.process(p[lfo2_rate_hz]*std::exp2(matrix.value(7)),int(p[lfo2_wave]));
  matrix.sources[1]=lfo1.algorithm.lastTarget*p[lfo1_depth];matrix.sources[2]=lfo2.algorithm.lastTarget*p[lfo2_depth];matrix.sources[3]=amp.algorithm.outBlock0;matrix.sources[4]=filterEg.algorithm.outBlock0;
  matrix.algorithm.process();
 }
};
}
