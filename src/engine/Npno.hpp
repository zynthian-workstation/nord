#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
namespace nord {
inline constexpr double kNpnoRate=35002.; // Decoded frames per second; independent of host.
enum class SampleBank : uint8_t { Attack=0, Resonance=1, Release=2 };
struct Stroke {
 uint64_t offset=0;
 uint32_t frames=0,loopStart=0,loopLength=0,decay=0;
 uint16_t blocks=0,trim=0;
 uint8_t flags=0,floor=0;
 int16_t history[2][4]{};
 std::array<uint32_t,14> decayLadder{};
 std::array<uint8_t,118> record{}; // Preserve unknown/vendor fields for investigation.
 int zone=-1;
};
struct Zone { int root=0,low=128,high=-1; };
struct PCM { std::vector<int32_t> left,right; };
struct NpnoTimings {double readMs=0,parseMs=0;};
class Npno {
 public:
  Npno(){noteZones_.fill(-1);}
  void open(const std::string& path);
  const std::vector<Zone>& zones() const {return zones_;}
  const std::vector<Stroke>& strokes() const {return strokes_;}
  int channels() const {return channels_;}
  int version() const {return version_;}
  int damperTop() const {return damperTop_;}
  int libraryGain() const {return libraryGain_;}
  NpnoTimings timings() const {return timings_;}
  int zoneFor(int midiNote) const noexcept;
  bool hasBank(int note,SampleBank bank) const noexcept;
  const Stroke& select(int midiNote,int velocity,SampleBank bank=SampleBank::Attack) const;
  PCM decode(const Stroke& s,const std::function<bool()>& cancelled={}) const;
 private:
  std::vector<uint8_t> bytes_;
  std::vector<Stroke> strokes_;
  std::vector<Zone> zones_;
  std::array<int,128> noteZones_{};
  std::vector<std::array<std::vector<size_t>,3>> zoneStrokes_;
  int channels_=0,version_=0,damperTop_=127,libraryGain_=0;
  size_t blockWords_=0;
  NpnoTimings timings_;
};
}
