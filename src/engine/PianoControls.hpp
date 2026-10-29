#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace nord {
// Personal development controls, not a reconstruction of Nord's transfer curves.
struct PianoControls {
 float transpose=0,bendUp=2,bendDown=-2;
 int touch=0;        // Normal, Heavy, Medium, Light.
 bool softRelease=false;
 int timbre=0;       // Neutral, Soft, Bright.
};
inline uint8_t touchVelocity(uint8_t velocity,int mode) noexcept {
 if(!velocity||!mode)return velocity;
 constexpr double powers[]={1.,1.2,.9,.7};
 return uint8_t(std::clamp(int(std::lround(127*std::pow(velocity/127.,powers[std::clamp(mode,0,3)]))),1,127));
}
}
