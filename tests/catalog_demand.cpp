#include "InstrumentLibrary.hpp"
#include "PianoRenderer.hpp"
#include "npno_fixture.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
namespace fs=std::filesystem;
static void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
static const nord::Bank* ready(nord::PianoEngine& engine,int index){
 for(int i=0;i<2000;++i){const auto* b=engine.beginBlock();if(b&&b->instrument==index&&engine.stats().token==b->requestToken)return b;engine.endBlock();std::this_thread::sleep_for(std::chrono::milliseconds(1));}throw std::runtime_error("load timeout");
}
int main(){fs::path root;try{
 std::string pattern=(fs::current_path()/"catalog-demand-XXXXXX").string();check(mkdtemp(pattern.data()),"temporary directory");root=pattern;
 const auto source=root/"source",bundle=root/"Nord.lv2";int value=70;
 for(const auto& category:fixtureCategories())fixture(source/category/"Tone.npno",value++);
 nord::InstrumentLibrary build({source});build.refresh(true);check(build.diagnostics().empty()&&build.instruments().size()==15,"14 categories registered");
 fs::create_directories(bundle);build.writeCatalog(bundle/"catalog.nord");
 for(const auto& i:build.instruments()){if(i.format==nord::InstrumentFormat::BuiltinPure)continue;fs::create_directories((bundle/i.relativePath).parent_path());fs::copy_file(i.path,bundle/i.relativePath);}
 nord::InstrumentLibrary catalog({});catalog.loadCatalog(bundle);check(catalog.instruments().size()==15,"static catalog roundtrip");
 std::vector<std::string> paths;for(const auto& i:catalog.instruments())if(i.format!=nord::InstrumentFormat::BuiltinPure)paths.push_back(i.path.string());
 const auto last=catalog.instruments().back().path,hidden=fs::path(last.string()+".hidden");fs::rename(last,hidden);
 nord::PianoEngine engine(paths,600000);
 std::this_thread::sleep_for(std::chrono::milliseconds(10));check(engine.selected()==-1&&!engine.loading()&&engine.stats().samples==0,"metadata startup performs no instrument loading");
 check(!engine.beginBlock(),"no default bank");engine.endBlock();
 engine.select(0);ready(engine,0);engine.endBlock(); // Unselected missing file must not matter.
 fs::rename(hidden,last);
 for(size_t index=0;index<paths.size();++index){
  engine.select(int(index));const auto* bank=ready(engine,int(index));
  const auto* sample=bank->sampleFor(60,127);check(sample&&bank->owned.size()==2,"only selected fixture decoded");
  nord::PianoRenderer renderer;renderer.setBank(bank);renderer.midi(0x90,60,127);float l,r;renderer.render(l,r);check(l>0&&l==r,"non-Grand category produces PCM");
  engine.endBlock();check(engine.stats().cacheBytes<=engine.cacheBudget(),"LRU remains bounded");
  if(index>0)check(!engine.stats().cacheHit,"instrument was not preloaded before selection");
 }
 engine.select(12);ready(engine,12);engine.endBlock();check(engine.stats().cacheHit,"recent instrument reuses cache");
 fs::remove_all(root);std::cout<<"14-category static catalog and sequential on-demand PCM passed\n";
 }catch(const std::exception& e){if(!root.empty())fs::remove_all(root);std::cerr<<e.what()<<'\n';return 1;}}
