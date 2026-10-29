#pragma once
#include "SynthParameters.hpp"
#include <array>
#include <cstdio>
#include <string_view>
namespace nord {
// One canonical public endpoint per value. Layout is shared by DPF, the desktop
// UI, the preset generator and Oram. Beta ABI v3 deliberately removes aliases.
struct NordControls {
 enum {Gain,Transpose,BendUp,BendDown,Touch,SoftRelease,Timbre};
 enum Scope:unsigned {Piano=1,Sample=2,Pure=4,All=7,Shared=6};
 static constexpr unsigned count=125,groupCount=36,modFirst=16;
 struct Definition {synth::ParameterDefinition parameter{};int internal=-1;};
 struct Group {const char* name;unsigned scope;};
 static unsigned controlScope(unsigned index){
  // General is shared, but piano deliberately has no Pitch Bend controls.
  return index==BendUp||index==BendDown?Shared:group(definitions()[index].parameter.group).scope;
 }
 static Group group(unsigned id){
  static constexpr const char* names[]={"General","Piano","OSC 1","OSC 2","Mixer","Pitch","Filter","Filter color","Filter EG","Filter EG amt","Amplifier","Amp EG","LFO 1","LFO 1 phase","LFO 2","LFO 2 phase","Mod 01","Mod 02","Mod 03","Mod 04","Mod 05","Mod 06","Mod 07","Mod 08","Mod 09","Mod 10","Mod 11","Mod 12","Mod 13","Mod 14","Mod 15","Mod 16","Mod 17","Mod 18","Voice","Performance"};
  return {names[id],id==0?All:id==1?Piano:id<=4?Pure:Shared};
 }
 static const std::array<Definition,count>& definitions(){
  static const auto result=[](){
   using namespace synth;std::array<Definition,count> out{};unsigned at=0;
   auto direct=[&](ParameterDefinition d){out[at++]={d,-1};};
   auto add=[&](int internal,unsigned group,const char* name=nullptr){auto d=parameterDefinitions[internal];d.group=group;if(name)d.name=name;else if(std::string_view(d.name).substr(0,6)=="Synth ")d.name+=6;out[at++]={d,internal};};
   direct({"gain","Volume",0,1,.3f,0,""});direct({"transpose","Transpose",-24,24,0,0,"integer"});
   add(bend_range,0,"Pitch Bend Range Up");add(bend_down,0,"Pitch Bend Range Down");
   direct({"kb_touch","Keyboard Touch",0,3,0,1,"Normal|Heavy|Medium|Light"});direct({"soft_release","Soft Release",0,1,0,1,"Off|On"});direct({"piano_timbre","Piano Timbre",0,2,0,1,"Neutral|Soft shelf|Bright shelf"});
   for(int p:{pure_osc1_waveform,pure_osc1_octave,pure_osc1_semitone,pure_osc1_detune_cents})add(p,2);
   for(int p:{pure_osc2_enabled,pure_osc2_waveform,pure_osc2_octave,pure_osc2_semitone})add(p,3);
   for(int p:{pure_osc1_level,pure_osc2_level,pure_osc2_detune_cents})add(p,4);
   add(fine_cents,5,"Fine Tune");
   add(filter_mode,6,"Mode");add(cutoff_hz,6,"Cutoff");add(resonance,6,"Resonance");add(stage_drive,6,"Drive");
   add(keytrack,7,"Key Track");add(stage_bright,7,"Brightness");
   for(int p:{feg_attack_ms,feg_decay_ms,feg_sustain,feg_release_ms})add(p,8);
   add(feg_depth_semitones,9,"Amount");add(stage_feg_velocity,9,"EG Velocity");
   add(velocity_gain_depth,10,"Velocity");
   for(int p:{amp_attack_ms,amp_decay_ms,amp_sustain,amp_release_ms})add(p,11);
   for(int p:{lfo1_rate_hz,lfo1_wave,lfo1_depth,lfo1_trigger})add(p,12);add(lfo1_phase,13);
   for(int p:{lfo2_rate_hz,lfo2_wave,lfo2_depth,lfo2_trigger})add(p,14);add(lfo2_phase,15);
   for(unsigned route=0;route<18;++route)for(unsigned field=0;field<4;++field)add(mod_01_source+4*route+field,modFirst+route);
   for(int p:{voice_mode,polyphony,unison_voices,unison_detune_cents})add(p,34);
   for(int p:{unison_spread,glide_ms,glide_mode})add(p,35);
   return out;
  }();return result;
 }
 static float clamp(unsigned index,float value){const auto&d=definitions()[index].parameter;return std::clamp(*d.labels?std::round(value):value,d.minimum,d.maximum);}
 template<class Get> static synth::SynthParameters synthParameters(Get get){
  synth::SynthParameters result;for(unsigned i=0;i<count;++i)if(definitions()[i].internal>=0)result.set(definitions()[i].internal,get(i));
  result.set(synth::coarse_semitones,get(Transpose));return result;
 }
};
}
