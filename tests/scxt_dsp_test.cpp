#include "ScxtDspAdapter.hpp"
#include <iostream>
#include <stdexcept>
#include <random>
using namespace nord::synth;
void ck(bool v,const char*s){if(!v)throw std::runtime_error(s);}
int main(){try{double maximum=0;
 auto diff=[&](float a,float b){ck(std::isfinite(a)&&std::isfinite(b),"nonfinite reference");maximum=std::max(maximum,double(std::abs(a-b)));ck(std::abs(a-b)<=1e-5,"SCXT numeric mismatch");};
 for(double rate:{44100.,48000.}){
  for(float attack:{0.f,500.f,30000.f})for(float release:{0.f,1000.f,30000.f}){ScxtEnvelope wrapped;wrapped.rate.setRate(rate);ScxtRate provider;provider.setRate(rate);ScxtEnvelopeAlgorithm reference(&provider);
   auto param=[](float ms){return ms<=0?0.f:float((std::log(std::min(ms,25000.f)+2.)-.6931471824646)/(10.1267113685608-.6931471824646));};
   wrapped.trigger(attack);reference.resetCurrent();reference.attackFromWithDelay(0,0,param(attack));
   for(int q=0;q<int(10*rate/16);++q){if(q==1000){wrapped.trigger(attack);reference.resetCurrent();reference.attackFromWithDelay(0,0,param(attack));}bool gate=q<2000;wrapped.process(gate,attack,500,.4,release);
    float duration=reference.stage==ScxtEnvelopeAlgorithm::s_attack?attack:reference.stage==ScxtEnvelopeAlgorithm::s_decay?500:reference.stage==ScxtEnvelopeAlgorithm::s_release?release:0;
    reference.processBlockWithDelayAndRateMul(0,param(attack),0,param(500),.4,param(release),0,0,0,duration>25000?25000/duration:1,gate,true);
    for(int j=0;j<16;++j)diff(wrapped.algorithm.outputCache[j],reference.outputCache[j]);
   }
  }
  for(int channels:{1,2})for(int signal:{0,1,2})for(int mode:{1,2,3})for(float hz:{40.f,1000.f,18000.f})for(float resonance:{0.f,.7f,1.f}){ScxtFastSvf wrapped;wrapped.prepare(rate);sst::filtersplusplus::Filter reference;reference.setSampleRateAndBlockSize(rate,16);reference.setFilterModel(sst::filtersplusplus::FilterModel::CytomicSVF);reference.setPassband(mode==1?sst::filtersplusplus::Passband::LP:mode==2?sst::filtersplusplus::Passband::HP:sst::filtersplusplus::Passband::BP);if(channels==1)reference.setMono();else reference.setStereo();ck(reference.prepareInstance(),"reference mode unsupported");std::mt19937 rng(1);
   for(int q=0;q<100;++q){float l[16],r[16],refL[16],refR[16];for(int i=0;i<16;++i){l[i]=refL[i]=signal==0?(q==0&&i==0?1.f:0.f):signal==1?float(std::sin((q*16+i)*.14)*.1):float(int32_t(rng())/2147483648.)*.1f;r[i]=refR[i]=channels==1?l[i]:signal==0?(q==0&&i==1?.5f:0.f):signal==1?float(std::cos((q*16+i)*.14)*.1):float(int32_t(rng())/2147483648.)*.1f;}
    wrapped.process(l,r,mode,hz,resonance);reference.makeCoefficients(0,12*std::log2(std::min(hz,float(.45*rate))/440),resonance);reference.copyCoefficientsFromVoiceToVoice(0,1);reference.freezeCoefficientsFor(2);reference.freezeCoefficientsFor(3);reference.prepareBlock();for(int i=0;i<16;++i){if(channels==1){float mono=reference.processMonoSample(refL[i]);diff(l[i],mono);diff(r[i],mono);}else{reference.processStereoSample(refL[i],refR[i],refL[i],refR[i]);diff(l[i],refL[i]);diff(r[i],refR[i]);}}reference.concludeBlock();
   }
  }
  for(int wave=0;wave<6;++wave){ScxtLfo wrapped;wrapped.rate.setRate(rate);ScxtRate provider;provider.setRate(rate);sst::basic_blocks::dsp::RNG rng(1);ScxtLfo::Algorithm reference(&provider,rng);wrapped.trigger(wave,.13,123);rng.reseed(123);reference.restartRandomSequence(0);reference.attack(ScxtLfo::shape(wave));reference.applyPhaseOffset(.13);
   for(int q=0;q<1000;++q){wrapped.process(5,wave);reference.process_block(std::log2(5.f),0,ScxtLfo::shape(wave),false,1,0);for(int i=0;i<16;++i)diff(wrapped.algorithm.outputBlock[i],reference.outputBlock[i]);}
  }
  {ScxtModMatrix wrapped;ScxtModMatrix::Algorithm reference;ScxtModMatrix::Table table;SynthParameters p;std::array<float,10> sources{};std::array<float,8>bases{};for(int i=0;i<10;++i)reference.bindSourceValue(i,sources[i]);for(int i=0;i<8;++i)reference.bindTargetBaseValue(i,bases[i]);
   constexpr int vias[]={0,7,5,8};constexpr float scales[]={0,24,60,1,24,1,4,4};
   for(int i=0;i<18;++i){int pos=mod_01_source+4*i,source=1+i%9,target=1+i%7,via=i%4;p.set(pos,source);p.set(pos+1,target);p.set(pos+2,via);p.set(pos+3,(i%2?.2f:-.1f));auto&r=table.routes[i];r.source=source;r.target=target;r.sourceVia=via?std::optional<int>(vias[via]):std::nullopt;r.depth=p[pos+3]*scales[target];r.sourceLagMS=source>=5?5:0;r.sourceViaLagMS=via?5:0;}
   wrapped.configure(p,rate);reference.prepare(table,rate,16);for(int q=0;q<1000;++q){for(int i=1;i<10;++i)sources[i]=wrapped.sources[i]=float(std::sin(q*.03+i));if(q==500){p.set(mod_01_depth,.5);wrapped.configure(p,rate);table.routes[0].depth=.5*24;}
    if(q%33==0||q==500){const auto&curves=scxt::modulation::ModulationCurves::allCurves;for(int r=0;r<18;++r){auto curve=curves[(r+q/33)%curves.size()];wrapped.table.routes[r].curve=curve;table.routes[r].curve=curve;}wrapped.algorithm.prepare(wrapped.table,rate,16);reference.prepare(table,rate,16);}wrapped.algorithm.process();reference.process();for(int i=1;i<8;++i)diff(wrapped.value(i),reference.getTargetValue(i));}
  }
 }
 std::cout<<"T3 original SCXT locked dependencies: two EG instances/10s/retrigger, Fast SVF LP/HP/BP 324 configs mono/stereo separate impulse/sine/noise, six LFOs/1000 quanta, 18 routes/via/all SCXT curves/automation 1000 quanta; maxAbs="<<maximum<<" PASS\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
