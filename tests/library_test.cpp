#include <algorithm>
#include <cmath>
#include <set>
#include "InstrumentLibrary.hpp"
#include "PianoEngine.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#include <stdexcept>
#include <unistd.h>
namespace fs=std::filesystem;
static void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
#include "npno_fixture.hpp"
static bool wait(nord::PianoEngine& engine,bool success){
 for(int i=0;i<500;++i){auto bank=engine.beginBlock();bool ready=bank&&bank->sampleFor(60,1)&&bank->sampleFor(60,1)->l.size()==16;engine.endBlock();if(success&&ready)return true;if(!success&&!engine.loading()&&!bank&&i>5)return true;std::this_thread::sleep_for(std::chrono::milliseconds(2));}return false;
}
int main(){
 fs::path root;
 try{
 std::string temp=(fs::temp_directory_path()/"nord-library-test-XXXXXX").string();
 check(mkdtemp(temp.data())!=nullptr,"temporary directory");root=temp;
 nord::InstrumentLibrary library({root});library.refresh();check(library.instruments().size()==1&&library.resolve("builtin:pure"),"empty");
 nord::InstrumentLibrary missing({root/"missing"});missing.refresh();check(!missing.diagnostics().empty(),"missing root");
 fixture(root/"Piano/Grand/A piano.npno");fixture(root/"EP/Árvore 日本.npno",65);
 fixture(root/"Piano/A piano.NPNO",66);std::ofstream(root/"ignored.nsmp")<<"unsupported";std::ofstream(root/"broken.npno")<<"bad";
 library.refresh();check(library.instruments().size()==4,"recursive and supported formats");check(!library.diagnostics().empty(),"invalid header");
 auto first=library.instruments();check(first[0].category=="EP","ordering unicode");
 const auto& grand=first[2];check(grand.category=="Piano"&&grand.subcategory=="Grand","nested category");
 check(first[1].id!=first[2].id,"similar names distinct contents");
 library.refresh();check(library.instruments()[2].id==grand.id,"deterministic refresh");
 fs::rename(root/"Piano/Grand/A piano.npno",root/"Piano/Grand/Renamed.npno");library.refresh();check(library.resolve(grand.id),"rename stable");
 fs::path moved=root.string()+"-moved";fs::rename(root,moved);root=moved;nord::InstrumentLibrary relocated({root});relocated.refresh();check(relocated.resolve(grand.id),"move stable");
 fs::remove(root/"Piano/Grand/Renamed.npno");relocated.refresh();check(!relocated.resolve(grand.id),"removed");
 fixture(root/"Sample/New.npno",67);relocated.refresh();check(relocated.instruments().size()==4,"added refresh");
 // Identical content is deduplicated; path changes do not create identities.
 fixture(root/"Copy.npno",67);relocated.refresh();check(relocated.instruments().size()==4,"duplicates");
 {
 nord::PianoEngine engine({(root/"Piano/A piano.NPNO").string(),(root/"broken.npno").string(),(root/"gone.npno").string()});
 engine.select(0);check(wait(engine,true),"real NPNO preload");
 for(int n=0;n<10;++n){engine.select(1);check(wait(engine,false),"invalid load safe");engine.select(0);check(wait(engine,true),"switch back");}
 engine.select(2);check(wait(engine,false),"missing load safe");engine.select(-1);check(wait(engine,false),"unresolved state silence");
 }
 // Exercise bank reclamation with a continuously active, single audio reader.
 {
 nord::PianoEngine engine({(root/"Piano/A piano.NPNO").string(),(root/"Sample/New.npno").string()});
 std::atomic<bool> stop{false},bad{false};std::atomic<int> observed{-1};
 std::thread audio([&]{while(!stop.load()){
  const auto* bank=engine.beginBlock();
  if(bank){const auto* sample=bank->sampleFor(60,1);if(!sample||sample->l.size()!=16||!std::isfinite(sample->l[0]))bad.store(true);observed.store(bank->instrument);}
  engine.endBlock();std::this_thread::sleep_for(std::chrono::microseconds(100));
 }});
 bool switched=true;
 for(int n=0;n<20;++n){int wanted=n%2;observed.store(-1);engine.select(wanted);
  int attempt=0;while(observed.load()!=wanted&&attempt++<500)std::this_thread::sleep_for(std::chrono::milliseconds(2));
  if(observed.load()!=wanted){switched=false;break;}
 }
 stop.store(true);audio.join();check(switched&&!bad.load(),"concurrent publication/reclamation");
 }
 // Static build catalog: exact categories, normalization, relative paths, IDs.
 const auto source=root/"static";
 int value=70;
 for(const auto& category:fixtureCategories())fixture(source/category/"Plain.npno",value++);
 fixture(source/"Grand Piano/Astoria Grand L 6.1.npno",100);
 fixture(source/"Grand Piano/Astoria Grand XL 6.1.npno",101);
 fixture(source/"Grand Piano/Royal Grand 3D L 5.3.npno",102);
 fixture(source/"Grand Piano/White Grand XL 6.0.npno",103);
 fixture(source/"Grand Piano/Fallback XL 1.0.npno",106);
 fixture(source/"Grand Piano/Fallback XL 1.0.NPNO",107);
 nord::InstrumentLibrary build({source});build.refresh(true);
 check(build.diagnostics().empty()&&build.instruments().size()==21,"all 14 physical categories");
 nord::InstrumentLibrary legacy({source});legacy.refresh();
 for(const auto& i:build.instruments()){
  if(i.format==nord::InstrumentFormat::BuiltinPure)continue;
  check(legacy.resolve(i.id),"v0.5 content ID compatibility");
  check(!i.relativePath.is_absolute()&&i.relativePath==fs::path("instruments")/i.category/i.originalName,"relative bundle path");
  check(i.subcategory.empty(),"no subcategory");
  check(i.displayCategory.find('_')==std::string::npos,"category presentation");
  if(i.originalName=="Astoria Grand L 6.1.npno")check(i.name=="Astoria Grand (L 6.1)","collision L");
  if(i.originalName=="Astoria Grand XL 6.1.npno")check(i.name=="Astoria Grand (XL 6.1)","collision XL");
  if(i.originalName=="Royal Grand 3D L 5.3.npno")check(i.name=="Royal Grand 3D","preserve 3D");
  if(i.originalName=="White Grand XL 6.0.npno")check(i.name=="White Grand","size/version omitted");
 }
 std::set<std::string> uniqueLabels;
 for(const auto& i:build.instruments()){
  if(i.format==nord::InstrumentFormat::BuiltinPure)continue;
  check(uniqueLabels.insert(i.label()).second,"unique collision labels");
  if(i.originalName.find("Fallback")==0)check(i.name.find(i.id.substr(13))!=std::string::npos,"stable fallback for equal suffixes");
 }
 check(nord::InstrumentLibrary::normalizeCategory("Voices_Choir")=="Voices/Choir","underscore category");
 check(nord::InstrumentLibrary::normalizeName("Model 42.npno")=="Model 42","real integer remains");
 check(nord::InstrumentLibrary::normalizeName("Plain.npno")=="Plain","no suffix");
 check(nord::InstrumentLibrary::normalizeName("Piano M.npno")=="Piano","size only");
 check(nord::InstrumentLibrary::normalizeName("Piano 2.1.npno")=="Piano","version only");
 const auto bundle=root/"Nord.lv2";fs::create_directories(bundle);build.writeCatalog(bundle/"catalog.nord");
 std::ifstream before(bundle/"catalog.nord",std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(before)),{});
 build.refresh(true);build.writeCatalog(bundle/"catalog.nord");
 std::ifstream after(bundle/"catalog.nord",std::ios::binary);check(std::string((std::istreambuf_iterator<char>(after)),{})==bytes,"deterministic catalog bytes");
 nord::InstrumentLibrary runtime({});runtime.loadCatalog(bundle);
 check(runtime.diagnostics().empty()&&runtime.instruments().size()==21,"load catalog without instrument probing");
 fixture(bundle/"instruments/Strings/Unregistered.npno",104);
 runtime.loadCatalog(bundle);check(runtime.instruments().size()==21,"unregistered files ignored");
 const auto movedBundle=root/"Moved Nord.lv2";fs::rename(bundle,movedBundle);runtime.loadCatalog(movedBundle);
 check(runtime.instruments()[0].path==movedBundle/runtime.instruments()[0].relativePath,"relocatable bundle");
 fs::create_directories(source/"Grand Piano/Subcategory");build.refresh(true);check(!build.diagnostics().empty(),"reject subcategories");fs::remove_all(source/"Grand Piano/Subcategory");
 fs::create_directories(source/"Unknown");build.refresh(true);check(build.diagnostics().empty(),"accept new category without rebuild");fs::remove_all(source/"Unknown");
 fixture(source/"Outside.NpNo",105);build.refresh(true);check(!build.diagnostics().empty(),"reject root instrument");
 // External catalog path: persistent IDs survive rename/category changes; missing is explicit.
 const auto externalCatalog=root/"external catalog";fs::create_directories(externalCatalog);
 nord::InstrumentLibrary external({source});external.refresh(false,true,externalCatalog/"index");
 external.writeCatalog(externalCatalog/"catalog.nord",true);
 nord::InstrumentLibrary loaded({});loaded.loadCatalog(externalCatalog);
 check(!loaded.instruments().empty()&&loaded.diagnostics().empty(),"external catalog loads");
 for(const auto& i:loaded.instruments())check(i.available&&(i.format==nord::InstrumentFormat::BuiltinPure||i.path.is_absolute()),"availability and absolute external paths");
 const auto removedPath=loaded.instruments()[0].path;const auto removedID=loaded.instruments()[0].id;fs::remove(removedPath);
 loaded.loadCatalog(externalCatalog);check(loaded.resolve(removedID)&&!loaded.resolve(removedID)->available,"registered missing keeps identity/status");
 fs::remove_all(root);std::cout<<"Discovery, static catalog, normalization and asynchronous loading passed\n";
 }catch(const std::exception& e){fs::remove_all(root);std::cerr<<e.what()<<'\n';return 1;}
}
