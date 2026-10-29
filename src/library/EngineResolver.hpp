#pragma once
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
namespace nord {
enum class EngineKind { Unsupported, Piano, SampleSynth };
inline EngineKind resolveEngine(const std::filesystem::path& path) {
 auto extension=path.extension().string();
 std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});
 return extension==".npno"?EngineKind::Piano:extension==".nsmp4"?EngineKind::SampleSynth:EngineKind::Unsupported;
}
}
