#pragma once
#include "PianoEngine.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
namespace nord {
constexpr double kQ23Unity=8388608.;
inline double pianoPitchStep(double note,int root,double hostRate) noexcept {
 return kNpnoRate/hostRate*std::exp2((note-root)/12.);
}
inline double sustainCoefficient(const Sample& s) noexcept {
 // Experimental: one coefficient, not the unsupported product of two poles.
 auto valid=[](uint32_t c){return c>0&&c<kQ23Unity;};
 return valid(s.decayLadder[0])?s.decayLadder[0]/kQ23Unity:valid(s.decay)?s.decay/kQ23Unity:1.;
}
// Worker only. A circular 100 ms RMS envelope; shared gain preserves stereo phase.
// Attenuation only, bounded to 12 dB. Never changes the original PCM vectors.
inline float loopGainAt(const Sample& s,double position) noexcept;
inline void prepareLoopGain(Sample& s) {
 s.loopGain.clear();if(!s.hasLoop()||s.loopLength<3500)return;
 const size_t n=s.loopLength,half=1750;
 std::vector<double> energy(n+1);for(size_t i=0;i<n;++i){double l=s.l[s.loopStart+i],r=s.r.empty()?l:s.r[s.loopStart+i];energy[i+1]=energy[i]+(l*l+r*r)*.5;}
 auto sum=[&](size_t begin,size_t count){size_t end=begin+count;return end<=n?energy[end]-energy[begin]:energy[n]-energy[begin]+energy[end-n];};
 constexpr size_t points=256;std::array<double,points> rms{};
 for(size_t i=0;i<points;++i){size_t center=i*n/points;rms[i]=std::sqrt(sum((center+n-half)%n,2*half)/(2*half));}
 const auto bounds=std::minmax_element(rms.begin(),rms.end());
 if(*bounds.first<1||*bounds.second/ *bounds.first<std::pow(10.,3./20))return;
 s.loopGain.resize(points);
 for(size_t i=0;i<points;++i)s.loopGain[i]=float(std::max(std::pow(10.,-12./20),*bounds.first/rms[i]));
 // Reject candidates that worsen detrended 100 ms RMS at any of eight phases, including the selected decay pole.
 // This is a conservative signal-domain guard, not a claim of auditory transparency.
 double improvement=0;
 const double energyPole=std::pow(sustainCoefficient(s),2);
 for(int channel=0;channel<(s.r.empty()?1:2);++channel)
 for(size_t offset=0;offset<3500;offset+=438){
  std::vector<double> before,after;
  for(size_t begin=offset;begin+3500<=n;begin+=3500){double raw=0,corrected=0,weight=1;
   for(size_t j=begin;j<begin+3500;++j){double l=s.l[s.loopStart+j],r=s.r.empty()?l:s.r[s.loopStart+j];double e=(channel?r*r:l*l)*weight;weight*=energyPole;double gain=loopGainAt(s,s.loopStart+j);raw+=e;corrected+=e*gain*gain;}
   before.push_back(10*std::log10(std::max(raw,1.)));after.push_back(10*std::log10(std::max(corrected,1.)));
  }
  if(before.size()<3){s.loopGain.clear();return;}
  auto range=[](const std::vector<double>& values){double center=(values.size()-1)*.5,mean=std::accumulate(values.begin(),values.end(),0.)/values.size(),xy=0,xx=0;
   for(size_t i=0;i<values.size();++i){xy+=(i-center)*(values[i]-mean);xx+=(i-center)*(i-center);}
   double slope=xy/xx,lo=1e30,hi=-1e30;for(size_t i=0;i<values.size();++i){double v=values[i]-i*slope;lo=std::min(lo,v);hi=std::max(hi,v);}return hi-lo;};
  double delta=range(before)-range(after);if(delta<-.1){s.loopGain.clear();return;}improvement+=delta;
 }
 if(improvement/(s.r.empty()?8:16)<.5)s.loopGain.clear();
}
inline float loopGainAt(const Sample& s,double position) noexcept {
 if(s.loopGain.empty()||position<s.loopStart)return 1;
 double phase=(position-s.loopStart)*s.loopGain.size()/s.loopLength;
 size_t i=size_t(phase)%s.loopGain.size();double t=phase-std::floor(phase);
 t=t*t*(3-2*t);
 return float(s.loopGain[i]+(s.loopGain[(i+1)%s.loopGain.size()]-s.loopGain[i])*t);
}
}
