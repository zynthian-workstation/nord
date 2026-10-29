#pragma once
#include <algorithm>
namespace nord {
// MIDI's two half-ranges have different denominators. Arguments are signed
// offsets from 8192; both extremes are exact. Range Up is nonnegative; Range Down is nonpositive.
inline double bendSemitones(int offset,double up,double down) noexcept {
 offset=std::clamp(offset,-8192,8191);
 return offset>=0?(offset/8191.)*up:(-offset/8192.)*down;
}
}
