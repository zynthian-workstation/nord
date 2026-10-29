#pragma once
#include "SynthParameters.hpp"
namespace nord::synth {
struct StageSampleControls {
 static constexpr size_t count=28;
 static constexpr std::array<ParameterDefinition,count> definitions{{
  {"sample_bright","Sample Bright",0.0f,1.0f,0.0f,0,"Off|On"},
  {"amp_attack_ms","Amp Attack",0.0f,30000.0f,0.0f,1,""},
  {"amp_decay_ms","Amp Decay",0.0f,30000.0f,0.0f,1,""},
  {"amp_sustain","Amp Sustain",0.0f,1.0f,1.0f,1,""},
  {"amp_release_ms","Amp Release",0.0f,30000.0f,5.0f,1,""},
  {"amp_velocity","Amp Velocity",0.0f,1.0f,1.0f,1,""},
  {"filter_on","Filter On",0.0f,1.0f,0.0f,2,"Off|On"},
  {"filter_type","Filter Type",0.0f,2.0f,0.0f,2,"LP12|HP|BP"},
  {"filter_freq_hz","Filter Freq",20.0f,20000.0f,20000.0f,2,""},
  {"filter_resonance","Filter Resonance",0.0f,1.0f,0.0f,2,""},
  {"filter_drive","Filter Drive",0.0f,1.0f,0.0f,2,""},
  {"filter_tracking","Filter Tracking",0.0f,3.0f,0.0f,2,"Off|1/3|2/3|Full"},
  {"filter_eg_attack_ms","Filter EG Attack",0.0f,30000.0f,0.0f,3,""},
  {"filter_eg_decay_ms","Filter EG Decay",0.0f,30000.0f,0.0f,3,""},
  {"filter_eg_sustain","Filter EG Sustain",0.0f,1.0f,0.0f,3,""},
  {"filter_eg_release_ms","Filter EG Release",0.0f,30000.0f,0.0f,3,""},
  {"filter_eg_amount","Filter EG Amount",-60.0f,60.0f,0.0f,3,""},
  {"filter_eg_velocity","Filter EG Velocity",0.0f,1.0f,0.0f,3,""},
  {"lfo_wave","LFO Wave",0.0f,4.0f,1.0f,4,"Square|Triangle|Ramp Down|Ramp Up|S&H"},
  {"lfo_rate_hz","LFO Rate",0.02f,40.0f,5.0f,4,""},
  {"lfo_amount","LFO Amount",0.0f,1.0f,0.0f,4,""},
  {"lfo_destination","LFO Destination",0.0f,2.0f,0.0f,4,"Off|Pitch|Filter"},
  {"voice_mode","Voice Mode",0.0f,2.0f,0.0f,5,"Poly|Mono|Legato"},
  {"glide_time_ms","Glide Time",0.0f,2000.0f,0.0f,5,""},
  {"unison","Unison",0.0f,3.0f,0.0f,5,"Off|1|2|3"},
  {"pitch_bend_range","Pitch Bend Range",0.0f,24.0f,2.0f,6,"integer"},
  {"vibrato_source","Vibrato Source",0.0f,2.0f,0.0f,6,"Off|Wheel|Aftertouch"},
  {"vibrato_amount","Vibrato Amount",0.0f,1.0f,0.0f,6,""},
 }};
 static SynthParameters translate(const std::array<float,count>& v){
  SynthParameters p;
  p.set(amp_attack_ms,v[1]);
  p.set(amp_decay_ms,v[2]);
  p.set(amp_sustain,v[3]);
  p.set(amp_release_ms,v[4]);
  p.set(velocity_gain_depth,v[5]);
  p.set(cutoff_hz,v[8]);
  p.set(resonance,v[9]);
  p.set(keytrack,v[11]);
  p.set(feg_attack_ms,v[12]);
  p.set(feg_decay_ms,v[13]);
  p.set(feg_sustain,v[14]);
  p.set(feg_release_ms,v[15]);
  p.set(feg_depth_semitones,v[16]);
  p.set(lfo1_rate_hz,v[19]);
  p.set(lfo1_depth,v[20]);
  p.set(voice_mode,v[22]);
  p.set(glide_ms,v[23]);
  p.set(bend_range,v[25]);
  p.set(filter_mode,v[6] ? v[7]+1 : 0);
  p.set(stage_bright,v[0]);p.set(stage_drive,v[10]);p.set(stage_feg_velocity,v[17]);p.set(stage_lfo_down,v[18]==2?1:0);
  constexpr int waves[]={3,1,2,2,5}; p.values[lfo1_wave]=float(waves[int(v[18])]);
  p.set(mod_01_source,v[21]?1:0);p.set(mod_01_target,v[21]);p.set(mod_01_depth,1);
  p.set(unison_voices,v[24]+1);
  p.set(glide_mode,v[23]>0?1:0);
  p.set(lfo2_depth,v[27]);p.set(mod_02_source,v[26]?2:0);p.set(mod_02_target,1);p.set(mod_02_via,v[26]==1?1:v[26]==2?3:0);p.set(mod_02_depth,0.5f/24);
  return p;
 }
};
}
