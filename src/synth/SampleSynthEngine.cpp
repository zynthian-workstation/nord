#include "SampleSynthEngine.hpp"
#include "ScxtDspAdapter.hpp"
#include "PureSource.hpp"
#include "PianoEngine.hpp"
#include "MidiPerformance.hpp"
#include <atomic>
#include <array>
namespace nord::synth {
struct SampleSynthEngine::State {
 struct Voice {
  PureSource pure;bool isPure=false;
  ScxtDspAdapter dsp;Nsmp4SamplePlayer player;const Instrument* instrument=nullptr;const SampleData* sample=nullptr;const nord::Bank* owner=nullptr;Zone zone;
  uint64_t age=0,group=0;uint8_t channel=0,note=0,velocity=0;bool active=false,gate=false,keyDown=false,held=false;
  float detune=0,pan=0,unisonGain=1,lastL=0,lastR=0,tailL=0,tailR=0;unsigned ramp=0;int filterMode=0;
  sst::basic_blocks::dsp::LinearLag<float,false> glide;
  sst::basic_blocks::dsp::OnePoleLag<float,false> gain,panLag,cutoffLag,resLag,pitchLag;
 };
 std::array<Voice,64> voices;
 struct Key {uint16_t count=0;uint8_t velocity=0;uint64_t order=0;};
 std::array<std::array<Key,128>,16> keys{};
 std::array<bool,16> pedal{};
 // Position and applied pitch are performance state. Range Up/Down remain in
 // params and only participate when an actual MIDI Bend event is dispatched.
 std::array<int16_t,16> bendPosition{};
 std::array<float,16> bendPitch{},wheel{},pressure{};std::array<std::array<float,128>,16> polyPressure{};
 std::array<float,16> volume,expression;
 std::array<int,16> previousNote;uint64_t age=0;double rate=44100;
 std::array<std::atomic<float>,parameterCount> pending;SynthParameters params;std::atomic<uint64_t> parameterGeneration{0};uint64_t appliedGeneration=0;
 ScxtBlockAdapter block;ScxtLfo freeLfo1,freeLfo2,wheelLfo;
 bool pure=false;
 const Instrument* instrument=nullptr;const nord::Bank* bank=nullptr;
 std::shared_ptr<const Instrument> standalone;
 State(){volume.fill(1);expression.fill(1);previousNote.fill(-1);for(size_t i=0;i<parameterCount;++i)pending[i].store(params[i]);}
 ~State(){for(auto&v:voices)stop(v);}
 static void stop(Voice&v)noexcept {if(v.owner){v.owner->synthPins.fetch_sub(1,std::memory_order_release);v.owner=nullptr;}v.active=v.gate=v.keyDown=v.held=false;v.sample=nullptr;v.instrument=nullptr;v.player.start(nullptr);}
 void prepare(double nextRate,uint32_t maximum){rate=nextRate;keys={};volume.fill(1);expression.fill(1);pedal.fill(false);bendPosition.fill(0);bendPitch.fill(0);wheel.fill(0);pressure.fill(0);polyPressure={};previousNote.fill(-1);block.prepare(maximum);freeLfo1.rate.setRate(rate);freeLfo2.rate.setRate(rate);wheelLfo.rate.setRate(rate);wheelLfo.trigger(0,0,0x9876);freeLfo1.trigger(0,0,0x1234);freeLfo2.trigger(0,0,0x5678);for(auto&v:voices){stop(v);v.dsp.setRate(rate);v.gain.setRateInMilliseconds(5,rate,1);v.panLag.setRateInMilliseconds(5,rate,1);v.pitchLag.setRateInMilliseconds(5,rate,1);v.cutoffLag.setRateInMilliseconds(5,rate,1./16);v.resLag.setRateInMilliseconds(5,rate,1./16);}}
 int latest(uint8_t ch)const noexcept {int result=-1;uint64_t latestAge=0;for(int n=0;n<128;++n)if(keys[ch][n].count&&keys[ch][n].order>=latestAge){result=n;latestAge=keys[ch][n].order;}return result;}
 void moveLegato(uint8_t ch,int note,uint8_t velocity)noexcept {for(auto&v:voices)if(v.active&&v.channel==ch&&(v.keyDown||v.held)){v.note=uint8_t(note);v.velocity=velocity;v.keyDown=v.gate=true;v.held=false;v.glide.setRateInMilliseconds(std::max(params[glide_ms],.001f),rate,1);if(params[glide_mode]==0||params[glide_ms]==0)v.glide.snapTo(note);else v.glide.setTarget(note);}}
 void start(uint8_t ch,uint8_t note,uint8_t velocity,bool overlap)noexcept {
  const Zone builtinZone{};const Zone*z=pure?&builtinZone:instrument?instrument->zoneFor(note,velocity):nullptr;if(!z)return;
  int copies=int(params[unison_voices]);size_t limit=std::min(size_t(64),size_t(params[polyphony])*copies);uint64_t group=++age;
  for(int c=0;c<copies;++c){Voice*slot=nullptr;for(size_t i=0;i<limit;++i)if(!voices[i].active){slot=&voices[i];break;}
   if(!slot){auto priority=[](const Voice&v){return v.gate?(v.keyDown?2:1):0;};for(size_t i=0;i<limit;++i){auto&v=voices[i];if(v.group==group)continue;if(!slot){slot=&v;continue;}int a=priority(v),b=priority(*slot);if(a<b||(a==b&&(v.dsp.amp.algorithm.outBlock0<slot->dsp.amp.algorithm.outBlock0||(v.dsp.amp.algorithm.outBlock0==slot->dsp.amp.algorithm.outBlock0&&v.age<slot->age))))slot=&v;}}
   auto&v=*slot;bool steal=v.active;float tailL=v.lastL,tailR=v.lastR;stop(v);
   v.owner=bank;if(bank)bank->synthPins.fetch_add(1,std::memory_order_relaxed);v.instrument=instrument;v.zone=*z;v.isPure=pure;v.sample=pure?nullptr:instrument->samples[z->sampleIndex].get();if(pure)v.pure.reset(uint32_t(v.age+age),params);v.active=v.keyDown=v.gate=true;v.held=false;v.channel=ch;v.note=note;v.velocity=velocity;v.age=++age;v.group=group;
   float symmetric=copies==1?0:2.f*c/(copies-1)-1;v.detune=symmetric*params[unison_detune_cents]/100;v.pan=symmetric*params[unison_spread];v.unisonGain=1/std::sqrt(float(copies));v.tailL=steal?tailL:0;v.tailR=steal?tailR:0;v.ramp=steal?32:0;v.lastL=v.lastR=0;v.filterMode=int(params[filter_mode]);
   v.pitchLag.snapTo(params[coarse_semitones]+params[fine_cents]/100+bendPitch[ch]+v.detune);
   v.player.start(v.sample);bool glide=params[glide_ms]>0&&(params[glide_mode]==1||(params[glide_mode]==2&&overlap));v.glide.snapTo(glide&&previousNote[ch]>=0?previousNote[ch]:note);v.glide.setRateInMilliseconds(std::max(params[glide_ms],.001f),rate,1);v.glide.setTarget(note);
   v.dsp.trigger(params,uint32_t(v.age),freeLfo1,freeLfo2);float vel=1-params[velocity_gain_depth]+params[velocity_gain_depth]*velocity/127.f;v.gain.snapTo(std::clamp(z->gainLinear*(pure?1:instrument->playbackGain(note))*vel*v.unisonGain*volume[ch]*expression[ch],0.f,1e6f));v.panLag.snapTo(v.pan);v.cutoffLag.snapTo(std::log2(params[cutoff_hz]));v.resLag.snapTo(params[resonance]);
  }previousNote[ch]=note;
 }
 void midi(const MidiVoiceEvent&e)noexcept {
  auto ch=e.channel;if(ch>=16||e.note>127||e.value>127)return;
  auto type=e.type;if(type==MidiVoiceEvent::Type::NoteOn&&!e.value)type=MidiVoiceEvent::Type::NoteOff;
  if(type==MidiVoiceEvent::Type::NoteOn){if(!pure&&(!instrument||!instrument->zoneFor(e.note,e.value)))return;bool overlap=latest(ch)>=0;polyPressure[ch][e.note]=0;auto& key=keys[ch][e.note];if(key.count<UINT16_MAX)++key.count;key.velocity=e.value;key.order=++age;
   if(params[voice_mode]==2&&overlap){bool running=false;for(auto&v:voices)running|=v.active&&v.channel==ch&&v.gate;if(running){moveLegato(ch,e.note,e.value);previousNote[ch]=e.note;return;}}
   if(params[voice_mode]!=0)for(auto&v:voices)if(v.active&&v.channel==ch){v.keyDown=v.held=v.gate=false;}
   start(ch,e.note,e.value,overlap);
  }else if(type==MidiVoiceEvent::Type::NoteOff){auto&key=keys[ch][e.note];if(key.count)--key.count;
   if(params[voice_mode]!=0){if(key.count)return;int next=latest(ch);bool current=false;for(auto&v:voices)current|=v.active&&v.channel==ch&&v.keyDown&&v.note==e.note;
    if(!current)return;if(next>=0){if(params[voice_mode]==2)moveLegato(ch,next,keys[ch][next].velocity);else{for(auto&v:voices)if(v.active&&v.channel==ch)v.keyDown=v.gate=v.held=false;start(ch,uint8_t(next),keys[ch][next].velocity,true);}return;}
   }
   uint64_t oldest=UINT64_MAX;for(auto&v:voices)if(v.active&&v.keyDown&&v.channel==ch&&v.note==e.note)oldest=std::min(oldest,v.group);
   for(auto&v:voices)if(v.active&&v.group==oldest){v.keyDown=false;v.held=pedal[ch];v.gate=v.held;}
  }else if(type==MidiVoiceEvent::Type::Sustain){bool down=e.value>=64;if(pedal[ch]&&!down)for(auto&v:voices)if(v.active&&v.channel==ch&&v.held){v.held=v.gate=false;}pedal[ch]=down;
  }else if(type==MidiVoiceEvent::Type::AllSoundOff||type==MidiVoiceEvent::Type::AllNotesOff){keys[ch].fill(Key{});for(auto&v:voices)if(v.active&&v.channel==ch){if(type==MidiVoiceEvent::Type::AllSoundOff)stop(v);else{v.keyDown=false;v.held=pedal[ch];v.gate=v.held;}}}
  else if(type==MidiVoiceEvent::Type::Bend){
   bendPosition[ch]=int16_t(std::clamp(int(e.pitchBend14),-8192,8191));
   bendPitch[ch]=float(nord::bendSemitones(bendPosition[ch],params[bend_range],params[bend_down]));
  }
  else if(type==MidiVoiceEvent::Type::Volume)volume[ch]=e.value/127.f;
  else if(type==MidiVoiceEvent::Type::Expression)expression[ch]=e.value/127.f;
  else if(type==MidiVoiceEvent::Type::ResetControllers){
   bendPosition[ch]=0;bendPitch[ch]=wheel[ch]=pressure[ch]=0;polyPressure[ch].fill(0);volume[ch]=expression[ch]=1;
   pedal[ch]=false;for(auto&v:voices)if(v.active&&v.channel==ch&&v.held)v.held=v.gate=false;
  }
  else if(type==MidiVoiceEvent::Type::CC1)wheel[ch]=e.value/127.f;
  else if(type==MidiVoiceEvent::Type::ChannelPressure)pressure[ch]=e.value/127.f;
  else if(type==MidiVoiceEvent::Type::PolyPressure)polyPressure[ch][e.note]=e.value/127.f;
 }
 void updateParameters()noexcept {
  auto generation=parameterGeneration.load(std::memory_order_acquire);if(generation==appliedGeneration||(generation&1))return;
  SynthParameters next;for(size_t i=0;i<parameterCount;++i)next.values[i]=pending[i].load(std::memory_order_relaxed);
  if(parameterGeneration.load(std::memory_order_acquire)!=generation)return;
  params=next;appliedGeneration=generation;for(auto&v:voices)if(v.active)v.dsp.matrix.configure(params,rate);
 }
 void quantum(float*outL,float*outR,uint64_t)noexcept {
  updateParameters();
  wheelLfo.process(5.5f,0);freeLfo1.process(params[lfo1_rate_hz],int(params[lfo1_wave]));freeLfo2.process(params[lfo2_rate_hz],int(params[lfo2_wave]));std::fill_n(outL,16,0);std::fill_n(outR,16,0);
  for(auto&v:voices){if(!v.active)continue;auto&d=v.dsp;

   d.matrix.sources[10]=wheelLfo.algorithm.lastTarget;d.matrix.sources[5]=v.velocity/127.f;d.matrix.sources[6]=std::clamp((int(v.note)-60)/60.f,-1.f,1.f);d.matrix.sources[7]=wheel[v.channel];d.matrix.sources[8]=pressure[v.channel];d.matrix.sources[9]=polyPressure[v.channel][v.note];
   if(!v.isPure&&!v.player.active())v.gate=false;d.modulators(params,v.gate);
   float pitch=std::clamp(params[coarse_semitones]+params[fine_cents]/100+bendPitch[v.channel]+d.matrix.value(1)+v.detune,-144.f,144.f);
   v.pitchLag.setTarget(pitch);
   float tracking=params[keytrack]/3.f;float cutoffSemis=tracking*(int(v.note)-60)+params[feg_depth_semitones]*(1-params[stage_feg_velocity]+params[stage_feg_velocity]*v.velocity/127.f)*d.filterEg.algorithm.outBlock0+d.matrix.value(2);
   float cutoff=std::clamp(std::log2(params[cutoff_hz])+cutoffSemis/12,float(std::log2(20.)),float(std::log2(std::min(20000.,.45*rate))));v.cutoffLag.setTarget(cutoff);v.cutoffLag.process();v.resLag.setTarget(std::clamp(params[resonance]+d.matrix.value(3),0.f,1.f));v.resLag.process();
   float gain=std::pow(10.f,std::clamp(d.matrix.value(4),-144.f,144.f)/20)*v.zone.gainLinear*(v.isPure?1:v.instrument->playbackGain(v.note))*(1-params[velocity_gain_depth]+params[velocity_gain_depth]*v.velocity/127.f)*v.unisonGain*volume[v.channel]*expression[v.channel];gain=std::isfinite(gain)?std::clamp(gain,0.f,1e6f):1e6f;v.gain.setTarget(gain);v.panLag.setTarget(std::clamp(v.pan+d.matrix.value(5),-1.f,1.f));
   alignas(16)float l[16],r[16];
   for(int i=0;i<16;++i){v.glide.process();v.pitchLag.process();if(v.isPure){l[i]=r[i]=v.pure.process(v.glide.getValue()+v.pitchLag.getValue(),rate,params);}else{v.player.setRate(Nsmp4SamplePlayer::rate(*v.sample,v.zone,v.glide.getValue(),rate,v.pitchLag.getValue()));v.player.render(l[i],r[i]);}v.gain.process();v.panLag.process();sst::basic_blocks::dsp::pan_laws::panmatrix_t pan;sst::basic_blocks::dsp::pan_laws::stereoEqualPower((v.panLag.getValue()+1)*.5f,pan);float a=l[i],b=r[i],g=v.gain.getValue()*d.amp.algorithm.outputCache[i];l[i]=(pan[0]*a+pan[2]*b)*g;r[i]=(pan[1]*b+pan[3]*a)*g;}
   if(v.filterMode!=int(params[filter_mode])){v.filterMode=int(params[filter_mode]);v.tailL=v.lastL;v.tailR=v.lastR;v.ramp=32;}
   d.tonal(l,r,params);
   d.filter.process(l,r,int(params[filter_mode]),std::exp2(v.cutoffLag.getValue()),v.resLag.getValue());
   for(int i=0;i<16;++i){if(v.ramp){float t=v.ramp/32.f;l[i]=l[i]*(1-t)+v.tailL*t;r[i]=r[i]*(1-t)+v.tailR*t;--v.ramp;}outL[i]+=l[i];outR[i]+=r[i];v.lastL=l[i];v.lastR=r[i];}
   if(d.amp.algorithm.stage==ScxtEnvelopeAlgorithm::s_complete||(!v.isPure&&!v.player.active()&&params[filter_mode]==0))stop(v);
  }
 }
};
SampleSynthEngine::SampleSynthEngine():state_(std::make_unique<State>()){prepare(44100,8192);}
SampleSynthEngine::~SampleSynthEngine()=default;
void SampleSynthEngine::prepare(double rate,uint32_t maximum){state_->prepare(std::isfinite(rate)&&rate>=1?rate:44100,maximum);}
void SampleSynthEngine::prepareInstrument(std::shared_ptr<const Instrument> p){for(auto&v:state_->voices)State::stop(v);state_->pure=false;state_->standalone=std::move(p);state_->instrument=state_->standalone.get();state_->bank=nullptr;}
void SampleSynthEngine::resetVoices() noexcept {for(auto&v:state_->voices)State::stop(v);state_->keys={};state_->pedal.fill(false);state_->previousNote.fill(-1);state_->block.reset();}
void SampleSynthEngine::resetPitchBend() noexcept {state_->bendPosition.fill(0);state_->bendPitch.fill(0);}
void SampleSynthEngine::setBank(const nord::Bank* b)noexcept {const auto* next=b&&(b->synth||b->pure)?b:nullptr;
 if(next!=state_->bank&&(state_->pure||(next&&next->pure))){for(auto&v:state_->voices)State::stop(v);state_->keys={};state_->pedal.fill(false);state_->previousNote.fill(-1);state_->block.reset();}
 state_->bank=next;state_->pure=next&&next->pure;state_->instrument=next?next->synth.get():nullptr;}
void SampleSynthEngine::setParameters(const SynthParameters&p)noexcept {state_->parameterGeneration.fetch_add(1,std::memory_order_acq_rel);for(size_t i=0;i<parameterCount;++i){const auto&d=parameterDefinitions[i];float value=p[i];if(!std::isfinite(value))value=d.initial;value=std::clamp(value,d.minimum,d.maximum);if(*d.labels)value=std::round(value);state_->pending[i].store(value,std::memory_order_relaxed);}state_->parameterGeneration.fetch_add(1,std::memory_order_release);}
void SampleSynthEngine::render(const RenderContext& c)noexcept {const double hostRate=std::isfinite(c.hostSampleRate)&&c.hostSampleRate>=1?c.hostSampleRate:44100.;if(hostRate!=state_->rate){state_->rate=hostRate;for(auto&v:state_->voices){v.dsp.setRate(hostRate);v.gain.setRateInMilliseconds(5,hostRate,1);v.panLag.setRateInMilliseconds(5,hostRate,1);v.pitchLag.setRateInMilliseconds(5,hostRate,1);v.cutoffLag.setRateInMilliseconds(5,hostRate,1./16);v.resLag.setRateInMilliseconds(5,hostRate,1./16);}state_->freeLfo1.rate.setRate(hostRate);state_->freeLfo2.rate.setRate(hostRate);state_->wheelLfo.rate.setRate(hostRate);}
 state_->block.render(c,[&](auto&e){state_->updateParameters();state_->midi(e);},[&](float*l,float*r,uint64_t frame){state_->quantum(l,r,frame);});}
size_t SampleSynthEngine::activeVoices()const noexcept {size_t n=0;for(auto&v:state_->voices)n+=v.active;return n;}
void SampleSynthEngine::addMidiOverflows(uint64_t n)noexcept{state_->block.addOverflows(n);}
uint64_t SampleSynthEngine::midiOverflows()const noexcept{return state_->block.overflows();}
}
