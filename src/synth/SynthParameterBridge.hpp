#pragma once
#include "SynthParameters.hpp"
#include "DistrhoPlugin.hpp"
#include <string>
#include <vector>
#include <cstdio>
namespace nord::synth {
// Metadata/host bridge only; no signal processing or second parameter store.
struct SynthParameterBridge {
 static void initialize(size_t index,DISTRHO::Parameter& p){initializeDefinition(parameterDefinitions[index],p);}
 static void initializeDefinition(const ParameterDefinition& d,DISTRHO::Parameter& p){p.name=d.name;p.symbol=d.symbol;p.hints=DISTRHO::kParameterIsAutomatable;p.groupId=d.group;p.ranges.min=d.minimum;p.ranges.max=d.maximum;p.ranges.def=d.initial;
  const std::string symbol=d.symbol;
  auto ends=[&](const char* suffix){std::string tail=suffix;return symbol.size()>=tail.size()&&symbol.compare(symbol.size()-tail.size(),tail.size(),tail)==0;};
  if(ends("_ms"))p.unit="ms";else if(ends("_hz"))p.unit="Hz";else if(ends("_cents"))p.unit="cents";else if(ends("_semitones")||ends("bend_range"))p.unit="semitones";
  // Explicit signed scale points also drive the generic Oram encoder labels.
  // These configure limits; the position arrives exclusively as MIDI events.
  if(symbol=="pitch_bend_up"||symbol=="pitch_bend_down"){
   p.unit="semitones";p.hints|=DISTRHO::kParameterIsInteger;
   p.enumValues.count=25;p.enumValues.restrictedMode=true;
   p.enumValues.values=new DISTRHO::ParameterEnumerationValue[25];
   for(int i=0;i<25;++i){const int value=symbol=="pitch_bend_up"?i:i-24;char label[8];
    std::snprintf(label,sizeof(label),symbol=="pitch_bend_up"?"%+d":"%d",value);
    p.enumValues.values[i]=DISTRHO::ParameterEnumerationValue(float(value),label);
   }return;
  }
  if(!*d.labels)return;
  p.hints|=DISTRHO::kParameterIsInteger;std::string labels=d.labels;if(labels=="integer")return;
  std::vector<std::string> values;size_t at=0;
  for(;;){auto end=labels.find('|',at);values.push_back(labels.substr(at,end-at));if(end==std::string::npos)break;at=end+1;}
  p.enumValues.count=uint8_t(values.size());p.enumValues.restrictedMode=true;p.enumValues.values=new DISTRHO::ParameterEnumerationValue[values.size()];
  for(size_t i=0;i<values.size();++i)p.enumValues.values[i]=DISTRHO::ParameterEnumerationValue(float(i),values[i].c_str());
 }
 static void stageGroup(uint32_t id,DISTRHO::PortGroup& p){
  const char* names[]={"Sample","Amp","Filter","Filter EG","LFO","Voice","Performance"};
  const char* symbols[]={"sample","amp","filter","filter_eg","lfo","voice","performance"};p.name=names[id<7?id:0];p.symbol=symbols[id<7?id:0];
 }
 static void group(uint32_t id,DISTRHO::PortGroup& p){
  const char* names[]={"SYN AMP","SYN FILTER","SYN FILTER EG","SYN LFO1","SYN LFO2","SYN PITCH","SYN VOICE"};char symbol[32],label[32];
  if(id<7){p.name=names[id];std::snprintf(symbol,sizeof(symbol),"synth_group_%u",id);}
  else{std::snprintf(label,sizeof(label),"SYN MOD %02u",id-6);std::snprintf(symbol,sizeof(symbol),"synth_mod_%02u",id-6);p.name=label;}
  p.symbol=symbol;
 }
};
}
