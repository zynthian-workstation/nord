#include "InstrumentLibrary.hpp"
#include "SynthParameters.hpp"
#include "NordControls.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <unistd.h>
int main(int argc,char** argv){
 try{
  if(argc==3&&(std::string(argv[1])=="--panel-controls"||std::string(argv[1])=="--synth-parameters"||std::string(argv[1])=="--control-layout")){
   std::ofstream out(argv[2]);out<<std::setprecision(9);const std::string mode=argv[1];
   for(unsigned i=0;i<nord::NordControls::count;++i){const auto&d=nord::NordControls::definitions()[i].parameter;auto group=nord::NordControls::group(d.group);
    if(mode=="--control-layout")out<<nord::NordControls::controlScope(i)<<'\t'<<d.group<<'\t'<<group.name<<'\t'<<i<<'\t'<<d.symbol<<'\t'<<d.minimum<<'\t'<<d.maximum<<'\t'<<d.initial<<'\n';
    else if(mode=="--synth-parameters")out<<d.symbol<<'\t'<<d.initial<<'\n';
    else for(auto pair:{std::pair<unsigned,const char*>{1,"piano"},{2,"sample"},{4,"pure"}})if(nord::NordControls::controlScope(i)&pair.first)out<<pair.second<<'\t'<<d.symbol<<'\t'<<i<<'\n';
   }
   if(!out)throw std::runtime_error("Cannot write control metadata");return 0;
  }
  const bool external=argc==4&&std::string(argv[3])=="--external";
  if(argc!=3&&!external){std::cerr<<"Usage: nord_catalog <source instruments> <catalog.nord>\n";return 2;}
  if(!std::filesystem::is_directory(argv[1])||::access(argv[1],R_OK|X_OK)!=0)throw std::runtime_error("Biblioteca indisponivel/sem permissao");
  nord::InstrumentLibrary library({std::filesystem::absolute(argv[1])});
  library.refresh(!external,external,external?std::filesystem::path(std::string(argv[2])+".index"):std::filesystem::path{});
  for(const auto& d:library.diagnostics())std::cerr<<"[Nord] "<<d<<'\n';
  if(!external&&!library.diagnostics().empty())return 1;
  library.writeCatalog(argv[2],external);
  // These files are visible during development, but must not become fake NPNO presets.
  std::error_code ec;
  for(const auto& entry:std::filesystem::recursive_directory_iterator(argv[1],std::filesystem::directory_options::skip_permission_denied,ec)){
   auto extension=entry.path().extension().string();
   std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});
   if(extension==".nsmp"||extension==".nsmp3")std::cerr<<"[Nord] formato sem engine, nao registrado: "<<entry.path().string()<<'\n';
  }
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
