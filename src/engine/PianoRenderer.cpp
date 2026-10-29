#include "PianoRenderer.hpp"
#include "PianoSustain.hpp"
#include "MidiPerformance.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace nord {
namespace {
constexpr float kReleaseSeconds=0.22f,kVoiceFloor=0.0001f;
// NPNO nominal full scale is 8192. Reserve 6 dB for summing voices; no per-note normalization.
constexpr float kPcmScale=.5f/8192.f;
}
void PianoRenderer::reset() noexcept {voices_.fill(Voice{});pedals_.fill(false);bend_.fill(0);volume_.fill(1);expression_.fill(1);level_.fill(1);target_.fill(1);levelDirty_=0;lowLeft_=lowRight_=timbreBlend_=0;}
void PianoRenderer::setSampleRate(double rate) noexcept {
 const double nextRate=std::isfinite(rate)&&rate>=1?rate:44100;
 if(nextRate==rate_)return;
 if(rate_>0)for(auto& v:voices_)if(v.sample){
  const double ratio=rate_/nextRate;
  v.step*=ratio;
  if(v.loop)v.decayCoefficient=float(std::pow(v.decayCoefficient,ratio));
 }
 rate_=nextRate;
 releaseCoefficient_=std::exp(-1.f/(kReleaseSeconds*float(rate_)));
 softReleaseCoefficient_=std::exp(-1.f/(1.5f*kReleaseSeconds*float(rate_)));
 toneAlpha_=1-std::exp(-2.f*3.14159265358979323846f*1800.f/float(rate_));
 smoothAlpha_=1-std::exp(-1.f/(.02f*float(rate_)));levelAlpha_=1-std::exp(-1.f/(.005f*float(rate_)));
}
void PianoRenderer::setBank(const Bank* bank) noexcept {
 const auto serial=bank?bank->serial:0;
 if(serial_!=serial){voices_.fill(Voice{});lowLeft_=lowRight_=0;serial_=serial;}
 bank_=bank;
}
size_t PianoRenderer::activeVoices() const noexcept {size_t n=0;for(const auto& v:voices_)n+=v.sample!=nullptr;return n;}
bool PianoRenderer::keyActive(uint8_t channel,uint8_t note) const noexcept {
 for(const auto& v:voices_)if(v.sample&&v.keyDown&&v.channel==channel&&v.note==note)return true;
 return false;
}
const Sample* PianoRenderer::newestSample() const noexcept {
 const Voice* newest=nullptr;
 for(const auto& v:voices_)if(v.sample&&(!newest||v.age>newest->age))newest=&v;
 return newest?newest->sample:nullptr;
}
void PianoRenderer::updatePitch(uint8_t channel) noexcept {
 for(auto&v:voices_)if(v.sample&&v.channel==channel){double old=v.step;v.step=pianoPitchStep(v.note+controls_.transpose+bendSemitones(bend_[channel],controls_.bendUp,controls_.bendDown),v.sample->root,rate_);if(v.loop)v.decayCoefficient=float(std::pow(v.decayCoefficient,v.step/old));}
}
void PianoRenderer::setControls(const PianoControls& controls) noexcept {
 bool pitch=controls.transpose!=controls_.transpose||controls.bendUp!=controls_.bendUp||controls.bendDown!=controls_.bendDown;
 controls_=controls;if(pitch)for(unsigned ch=0;ch<16;++ch)updatePitch(ch);
}
void PianoRenderer::noteOn(uint8_t channel,uint8_t note,uint8_t velocity) noexcept {
 if(!bank_)return;
 // Only the Attack bank is triggered.
 // Resonance/Release descriptors remain available in the parser, not triggered here.
 velocity=touchVelocity(velocity,controls_.touch);
 const Sample* sample=bank_->sampleFor(note,velocity);
 if(!sample||sample->l.empty())return;
 Voice* slot=nullptr;
 for(auto& v:voices_)if(!v.sample){slot=&v;break;}
 if(!slot){
  // Prefer a fading/tail voice, then a pedal-held voice, then the oldest key.
  auto priority=[](const Voice& v){return v.released?0:!v.keyDown?1:2;};
  slot=&voices_[0];
  for(auto& v:voices_)if(priority(v)<priority(*slot)||(priority(v)==priority(*slot)&&v.age<slot->age))slot=&v;
 }
 const double step=pianoPitchStep(note+controls_.transpose+bendSemitones(bend_[channel],controls_.bendUp,controls_.bendDown),sample->root,rate_);
 *slot=Voice{};
 slot->sample=sample;slot->step=step;slot->amplitude=velocity/127.f;
 slot->age=++age_;slot->note=note;slot->channel=channel;slot->velocity=velocity;slot->keyDown=true;
 // Preserve recorded attack/decay until the loop entry. Model remains experimental.
 const double coefficient=sustainCoefficient(*sample);
 slot->loop=sample->hasLoop()&&sample->loopLength>=2&&coefficient<1;
 if(slot->loop)slot->decayCoefficient=float(std::pow(coefficient,step));
}
void PianoRenderer::damp(Voice& voice) noexcept {
 voice.held=false;
 // Do not reset sample/position or create a new attack at Note Off.
 // Match the former release envelope, including high notes, without a Damper Top policy.
 voice.released=true;
}
void PianoRenderer::noteOff(uint8_t channel,uint8_t note) noexcept {
 Voice* oldest=nullptr;
 for(auto& v:voices_)if(v.sample&&v.keyDown&&v.channel==channel&&v.note==note&&(!oldest||v.age<oldest->age))oldest=&v;
 if(!oldest)return;
 oldest->keyDown=false;
 if(pedals_[channel])oldest->held=true;
 else damp(*oldest);
}
void PianoRenderer::midi(uint8_t status,uint8_t note,uint8_t value) noexcept {
 if(note>127||value>127)return;
 const uint8_t channel=status&15,type=status&0xf0;
 if(type==0x90){if(value)noteOn(channel,note,value);else noteOff(channel,note);}
 else if(type==0x80)noteOff(channel,note);
 else if(type==0xe0){bend_[channel]=int(note)+(int(value)<<7)-8192;updatePitch(channel);}
 else if(type==0xb0){
  if(note==7||note==11){(note==7?volume_[channel]:expression_[channel])=value/127.f;target_[channel]=volume_[channel]*expression_[channel];levelDirty_|=uint16_t(1u<<channel);return;}
  if(note==121){bend_[channel]=0;updatePitch(channel);volume_[channel]=expression_[channel]=target_[channel]=1;levelDirty_|=uint16_t(1u<<channel);}
  if(note==64||note==121){
   const bool pressed=note==64&&value>=64;
   if(pedals_[channel]&&!pressed)for(auto& v:voices_)if(v.sample&&v.channel==channel&&v.held)damp(v);
   pedals_[channel]=pressed;
  }else if(note==120){for(auto& v:voices_)if(v.channel==channel)v=Voice{};}
  else if(note==123){for(auto& v:voices_)if(v.sample&&v.channel==channel&&v.keyDown){v.keyDown=false;if(pedals_[channel])v.held=true;else damp(v);}}
 }
}
void PianoRenderer::render(float& left,float& right) noexcept {
 left=right=0;
 if(levelDirty_)for(unsigned ch=0;ch<16;++ch)if(levelDirty_&(1u<<ch)){level_[ch]+=levelAlpha_*(target_[ch]-level_[ch]);if(std::abs(target_[ch]-level_[ch])<1e-7f){level_[ch]=target_[ch];levelDirty_&=uint16_t(~(1u<<ch));}}
 for(auto& voice:voices_){
  if(!voice.sample)continue;
  const auto& s=*voice.sample;
  const size_t end=voice.loop?size_t(s.loopStart)+s.loopLength:s.l.size();
  if(voice.loop&&voice.position>=double(end))voice.position=s.loopStart+std::fmod(voice.position-s.loopStart,double(s.loopLength));
  if(voice.position>=double(s.l.size())){voice=Voice{};continue;}
  const size_t at=size_t(voice.position);
  size_t next=at+1;
  if(voice.loop&&next>=end)next=s.loopStart;
  else if(next>=s.l.size())next=at; // Last frame is valid; no out-of-buffer interpolation.
  // Introduce compensation over the first loop, keeping gain continuous at entry.
  const double blend=voice.loop?std::clamp((voice.sourceAge-s.loopStart)/s.loopLength,0.,1.):0.;
  const float correction=1+float(blend)*(loopGainAt(s,voice.position)-1);
  const float t=float(voice.position-double(at)),scale=correction*voice.amplitude*voice.envelope*voice.decayEnvelope*kPcmScale*level_[voice.channel];
  left+=(float(s.l[at])+(float(s.l[next])-s.l[at])*t)*scale;
  const auto& r=s.r.size()==s.l.size()?s.r:s.l;
  right+=(float(r[at])+(float(r[next])-r[at])*t)*scale;
  voice.position+=voice.step;voice.sourceAge+=voice.step;
  if(voice.released){voice.envelope*=controls_.softRelease?softReleaseCoefficient_:releaseCoefficient_;if(voice.envelope<kVoiceFloor)voice=Voice{};}
  if(voice.loop&&voice.sourceAge>=s.loopStart){voice.decayEnvelope*=voice.decayCoefficient;if(voice.decayEnvelope<0.000001f)voice=Voice{};}
  else if(!voice.loop&&voice.position>=double(s.l.size()))voice=Voice{};
 }
 // A neutral setting is an exact bypass; the filter state still follows the signal.
 lowLeft_+=toneAlpha_*(left-lowLeft_);lowRight_+=toneAlpha_*(right-lowRight_);
 if(std::abs(lowLeft_)<1e-20f)lowLeft_=0;if(std::abs(lowRight_)<1e-20f)lowRight_=0;
 const float target=controls_.timbre==1?-.5f:controls_.timbre==2?.5f:0.f;
 timbreBlend_+=smoothAlpha_*(target-timbreBlend_);
 if(std::abs(timbreBlend_)<1e-8f&&target==0)timbreBlend_=0;
 left+=timbreBlend_*(left-lowLeft_);right+=timbreBlend_*(right-lowRight_);
}
}
