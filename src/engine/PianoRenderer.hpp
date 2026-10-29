#pragma once
#include "PianoEngine.hpp"
#include "PianoControls.hpp"
#include <array>
namespace nord {
// Allocation-free voice renderer. Bank pointers are valid only inside engine's block guard.
class PianoRenderer {
public:
 static constexpr size_t kVoices=48;
 PianoRenderer(){volume_.fill(1);expression_.fill(1);level_.fill(1);target_.fill(1);setSampleRate(44100);}
 void setBank(const Bank* bank) noexcept;
 void setSampleRate(double rate) noexcept;
 void midi(uint8_t status,uint8_t note,uint8_t value) noexcept;
 void render(float& left,float& right) noexcept;
 size_t activeVoices() const noexcept;
 bool keyActive(uint8_t channel,uint8_t note) const noexcept;
 const Sample* newestSample() const noexcept; // Deterministic diagnostics, no allocation.
 void reset() noexcept;
 void setControls(const PianoControls& controls) noexcept;
private:
 struct Voice {
  const Sample* sample=nullptr;
  double position=0,step=1,sourceAge=0;
  float amplitude=0,envelope=1,decayEnvelope=1,decayCoefficient=1;
  bool loop=false;
  uint64_t age=0;
  uint8_t note=0,channel=0,velocity=0;
  bool keyDown=false,held=false,released=false;
 };
 void updatePitch(uint8_t channel) noexcept;
 void noteOn(uint8_t channel,uint8_t note,uint8_t velocity) noexcept;
 void noteOff(uint8_t channel,uint8_t note) noexcept;
 void damp(Voice& voice) noexcept;
 std::array<Voice,kVoices> voices_{};
 std::array<bool,16> pedals_{};
 const Bank* bank_=nullptr;
 uint64_t serial_=0,age_=0;
 double rate_=0;
 float releaseCoefficient_=1,softReleaseCoefficient_=1;
 PianoControls controls_;
 std::array<int,16> bend_{};
 std::array<float,16> volume_,expression_,level_,target_;
 uint16_t levelDirty_=0;
 float lowLeft_=0,lowRight_=0,timbreBlend_=0,toneAlpha_=0,smoothAlpha_=0,levelAlpha_=0;
};
}
