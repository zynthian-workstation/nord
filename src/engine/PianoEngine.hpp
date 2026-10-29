#pragma once
#include "Npno.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
namespace nord::synth {struct Instrument;}
namespace nord {
struct Sample {
 std::vector<int16_t> l,r;
 std::vector<float> loopGain; // Prepared by worker, read-only on audio.
 int root=60;
 size_t sourceStroke=0;
 uint8_t bank=0,layer=0;
 uint32_t loopStart=0,loopLength=0,decay=0;
 uint16_t trim=0;
 std::array<uint32_t,14> decayLadder{};
 bool hasLoop() const noexcept {return loopLength>0&&loopStart<l.size()&&uint64_t(loopStart)+loopLength<=l.size();}
};
struct Bank {
 using VelocityMap=std::array<std::array<const Sample*,128>,128>;
 std::shared_ptr<const synth::Instrument> synth;
 mutable std::atomic<uint32_t> synthPins{0};
 bool pure=false; // Builtin source: no parser, zones or PCM.
 uint64_t serial=0,requestToken=0;
 int instrument=-1,damperTop=127;
 std::array<VelocityMap,3> maps{};
 std::vector<std::unique_ptr<Sample>> owned;
 std::vector<Stroke> metadata; // Includes unknown banks and complete vendor records.
 size_t bytes=0;
 Bank():bytes(sizeof(Bank)){}
 const Sample* sampleFor(int note,int velocity,SampleBank kind=SampleBank::Attack) const noexcept {
  const auto group=static_cast<size_t>(kind);
  return note>=0&&note<128&&velocity>0&&velocity<128&&group<3?maps[group][size_t(note)][size_t(velocity)]:nullptr;
 }
};
struct LoadStats {
 uint64_t token=0;
 int instrument=-1;
 double readMs=0,parseMs=0,decodeMs=0,prepareMs=0,publishMs=0,totalMs=0;
 size_t pcmBytes=0,cacheBytes=0,samples=0;
 bool cacheHit=false;
};
class PianoEngine {
 static_assert(std::atomic<const Bank*>::is_always_lock_free&&std::atomic<bool>::is_always_lock_free&&std::atomic<uint64_t>::is_always_lock_free,"Audio publication requires lock-free atomics");
public:
 static size_t defaultCacheBudget() noexcept;
 explicit PianoEngine(std::vector<std::string> paths,size_t cacheBudget=defaultCacheBudget());
 ~PianoEngine();
 PianoEngine(const PianoEngine&)=delete;
 PianoEngine& operator=(const PianoEngine&)=delete;
 void select(int index) noexcept;
 // One audio reader. All sample use is inside this section; never free on audio.
 const Bank* beginBlock() noexcept {
  reading_.store(true,std::memory_order_seq_cst);
  const Bank* bank=current_.load(std::memory_order_seq_cst);
  return bank&&bank->requestToken==request_.load(std::memory_order_seq_cst)?bank:nullptr;
 }
 void endBlock() noexcept {reading_.store(false,std::memory_order_seq_cst);}
 int selected() const noexcept {return requestIndex(request_.load(std::memory_order_acquire));}
 bool loading() const noexcept {return loading_.load(std::memory_order_acquire);}
 LoadStats stats() const {std::lock_guard<std::mutex> lock(statsMutex_);return stats_;}
 size_t cacheBudget() const noexcept {return cacheBudget_;}
private:
 static int requestIndex(uint64_t token) noexcept {return int(uint32_t(token))-1;}
 void work();
 std::unique_ptr<Bank> load(int index,uint64_t token,LoadStats& stats);
 void remember(std::unique_ptr<Bank> bank) noexcept;
 std::vector<std::string> paths_;
 const size_t cacheBudget_;
 size_t cacheBytes_=0;
 std::unique_ptr<Bank> published_;
 std::array<std::unique_ptr<Bank>,65> retired_{}; // Worker-owned; Synth voice pins keep old PCM alive.
 size_t retiredBytes_=0;
 std::deque<std::unique_ptr<Bank>> cache_; // MRU front, worker owns all banks.
 std::atomic<bool> reading_{false};
 std::atomic<uint64_t> request_{0};
 std::atomic<const Bank*> current_{nullptr};
 std::atomic<bool> loading_{false},stop_{false};
 mutable std::mutex statsMutex_; // Metrics API is non-RT, never taken by audio.
 LoadStats stats_;
 std::thread loader_; // Last: all state exists before the thread starts.
};
}
