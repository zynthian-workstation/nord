#include "PianoEngine.hpp"
#include "PianoSustain.hpp"
#include "Nsmp4Reader.hpp"
#include "EngineResolver.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <unistd.h>
#ifndef NORD_PIANO_CACHE_MIB
#define NORD_PIANO_CACHE_MIB 2048
#endif
namespace nord {
namespace {
using Clock=std::chrono::steady_clock;
double milliseconds(Clock::time_point from){return std::chrono::duration<double,std::milli>(Clock::now()-from).count();}
}
size_t PianoEngine::defaultCacheBudget() noexcept {
 const size_t maximum=size_t(NORD_PIANO_CACHE_MIB)*1024*1024;
 const long pages=sysconf(_SC_PHYS_PAGES),pageSize=sysconf(_SC_PAGESIZE);
 if(pages<=0||pageSize<=0)return std::min(maximum,size_t(256)*1024*1024);
 return size_t(std::min(uint64_t(maximum),uint64_t(pages)*uint64_t(pageSize)/4));
}
PianoEngine::PianoEngine(std::vector<std::string> paths,size_t cacheBudget)
 :paths_(std::move(paths)),cacheBudget_(cacheBudget),loader_([this]{work();}){}
PianoEngine::~PianoEngine(){stop_.store(true,std::memory_order_release);if(loader_.joinable())loader_.join();}
void PianoEngine::select(int index) noexcept {
 if(index < -1 || index>=int(paths_.size()))return;
 auto previous=request_.load(std::memory_order_relaxed);
 do {
  if(requestIndex(previous)==index)return; // Same active/pending selection is a no-op.
  const uint64_t next=((previous>>32)+1)<<32 | uint32_t(index+1);
  if(request_.compare_exchange_weak(previous,next,std::memory_order_seq_cst))return;
 }while(true);
}
void PianoEngine::remember(std::unique_ptr<Bank> bank) noexcept {
 if(bank&&bank->synthPins.load(std::memory_order_acquire)){retiredBytes_+=bank->bytes;while(!cache_.empty()&&cacheBytes_>(retiredBytes_<cacheBudget_?cacheBudget_-retiredBytes_:0)){cacheBytes_-=cache_.back()->bytes;cache_.pop_back();}for(auto& slot:retired_)if(!slot){slot=std::move(bank);return;}std::terminate();}
 const size_t inactiveBudget=retiredBytes_<cacheBudget_?cacheBudget_-retiredBytes_:0;
 if(!bank||bank->bytes>inactiveBudget)return; // Oversized active bank works, but is not cached.
 while(cacheBytes_>inactiveBudget-bank->bytes){cacheBytes_-=cache_.back()->bytes;cache_.pop_back();}
 const size_t bytes=bank->bytes;
 try{cache_.push_front(std::move(bank));cacheBytes_+=bytes;}
 catch(const std::bad_alloc&){std::fprintf(stderr,"[Nord] cache sem memoria; banco nao retido\n");}
}
std::unique_ptr<Bank> PianoEngine::load(int index,uint64_t token,LoadStats& stats){
 const auto started=Clock::now();
 auto cancelled=[&]{return stop_.load(std::memory_order_relaxed)||request_.load(std::memory_order_acquire)!=token;};
 if(paths_[size_t(index)]=="builtin:pure"){auto bank=std::make_unique<Bank>();bank->instrument=index;bank->pure=true;return bank;}
 if(resolveEngine(paths_[size_t(index)])==EngineKind::SampleSynth){
  auto bank=std::make_unique<Bank>();bank->instrument=index;bank->synth=synth::Nsmp4Reader::read(paths_[size_t(index)],cancelled);bank->bytes+=bank->synth->bytes;
  if(retiredBytes_>cacheBudget_+synth::Nsmp4Reader::maxPcmBytes||bank->bytes>cacheBudget_+synth::Nsmp4Reader::maxPcmBytes-retiredBytes_)throw std::runtime_error("Pinned Synth bank memory limit");
  stats.samples=bank->synth->samples.size();stats.pcmBytes=bank->bytes;stats.decodeMs=milliseconds(started);return bank;
 }
 Npno npno;npno.open(paths_[size_t(index)]);
 stats.readMs=npno.timings().readMs;stats.parseMs=npno.timings().parseMs;
 if(cancelled())return {};
 auto bank=std::make_unique<Bank>();bank->instrument=index;bank->damperTop=npno.damperTop();
 bank->metadata=npno.strokes();
 std::vector<const Sample*> samples(npno.strokes().size());
 // Decode only the active, validated playback bank on explicit instrument selection.
 // Full vendor descriptors (including inactive banks) stay in Bank::metadata.
 for(size_t i=0;i<npno.strokes().size();++i){
  if(cancelled())return {};
  const auto& stroke=npno.strokes()[i];
  if(stroke.flags!=uint8_t(SampleBank::Attack))continue;
  auto decoding=Clock::now();PCM pcm=npno.decode(stroke,cancelled);stats.decodeMs+=milliseconds(decoding);
  if(pcm.left.empty()||(npno.channels()==2&&pcm.right.size()!=pcm.left.size()))throw std::runtime_error("PCM vazio ou canais inconsistentes");
  auto sample=std::make_unique<Sample>();sample->root=npno.zones().at(size_t(stroke.zone)).root;
  sample->sourceStroke=i;sample->bank=stroke.flags;sample->layer=stroke.floor;
  sample->decay=stroke.decay;sample->trim=stroke.trim;sample->decayLadder=stroke.decayLadder;
  sample->l.resize(pcm.left.size());if(npno.channels()==2)sample->r.resize(pcm.right.size());
  for(size_t frame=0;frame<pcm.left.size();++frame){
   sample->l[frame]=int16_t(std::clamp(pcm.left[frame],int32_t(-32768),int32_t(32767)));
   if(npno.channels()==2)sample->r[frame]=int16_t(std::clamp(pcm.right[frame],int32_t(-32768),int32_t(32767)));
  }
  if((npno.version()==0x464||npno.version()==0x450)&&stroke.flags<2&&stroke.record[4]==stroke.flags&&stroke.loopLength){
   if(stroke.loopStart<sample->l.size()&&uint64_t(stroke.loopStart)+stroke.loopLength<=sample->l.size()){
    sample->loopStart=stroke.loopStart;sample->loopLength=stroke.loopLength;
   }else std::fprintf(stderr,"[Nord] loop invalido, one-shot: stroke %zu\n",i);
  }
  prepareLoopGain(*sample);
  bank->bytes+=sample->loopGain.capacity()*sizeof(float);
  bank->bytes+=(sample->l.capacity()+sample->r.capacity())*sizeof(int16_t)+sizeof(Sample);
  samples[i]=sample.get();bank->owned.push_back(std::move(sample));
 }
 // Preserve the complete velocity table for Attack; other banks stay metadata-only.
 for(int note=0;note<128;++note){
  if(cancelled())return {};
  if(!npno.hasBank(note,SampleBank::Attack))continue;
  for(int velocity=1;velocity<=127;++velocity){
   const auto& stroke=npno.select(note,velocity,SampleBank::Attack);
   bank->maps[size_t(SampleBank::Attack)][size_t(note)][size_t(velocity)]=samples[size_t(&stroke-npno.strokes().data())];
  }
 }
 bank->bytes+=bank->metadata.capacity()*sizeof(Stroke)+bank->owned.capacity()*sizeof(std::unique_ptr<Sample>);
 if(bank->owned.empty())throw std::runtime_error("Nenhum sample decodificado");
 stats.samples=bank->owned.size();stats.pcmBytes=bank->bytes;
 stats.prepareMs=milliseconds(started)-stats.readMs-stats.parseMs-stats.decodeMs;
 return bank;
}
void PianoEngine::work(){
 uint64_t handled=0,serial=0;
 while(!stop_.load(std::memory_order_acquire)){
  for(auto& retired:retired_)if(retired&&!retired->synthPins.load(std::memory_order_acquire)){auto done=std::move(retired);retiredBytes_-=done->bytes;remember(std::move(done));}
  const uint64_t token=request_.load(std::memory_order_acquire);
  if(token==handled){std::this_thread::sleep_for(std::chrono::milliseconds(1));continue;}
  handled=token;const int index=requestIndex(token);const auto started=Clock::now();
  current_.store(nullptr,std::memory_order_seq_cst);
  loading_.store(index>=0,std::memory_order_release);
  // Drain the previous reader before recycling a bank or changing its publication tag.
  while(reading_.load(std::memory_order_seq_cst)&&!stop_.load(std::memory_order_acquire))std::this_thread::sleep_for(std::chrono::milliseconds(1));
  if(stop_.load(std::memory_order_acquire))break;
  std::unique_ptr<Bank> ready;
  for(auto it=cache_.begin();it!=cache_.end();++it)if((*it)->instrument==index){
   cacheBytes_-=(*it)->bytes;ready=std::move(*it);cache_.erase(it);break;
  }
  if(!ready)for(auto& retired:retired_)if(retired&&retired->instrument==index){retiredBytes_-=retired->bytes;ready=std::move(retired);break;}
  if(published_&&published_->instrument==index)ready=std::move(published_);
  else remember(std::move(published_));
  if(index<0){loading_.store(false,std::memory_order_release);continue;}
  LoadStats stats;stats.token=token;stats.instrument=index;stats.cacheHit=bool(ready);
  try{
   if(!ready)ready=load(index,token,stats);
   if(!ready||request_.load(std::memory_order_acquire)!=token){remember(std::move(ready));continue;}
   stats.pcmBytes=ready->bytes;stats.samples=ready->synth?ready->synth->samples.size():ready->owned.size();stats.cacheBytes=cacheBytes_;
   ready->serial=++serial;ready->requestToken=token;
   const auto publishing=Clock::now();published_=std::move(ready);
   current_.store(published_.get(),std::memory_order_seq_cst);
   stats.publishMs=milliseconds(publishing);stats.totalMs=milliseconds(started);
   {std::lock_guard<std::mutex> lock(statsMutex_);stats_=stats;}
   if(request_.load(std::memory_order_acquire)==token)loading_.store(false,std::memory_order_release);
   std::fprintf(stderr,"[Nord] piano pronto: %s (%zu samples, cache=%s, %.2f ms)\n",paths_[size_t(index)].c_str(),stats.samples,stats.cacheHit?"hit":"miss",stats.totalMs);
  }catch(const std::exception& e){
   if(request_.load(std::memory_order_acquire)==token&&!stop_.load(std::memory_order_acquire)){
    loading_.store(false,std::memory_order_release);
    std::fprintf(stderr,"[Nord] falha ao carregar %s: %s\n",paths_[size_t(index)].c_str(),e.what());
   }
  }
 }
}
}
