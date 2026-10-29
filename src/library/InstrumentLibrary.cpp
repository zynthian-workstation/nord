#include "InstrumentLibrary.hpp"
#include "EngineResolver.hpp"
#include "Nsmp4Reader.hpp"
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <regex>
#include <map>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
namespace nord {
namespace fs=std::filesystem;
namespace {
// Content ID survives relocation and renaming. Stream rather than preload samples.
std::string identify(const fs::path& path){
 std::ifstream f(path,std::ios::binary);
 if(!f)throw std::runtime_error("arquivo ilegivel");
 std::array<char,65536> bytes{};
 f.read(bytes.data(),bytes.size());
 auto n=f.gcount();
 const bool nsmp4=resolveEngine(path)==EngineKind::SampleSynth;
 if(nsmp4)synth::Nsmp4Reader::inspect(path.string());
 if(!nsmp4&&(n<4096||std::memcmp(bytes.data(),"CBIN",4)||std::memcmp(bytes.data()+8,"npno",4)))throw std::runtime_error("cabecalho NPNO invalido");
 // Cheap structural screen; full stroke/PCM validation belongs to PianoEngine.
 if(!nsmp4){
 size_t base=0;
 for(;base<0x80;++base)if(!std::memcmp(bytes.data()+base,"CNSP",4))break;
 if(base==0x80)throw std::runtime_error("CNSP ausente");
 auto word=[&](size_t p){return (unsigned(static_cast<unsigned char>(bytes[p]))<<8)|static_cast<unsigned char>(bytes[p+1]);};
 const unsigned channels=word(base+0x61e),count=word(base+0x620);
 unsigned roots=0;for(int i=0;i<128;++i)roots+=word(base+0x622+2*i);
 if((channels!=1&&channels!=2)||count<2||roots!=count||base+0x732+size_t(count)*0x76>fs::file_size(path))throw std::runtime_error("Diretorio NPNO invalido");
 }
 uint64_t hash=14695981039346656037ULL;
 do {for(std::streamsize i=0;i<n;++i){hash^=static_cast<unsigned char>(bytes[i]);hash*=1099511628211ULL;}
 f.read(bytes.data(),bytes.size());n=f.gcount();}while(n);
 if(!f.eof())throw std::runtime_error("falha de leitura");
 std::ostringstream out;out<<(nsmp4?"nsmp4-fnv1a64-":"npno-fnv1a64-")<<std::hex<<std::setw(16)<<std::setfill('0')<<hash;return out.str();
}
}
void InstrumentLibrary::ensurePure(){
 if(resolve("builtin:pure"))return;
 Instrument i;i.id="builtin:pure";i.name="Pure";i.category=i.displayCategory="Synth";i.format=InstrumentFormat::BuiltinPure;i.engine=EngineKind::SampleSynth;i.path="builtin:pure";instruments_.push_back(std::move(i));
}
std::string Instrument::label() const {
 std::string prefix=displayCategory.empty()?category:displayCategory;
 if(!subcategory.empty())prefix+=(prefix.empty()?"":" / ")+subcategory;
 return prefix.empty()?name:prefix+" / "+name;
}
std::string InstrumentLibrary::normalizeCategory(std::string name){
 std::replace(name.begin(),name.end(),'_','/');return name;
}
std::string InstrumentLibrary::normalizeName(const std::string& filename){
 fs::path path(filename);std::string name=filename;
 auto ext=path.extension().string();
 std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
 if(ext==".npno"||ext==".nsmp4")name=path.stem().string();
 // Only trailing dotted versions and standalone size tokens are metadata.
 static const std::regex version(R"( +[0-9]+(?:\.[0-9]+)+$)");
 static const std::regex size(R"( +(XL|S|M|L)$)");
 name=std::regex_replace(name,version,"");
 name=std::regex_replace(name,size,"");
 return name.empty()?path.stem().string():name;
}
void InstrumentLibrary::refresh(bool staticBuild,bool external,const fs::path& cache){
 std::map<std::string,std::pair<std::string,std::string>> previous,updated;
 {std::ifstream in(cache,std::ios::binary);std::string path,fingerprint,id;
  while(std::getline(in,path,'\0')&&std::getline(in,fingerprint,'\0')&&std::getline(in,id,'\0'))previous[path]={fingerprint,id};}
 auto identity=[&](const fs::path& path){
  struct stat st{};if(::stat(path.c_str(),&st)!=0)throw std::runtime_error("stat falhou");
  const std::string fingerprint=std::to_string(st.st_dev)+":"+std::to_string(st.st_ino)+":"+std::to_string(st.st_size)+":"+std::to_string(st.st_mtim.tv_sec)+":"+std::to_string(st.st_mtim.tv_nsec)+":"+std::to_string(st.st_ctim.tv_sec)+":"+std::to_string(st.st_ctim.tv_nsec);
  const auto old=previous.find(path.string());
  auto id=old!=previous.end()&&old->second.first==fingerprint?old->second.second:identify(path);
  updated[path.string()]={fingerprint,id};return id;
 };
 instruments_.clear();diagnostics_.clear();std::set<fs::path> seen;
 for(const auto& root:roots_){
  std::error_code ec;
  if(!root.is_absolute()||!fs::is_directory(root,ec)){diagnostics_.push_back("Biblioteca indisponivel: "+root.string());continue;}
  if(staticBuild){
   // Validate the entire physical layout before hashing any instruments.
   for(const auto& item:fs::directory_iterator(root)){
    if(item.is_directory()){
     const auto category=item.path().filename().string();
     if(item.is_symlink())diagnostics_.push_back("Categoria nao permitida: "+item.path().string());
     else for(const auto& child:fs::directory_iterator(item.path()))if(child.is_directory()||child.is_symlink())diagnostics_.push_back("Subdiretorio/link nao permitido: "+child.path().string());
    }else {
     auto extension=item.path().extension().string();
     std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});
     if(extension==".npno"||extension==".nsmp4")diagnostics_.push_back("Instrumento fora de categoria: "+item.path().string());
    }
   }
   if(!diagnostics_.empty())continue;
  }
  fs::recursive_directory_iterator it(root,fs::directory_options::skip_permission_denied,ec),end;
  while(it!=end){
   auto entry=*it;
   if(entry.is_directory(ec)&&::access(entry.path().c_str(),R_OK|X_OK)!=0){diagnostics_.push_back("Diretorio sem permissao: "+entry.path().string());it.disable_recursion_pending();}
   if(entry.is_regular_file(ec)){
    auto ext=entry.path().extension().string();std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
    if(resolveEngine(entry.path())!=EngineKind::Unsupported)try{
     auto path=fs::weakly_canonical(entry.path());
     if(seen.insert(path).second){
      Instrument instrument;instrument.path=path;instrument.engine=resolveEngine(path);instrument.format=instrument.engine==EngineKind::SampleSynth?InstrumentFormat::Nsmp4:InstrumentFormat::Npno;instrument.originalName=entry.path().filename().string();instrument.id=identity(path);instrument.name=entry.path().stem().string();
      const auto relative=entry.path().lexically_relative(root).parent_path();
      auto part=relative.begin();
      if(part!=relative.end()){instrument.category=part->string();for(++part;part!=relative.end();++part){if(!instrument.subcategory.empty())instrument.subcategory+=" / ";instrument.subcategory+=part->string();}}
      if(staticBuild||external){
       instrument.name=normalizeName(instrument.originalName);
       instrument.displayCategory=normalizeCategory(instrument.category);
       instrument.relativePath=fs::path("instruments")/entry.path().lexically_relative(root);
      }
      instruments_.push_back(std::move(instrument));
     }
    }catch(const std::exception& e){diagnostics_.push_back(entry.path().string()+": "+e.what());}
   }
   it.increment(ec);if(ec){diagnostics_.push_back(root.string()+": "+ec.message());ec.clear();}
  }
 }
 ensurePure();
 std::sort(instruments_.begin(),instruments_.end(),[](const Instrument& a,const Instrument& b){return std::make_pair(a.label(),a.path)<std::make_pair(b.label(),b.path);});
 if(!cache.empty()){
  std::ofstream out(cache.string()+".tmp",std::ios::binary);
  for(const auto& p:updated)out<<p.first<<'\0'<<p.second.first<<'\0'<<p.second.second<<'\0';
  out.close();if(!out)throw std::runtime_error("Falha ao escrever index");fs::rename(cache.string()+".tmp",cache);
 }
 if(external){std::set<std::string> ids;instruments_.erase(std::remove_if(instruments_.begin(),instruments_.end(),[&](const Instrument& i){return !ids.insert(i.id).second;}),instruments_.end());}
 if(staticBuild||external){
  std::map<std::pair<std::string,std::string>,std::vector<Instrument*>> names;
  for(auto& i:instruments_)names[{i.displayCategory,i.name}].push_back(&i);
  for(auto& group:names)if(group.second.size()>1){
   std::map<std::string,int> counts;
   for(auto* i:group.second){
    const auto stem=fs::path(i->originalName).stem().string();
    auto suffix=stem.substr(normalizeName(i->originalName).size());
    if(!suffix.empty()&&suffix.front()==' ')suffix.erase(0,1);
    i->name+=" ("+(suffix.empty()?i->id.substr(i->id.size()-8):suffix)+")";
    ++counts[i->name];
   }
   for(auto* i:group.second)if(counts[i->name]>1)i->name+=" ["+i->id.substr(13)+"]";
  }
  // Also handle collisions with names that naturally contain our suffix syntax.
  for(;;){
   std::map<std::string,std::vector<Instrument*>> labels;
   for(auto& i:instruments_)labels[i.label()].push_back(&i);
   bool changed=false;
   for(auto& group:labels)if(group.second.size()>1){
    std::set<std::string> distinct;
    for(auto* i:group.second)distinct.insert(i->id);
    if(distinct.size()!=group.second.size())continue; // duplicate IDs are rejected below
    for(auto* i:group.second)i->name+=" ["+i->id.substr(13)+"]";
    changed=true;
   }
   if(!changed)break;
  }
  std::set<std::string> ids;
  for(const auto& i:instruments_)if(!ids.insert(i.id).second)diagnostics_.push_back("Conteudo duplicado (mesmo ID): "+i.path.string());
  std::sort(instruments_.begin(),instruments_.end(),[](const Instrument& a,const Instrument& b){return std::make_pair(a.label(),a.relativePath)<std::make_pair(b.label(),b.relativePath);});
  return;
 }
 // Identical copies are one instrument; deterministic path/label wins.
 std::set<std::string> ids;
 instruments_.erase(std::remove_if(instruments_.begin(),instruments_.end(),[&](const Instrument& i){return !ids.insert(i.id).second;}),instruments_.end());
}
void InstrumentLibrary::writeCatalog(const fs::path& file,bool external) const {
 std::ofstream out(file,std::ios::binary);
 if(!out)throw std::runtime_error("Nao criou catalogo: "+file.string());
 out<<(external?"NORD-LIBRARY-2":"NORD-CATALOG-1")<<'\0';
 for(const auto& i:instruments_)for(const auto& field:{i.id,i.originalName,i.name,i.category,i.displayCategory,std::string(i.format==InstrumentFormat::BuiltinPure?"pure":i.format==InstrumentFormat::Nsmp4?"nsmp4":"npno"),(external?i.path:i.relativePath).generic_string()})out<<field<<'\0';
 if(!out)throw std::runtime_error("Falha ao escrever catalogo");
}
void InstrumentLibrary::loadCatalog(const fs::path& bundle){
 instruments_.clear();diagnostics_.clear();
 try{
  if(bundle.empty()||!bundle.is_absolute())throw std::runtime_error("Caminho do bundle LV2 indisponivel");
  fs::path catalog=bundle/"catalog.nord";
  if(const char* configured=std::getenv("NORD_LIBRARY_CATALOG")){if(*configured)catalog=configured;}
  else {
   fs::path data;
   if(const char* z=std::getenv("ZYNTHIAN_MY_DATA_DIR"))data=fs::path(z)/"nord-lv2";
   else if(const char* x=std::getenv("XDG_DATA_HOME"))data=fs::path(x)/"nord-lv2";
   else if(const char* h=std::getenv("HOME"))data=fs::path(h)/".local/share/nord-lv2";
   if(!data.empty()&&fs::is_regular_file(data/"catalog.nord"))catalog=data/"catalog.nord";
  }
  std::ifstream input(catalog,std::ios::binary);std::string header;
  if(!std::getline(input,header,'\0')||(header!="NORD-CATALOG-1"&&header!="NORD-LIBRARY-2"))throw std::runtime_error("Catalogo ausente ou invalido");
  std::set<std::string> ids;
  while(input.peek()!=std::char_traits<char>::eof()){
   std::array<std::string,7> fields;
   for(auto& field:fields)if(!std::getline(input,field,'\0'))throw std::runtime_error("Catalogo truncado");
   Instrument i;i.id=fields[0];i.originalName=fields[1];i.name=fields[2];i.category=fields[3];i.displayCategory=fields[4];i.relativePath=fields[6];
   if(fields[5]=="pure"){
    if(i.id!="builtin:pure"||!ids.insert(i.id).second)throw std::runtime_error("Invalid builtin source");
    i.path="builtin:pure";i.format=InstrumentFormat::BuiltinPure;i.engine=EngineKind::SampleSynth;instruments_.push_back(std::move(i));continue;
   }
   const fs::path expected=fs::path("instruments")/i.category/i.originalName;
   i.format=fields[5]=="nsmp4"?InstrumentFormat::Nsmp4:InstrumentFormat::Npno;
   if((fields[5]!="npno"&&fields[5]!="nsmp4")||i.id.empty()||!ids.insert(i.id).second||resolveEngine(i.relativePath)!=(i.format==InstrumentFormat::Nsmp4?EngineKind::SampleSynth:EngineKind::Piano))throw std::runtime_error("Entrada de catalogo invalida");
   if(header=="NORD-LIBRARY-2"){
    if(!i.relativePath.is_absolute())throw std::runtime_error("Caminho externo deve ser absoluto");
    i.path=i.relativePath;
    std::error_code availabilityError;i.available=fs::is_regular_file(i.path,availabilityError);
   }else {
    if(fs::path(i.originalName).filename()!=i.originalName||i.originalName=="."||i.originalName==".."||i.category.empty()||fs::path(i.category).filename()!=i.category||i.relativePath!=expected||i.relativePath.is_absolute())throw std::runtime_error("Entrada de catalogo invalida");
    i.path=bundle/i.relativePath;
   }
   i.engine=resolveEngine(i.path);
   // Catalog metadata only: worker validates/decodes PCM on explicit selection.
   instruments_.push_back(std::move(i));
  }
 }catch(const std::exception& e){instruments_.clear();diagnostics_.push_back(e.what());}
 ensurePure();
}
const Instrument* InstrumentLibrary::resolve(const std::string& id) const {
 for(const auto& i:instruments_)if(i.id==id)return &i;
 return nullptr;
}
}
