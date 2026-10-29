#pragma once
#include "EngineResolver.hpp"
#include <filesystem>
#include <string>
#include <vector>
#include <utility>
namespace nord {
enum class InstrumentFormat { Npno, Nsmp4, BuiltinPure };
struct Instrument {
 std::string id, name, category, subcategory;
 std::string originalName, displayCategory;
 std::filesystem::path relativePath;
 EngineKind engine=EngineKind::Unsupported;
 bool available=true;
 InstrumentFormat format=InstrumentFormat::Npno;
 std::filesystem::path path;
 std::string label() const;
};
// No DPF/LV2 dependency. All operations here belong to a non-audio thread.
class InstrumentLibrary {
public:
 explicit InstrumentLibrary(std::vector<std::filesystem::path> roots):roots_(std::move(roots)){}
 static std::string normalizeCategory(std::string name);
 static std::string normalizeName(const std::string& filename);
 void refresh(bool staticBuild=false, bool external=false, const std::filesystem::path& cache={});
 void writeCatalog(const std::filesystem::path& file, bool external=false) const;
 void loadCatalog(const std::filesystem::path& bundle);
 const std::vector<Instrument>& instruments() const {return instruments_;}
 const std::vector<std::string>& diagnostics() const {return diagnostics_;}
 const Instrument* resolve(const std::string& id) const;
private:
 void ensurePure();
 std::vector<std::filesystem::path> roots_;
 std::vector<Instrument> instruments_;
 std::vector<std::string> diagnostics_;
};
}
