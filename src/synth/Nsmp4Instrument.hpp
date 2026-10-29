#pragma once
#include <cstdint>
#include <array>
#include <memory>
#include <vector>
namespace nord::synth {
struct LoopInfo {bool enabled=false;uint64_t begin=0,end=0;};
struct SampleData {
 uint32_t channels=0,sampleRate=0;uint64_t frames=0;
 std::vector<float> left,right;LoopInfo loop;
};
struct Zone {
 uint8_t keyLow=0,keyHigh=127,rootKey=60,velLow=1,velHigh=127;
 float tuneCents=0,gainLinear=1;bool unpitched=false;uint32_t sampleIndex=0;
};
struct Instrument {
 std::vector<Zone> zones;std::vector<std::shared_ptr<const SampleData>> samples;
 // Authored v21 keyboard levels, u24 / 2^20. Older layouts stay neutral.
 float instrumentGain=1;
 std::array<float,128> keyGain=[](){std::array<float,128> g{};g.fill(1);return g;}();
 float playbackGain(uint8_t note)const noexcept{return instrumentGain*keyGain[note];}
 uint32_t contentVersion=0,mapVersion=0;size_t bytes=0;
 const Zone* zoneFor(int note,int velocity) const noexcept {
  if(note<0||note>127||velocity<1||velocity>127)return nullptr;
  for(const auto& z:zones)if(note>=z.keyLow&&note<=z.keyHigh&&velocity>=z.velLow&&velocity<=z.velHigh)return &z;
  return nullptr;
 }
};
}
