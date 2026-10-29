#include "DistrhoPlugin.hpp"
#include "PianoEngine.hpp"
#include "PianoRenderer.hpp"
#include "InstrumentLibrary.hpp"
#include "SampleSynthEngine.hpp"
#include "SynthParameterBridge.hpp"
#include "NordControls.hpp"
#include <mutex>
#include <cstdio>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
USE_NAMESPACE_DISTRHO;
class NordPlugin final:public Plugin {
 nord::PianoRenderer renderer;
 nord::InstrumentLibrary library;
 std::unique_ptr<nord::PianoEngine> piano;
 nord::synth::SampleSynthEngine synth; // Destroy voices/pins before worker/banks.
 std::array<nord::synth::MidiVoiceEvent,8192> synthEvents_{};
 mutable std::mutex stateMutex; // State callbacks only, never audio/parameters.
 int audioSelection_=-1;
 std::atomic<uint64_t> presetGeneration_{0};
 uint64_t audioPresetGeneration_=0;
 std::string unresolved;
 std::atomic<float> gain{0.3f};
 static_assert(std::atomic<float>::is_always_lock_free,"Gain must be lock-free");
 static constexpr unsigned kParameterCount=nord::NordControls::count;
 std::array<std::atomic<float>,kParameterCount> parameters_{};
public:
 NordPlugin():Plugin(kParameterCount,0,1),library({}){
  for(unsigned i=0;i<kParameterCount;++i)parameters_[i].store(nord::NordControls::definitions()[i].parameter.initial,std::memory_order_relaxed);
  library.loadCatalog(getBundlePath()?getBundlePath():"");
  for(const auto& d:library.diagnostics())std::fprintf(stderr,"[Nord] %s\n",d.c_str());
  std::vector<std::string> paths;
  for(const auto& i:library.instruments())paths.push_back(i.path.string());
  piano=std::make_unique<nord::PianoEngine>(std::move(paths));
  // Catalog only at startup. LV2 State/preset selection starts the asynchronous load.
 }

protected:
 const char* getLabel() const override{return DISTRHO_PLUGIN_NAME;}
 const char* getMaker() const override{return "Nord Project";}
 const char* getLicense() const override{return "GPL-3.0-or-later";}
 uint32_t getVersion() const override{return d_version(1,0,0);}
 int64_t getUniqueId() const override{return d_cconst('N','O','R','D');}
 void initParameter(uint32_t index,Parameter& p) override {
  const auto& d=nord::NordControls::definitions()[index].parameter;
  nord::synth::SynthParameterBridge::initializeDefinition(d,p);
  if(index==nord::NordControls::BendUp||index==nord::NordControls::BendDown||index==nord::NordControls::Transpose)p.unit="semitones";
  if(std::string(d.symbol)=="soft_release"||std::string(d.symbol)=="pure_osc2_enabled")p.hints|=kParameterIsBoolean;
 }
 void initPortGroup(uint32_t id,PortGroup& group) override {
  // DPF requires a symbol for EVERY sequential id, including intermediate groups.
  auto def=nord::NordControls::group(id);char symbol[32];std::snprintf(symbol,sizeof(symbol),"nord_group_%02u",id);
  group.name=def.name;group.symbol=symbol;
 }
 float getParameterValue(uint32_t index) const override {
  return index<kParameterCount?parameters_[index].load(std::memory_order_relaxed):0;
 }
 void setParameterValue(uint32_t index,float value) override {
  if(index>=kParameterCount||!std::isfinite(value))return;
  value=nord::NordControls::clamp(index,value);parameters_[index].store(value,std::memory_order_relaxed);
  if(index==nord::NordControls::Gain)gain.store(value,std::memory_order_relaxed);
 }
 void initState(uint32_t, State& state) override {
  state.key="instrument_id";state.label="Instrument ID";
  state.hints=kStateIsHostWritable;
  state.defaultValue="";
 }
 String getState(const char* key) const override {
  if(std::string(key)!="instrument_id")return String();
  std::lock_guard<std::mutex> lock(stateMutex);
  const int index=piano->selected();
  if(index>=0)return String(library.instruments()[size_t(index)].id.c_str());
  return String(unresolved.c_str());
 }
 void setState(const char* key,const char* value) override {
  if(std::string(key)!="instrument_id")return;
  std::lock_guard<std::mutex> lock(stateMutex);
  unresolved=value;
  const auto* found=library.resolve(value);
  if(found)piano->select(int(found-library.instruments().data()));
  else {
   piano->select(-1);
   if(*value)std::fprintf(stderr,"[Nord] Instrumento ausente: %s; verifique o bundle instalado\n",value);
  }
  // MIDI wheel position is performance state, not part of an instrument preset.
  // Publish even when selecting the same instrument again. Only the audio thread
  // touches DSP state, before processing any new MIDI events in that block.
  presetGeneration_.fetch_add(1,std::memory_order_release);
 }

 void run(const float**,float** outputs,uint32_t frames,const MidiEvent* events,uint32_t eventCount) override {
  nord::PianoControls controls;
  controls.touch=int(parameters_[nord::NordControls::Touch].load());controls.softRelease=parameters_[nord::NordControls::SoftRelease].load()>=.5f;
  controls.timbre=int(parameters_[nord::NordControls::Timbre].load());
  // Piano exposes transpose, but neither Pitch Bend direction applies to NPNO.
  // Keep the LV2 range ports for Sample/Pure without leaking them into piano.
  controls.transpose=parameters_[nord::NordControls::Transpose].load();controls.bendUp=controls.bendDown=0;
  const int selected=piano->selected();if(selected!=audioSelection_){synth.resetVoices();audioSelection_=selected;}
  const auto presetGeneration=presetGeneration_.load(std::memory_order_acquire);
  if(presetGeneration!=audioPresetGeneration_){synth.resetPitchBend();audioPresetGeneration_=presetGeneration;}
  const auto* bank=piano->beginBlock();
  renderer.setBank(bank&&!bank->synth&&!bank->pure?bank:nullptr);synth.setBank(bank);
  renderer.setSampleRate(getSampleRate());renderer.setControls(controls);
  auto synthParameters=nord::NordControls::synthParameters([&](unsigned i){return parameters_[i].load(std::memory_order_relaxed);});
  synth.setParameters(synthParameters);
  uint32_t synthCount=0;
  using E=nord::synth::MidiVoiceEvent;
  for(uint32_t i=0;i<eventCount;++i){const auto&e=events[i];if(e.size<2||e.size>MidiEvent::kDataSize)continue;const uint8_t status=e.data[0]&0xf0;E m;m.sampleOffset=e.frame;m.channel=e.data[0]&15;m.note=e.data[1];m.value=e.size>=3?e.data[2]:e.data[1];bool supported=true;
   if(status==0x90&&e.size>=3){m.type=E::Type::NoteOn;if(m.value&&(!bank||(!bank->synth&&!bank->pure)))continue;}
   else if(status==0x80&&e.size>=3)m.type=E::Type::NoteOff;
   else if(status==0xe0&&e.size>=3){m.type=E::Type::Bend;m.pitchBend14=int16_t((int(e.data[2])<<7)+e.data[1]-8192);}
   else if(status==0xd0)m.type=E::Type::ChannelPressure;
   else if(status==0xa0&&e.size>=3)m.type=E::Type::PolyPressure;
   else if(status==0xb0&&e.size>=3){if(m.note==64)m.type=E::Type::Sustain;else if(m.note==1)m.type=E::Type::CC1;else if(m.note==7)m.type=E::Type::Volume;else if(m.note==11)m.type=E::Type::Expression;else if(m.note==121)m.type=E::Type::ResetControllers;else if(m.note==120)m.type=E::Type::AllSoundOff;else if(m.note==123)m.type=E::Type::AllNotesOff;else supported=false;}
   else supported=false;
   if(supported){if(synthCount<synthEvents_.size())synthEvents_[synthCount++]=m;else synth.addMidiOverflows(1);}
  }
  synth.render({getSampleRate(),frames,synthEvents_.data(),synthCount,outputs[0],outputs[1]});
  const float blockGain=gain.load(std::memory_order_relaxed);
  uint32_t next=0;
  for(uint32_t frame=0;frame<frames;++frame){
   while(next<eventCount&&events[next].frame<=frame){
    const auto& event=events[next++];
    if(event.size>=3&&event.size<=MidiEvent::kDataSize&&(event.data[0]&0xf0)!=0xe0)renderer.midi(event.data[0],event.data[1],event.data[2]);
   }
   float l,r;renderer.render(l,r);
   outputs[0][frame]=outputs[0][frame]*blockGain+std::clamp(l*blockGain,-1.f,1.f);
   outputs[1][frame]=outputs[1][frame]*blockGain+std::clamp(r*blockGain,-1.f,1.f);
  }
  piano->endBlock();
 }
};
Plugin* DISTRHO::createPlugin(){return new NordPlugin();}
