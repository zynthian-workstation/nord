#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <vector>
#include <atomic>
namespace nord::synth {
struct MidiVoiceEvent {
 enum class Type:uint8_t {NoteOn,NoteOff,Sustain,Bend,CC1,ChannelPressure,PolyPressure,AllNotesOff,AllSoundOff,Volume,Expression,ResetControllers};
 Type type=Type::NoteOff;uint32_t sampleOffset=0;uint8_t channel=0,note=0,value=0;int16_t pitchBend14=0;
};
struct RenderContext {double hostSampleRate=44100;uint32_t frameCount=0;const MidiVoiceEvent* events=nullptr;uint32_t eventCount=0;float* outputL=nullptr;float* outputR=nullptr;};
class ScxtBlockAdapter {
 struct Scheduled {uint64_t frame,source,order;MidiVoiceEvent event;};
 static constexpr size_t maxEvents=8192;
 std::array<Scheduled,maxEvents> events_{};size_t count_=0;
 std::vector<float> fifoL_,fifoR_;unsigned cursor_=16;uint64_t frame_=0,order_=0;std::atomic<uint64_t> overflow_{0};
public:
 static constexpr unsigned quantum=16;
 void prepare(uint32_t maximum){fifoL_.assign(size_t(maximum)+32,0);fifoR_.assign(size_t(maximum)+32,0);reset();}
 void reset()noexcept{cursor_=16;frame_=order_=count_=0;}
 uint64_t absoluteFrame()const noexcept{return frame_;}
 uint64_t overflows()const noexcept{return overflow_.load(std::memory_order_relaxed);}
 void addOverflows(uint64_t n)noexcept{overflow_.fetch_add(n,std::memory_order_relaxed);}
 template<class Dispatch,class Generate> void render(const RenderContext& c,Dispatch dispatch,Generate generate) noexcept {
  for(uint32_t i=0;i<c.eventCount;++i){const auto&e=c.events[i];if(e.sampleOffset>=c.frameCount||count_==maxEvents){++overflow_;continue;}uint64_t at=(frame_+e.sampleOffset+15)&~uint64_t(15);events_[count_++]={at,frame_+e.sampleOffset,order_++,e};}
  std::sort(events_.begin(),events_.begin()+count_,[](const auto&a,const auto&b){return a.frame<b.frame||(a.frame==b.frame&&(a.source<b.source||(a.source==b.source&&a.order<b.order)));});
  for(uint32_t f=0;f<c.frameCount;++f){
   if(cursor_==16){size_t consumed=0;while(consumed<count_&&events_[consumed].frame<=frame_){dispatch(events_[consumed].event);++consumed;}if(consumed){std::move(events_.begin()+consumed,events_.begin()+count_,events_.begin());count_-=consumed;}generate(fifoL_.data(),fifoR_.data(),frame_);cursor_=0;}
   c.outputL[f]=fifoL_[cursor_];c.outputR[f]=fifoR_[cursor_];++cursor_;++frame_;
  }
 }
};
}
