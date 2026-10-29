#pragma once
#include "Nsmp4Instrument.hpp"
#include <algorithm>
#include <cmath>
namespace nord::synth {
class Nsmp4SamplePlayer {
 const SampleData* sample_=nullptr;double position_=0,step_=1;
public:
 void start(const SampleData* sample) noexcept {sample_=sample;position_=0;}
 static double rate(const SampleData& sample,const Zone& z,double note,double host,double additional=0) noexcept {
  return sample.sampleRate/host*std::exp2(((z.unpitched?0:note-int(z.rootKey))+z.tuneCents/100.+additional)/12.);
 }
 void setRate(double rate)noexcept {step_=std::isfinite(rate)&&rate>0?rate:1;}
 bool active()const noexcept{return sample_!=nullptr;}
 double position()const noexcept{return position_;}
 void render(float& l,float& r)noexcept {
  l=r=0;if(!sample_)return;const auto& s=*sample_;
  if(s.loop.enabled&&position_>=s.loop.end)position_=s.loop.begin+std::fmod(position_-s.loop.begin,double(s.loop.end-s.loop.begin));
  if(position_>=s.frames){sample_=nullptr;return;}
  size_t i=size_t(position_),j=i+1;float t=float(position_-i);
  if(s.loop.enabled&&j>=s.loop.end)j=size_t(s.loop.begin);else if(j>=s.frames)j=i;
  l=s.left[i]+(s.left[j]-s.left[i])*t;
  r=s.channels==2?s.right[i]+(s.right[j]-s.right[i])*t:l;
  position_+=step_;if(!s.loop.enabled&&position_>=s.frames)sample_=nullptr;
 }
};
}
