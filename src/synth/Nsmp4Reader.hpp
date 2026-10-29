#pragma once
#include "Nsmp4Instrument.hpp"
#include <string>
#include <functional>
#include <stdexcept>
namespace nord::synth {
enum class ReadError {InvalidContainer,Checksum,UnsupportedVersion,InvalidStructure,UnsupportedCodec,Limit,Cancelled};
struct Nsmp4Error:std::runtime_error {ReadError code;Nsmp4Error(ReadError c,const std::string& s):std::runtime_error(s),code(c){}};
class Nsmp4Reader {
public:
 static std::shared_ptr<const Instrument> inspect(const std::string& path);
 static constexpr size_t maxSamples=256,maxPcmBytes=512*1024*1024;
 static std::shared_ptr<const Instrument> read(const std::string& path,const std::function<bool()>& cancelled={});
};
}
