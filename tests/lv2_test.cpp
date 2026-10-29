#include "NordControls.hpp"
#include "InstrumentLibrary.hpp"
#include "SynthParameters.hpp"
#include "lv2/lv2.h"
#include "lv2/urid.h"
#include "lv2/options.h"
#include "lv2/worker.h"
#include "lv2/state.h"
#include "lv2/atom.h"
#include "lv2/midi.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <thread>
#include <vector>
#include <unistd.h>
namespace fs=std::filesystem;
#include "npno_fixture.hpp"
static void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
struct Host {
 std::map<std::string,uint32_t> urids;
 static uint32_t map(void* h,const char* uri){auto& m=static_cast<Host*>(h)->urids;auto it=m.find(uri);if(it!=m.end())return it->second;uint32_t id=m.size()+1;m[uri]=id;return id;}
 std::map<uint32_t,std::string> state;
 static LV2_State_Status store(void* h,uint32_t k,const void* v,size_t n,uint32_t,uint32_t){static_cast<Host*>(h)->state[k]=std::string(static_cast<const char*>(v),n);return LV2_STATE_SUCCESS;}
 static const void* retrieve(void* h,uint32_t k,size_t* n,uint32_t* type,uint32_t* flags){auto& host=*static_cast<Host*>(h);auto it=host.state.find(k);if(it==host.state.end())return nullptr;*n=it->second.size();*type=map(h,LV2_ATOM__String);*flags=LV2_STATE_IS_POD|LV2_STATE_IS_PORTABLE;return it->second.data();}
 static LV2_Worker_Status schedule(void*,uint32_t,const void*){return LV2_WORKER_ERR_NO_SPACE;}
};
int main(int argc,char** argv){
 fs::path root;
 try{
 check(argc>=2,"plugin path required");
 const bool real=argc==3;
 std::string temp=(fs::temp_directory_path()/"nord-lv2-test-XXXXXX").string();
 check(mkdtemp(temp.data())!=nullptr,"temporary directory");root=temp;
 fs::path testBundle;
 std::string identity;
 if(real){
  testBundle=fs::absolute(argv[1]).parent_path();
  nord::InstrumentLibrary library({});library.loadCatalog(testBundle);
  const auto requested=fs::canonical(argv[2]);
  for(const auto& i:library.instruments())if(i.path==requested)identity=i.id;
  check(!identity.empty(),"real instrument must be registered in bundle");
 }else{
  const auto source=root/"source";fixture(source/"Upright Piano/Test.npno");
  nord::InstrumentLibrary build({source});build.refresh(true);check(build.diagnostics().empty(),"static source");
  for(const auto&i:build.instruments())if(i.format==nord::InstrumentFormat::Npno)identity=i.id;
  testBundle=root/"Moved bundle/Nord.lv2";fs::create_directories(testBundle/"instruments/Upright Piano");
  build.writeCatalog(testBundle/"catalog.nord");
  fs::copy_file(source/"Upright Piano/Test.npno",testBundle/"instruments/Upright Piano/Test.npno");
  // Runtime must ignore both external configuration and unregistered bundle files.
  setenv("NORD_INSTRUMENT_PATH",(root/"external-ignored").c_str(),1);
  fixture(testBundle/"instruments/Strings/Unregistered.npno",100);
  fs::remove_all(source);
 }
 void* module=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);check(module,"dlopen");
 auto entry=reinterpret_cast<const LV2_Descriptor*(*)(uint32_t)>(dlsym(module,"lv2_descriptor"));check(entry,"descriptor");const auto* d=entry(0);
 Host host;LV2_URID_Map map{&host,Host::map};LV2_Worker_Schedule worker{&host,Host::schedule};
 int block=256;LV2_Options_Option options[]={{LV2_OPTIONS_INSTANCE,0,Host::map(&host,"http://lv2plug.in/ns/ext/buf-size#maxBlockLength"),sizeof(block),Host::map(&host,LV2_ATOM__Int),&block},{}};
 LV2_Feature f1{LV2_URID__map,&map},f2{LV2_OPTIONS__options,options},f3{LV2_WORKER__schedule,&worker};const LV2_Feature* features[]={&f1,&f2,&f3,nullptr};
 std::string bundle=testBundle.string()+"/";
 auto instance=d->instantiate(d,44100,bundle.c_str(),features);check(instance,"instantiate");
 const auto* state=static_cast<const LV2_State_Interface*>(d->extension_data(LV2_STATE__interface));check(state,"state extension");
 std::array<float,256> left{},right{};std::array<float,nord::NordControls::count> controls{};
 for(size_t i=0;i<controls.size();++i)controls[i]=nord::NordControls::definitions()[i].parameter.initial;
 alignas(8) std::array<unsigned char,4096> input{},output{};
 auto* seq=reinterpret_cast<LV2_Atom_Sequence*>(input.data());seq->atom.type=Host::map(&host,LV2_ATOM__Sequence);
 d->connect_port(instance,0,left.data());d->connect_port(instance,1,right.data());d->connect_port(instance,2,input.data());d->connect_port(instance,3,output.data());
 for(size_t i=0;i<controls.size();++i)d->connect_port(instance,uint32_t(4+i),&controls[i]);
 auto run=[&](bool note,uint8_t status=0x90,uint8_t midiNote=60,uint8_t velocity=100){seq->atom.size=sizeof(LV2_Atom_Sequence_Body);if(note){auto* e=reinterpret_cast<LV2_Atom_Event*>(input.data()+sizeof(LV2_Atom_Sequence));e->time.frames=0;e->body.type=Host::map(&host,LV2_MIDI__MidiEvent);e->body.size=3;auto* data=reinterpret_cast<unsigned char*>(e+1);data[0]=status;data[1]=midiNote;data[2]=velocity;seq->atom.size+=24;}reinterpret_cast<LV2_Atom*>(output.data())->size=output.size();d->run(instance,256);float peak=0;for(float v:left){check(std::isfinite(v),"finite output");peak=std::max(peak,std::abs(v));}return peak;};
 auto audible=[&](){for(int i=0;i<(real?12000:500);++i){if(run(true)>0)return true;std::this_thread::sleep_for(std::chrono::milliseconds(5));}return false;};
 check(state->save(instance,Host::store,&host,0,features)==LV2_STATE_SUCCESS,"save initial metadata-only state");
 check(host.state.at(Host::map(&host,"urn:nord:instrument#instrument_id"))==std::string(1,'\0'),"constructor must not select or preload an instrument");
 d->activate(instance);
 check(run(false)==0,"unselected instance is silent");
 d->deactivate(instance);
 uint32_t selectedKey=Host::map(&host,"urn:nord:instrument#instrument_id");
 host.state[selectedKey]=identity+std::string(1,'\0');
 check(state->restore(instance,Host::retrieve,&host,0,features)==LV2_STATE_SUCCESS,"initial state selection");
 d->activate(instance);check(audible(),"MIDI rendered nonzero PCM");
 if(!real){const float baseline=run(true);controls[nord::NordControls::Touch]=3;controls[nord::NordControls::Timbre]=2;const float automated=run(true);check(automated>0&&std::abs(automated-baseline)>1e-8,"new control ports change actual plugin audio");}
 controls[nord::NordControls::Transpose]=7;
 run(true,0xb0,120,0);
 const auto savedControls=controls; // Control-port values are saved/restored by the LV2 host.

 check(state->save(instance,Host::store,&host,0,features)==LV2_STATE_SUCCESS,"save");
 uint32_t key=Host::map(&host,"urn:nord:instrument#instrument_id");check(host.state.at(key)==identity+std::string(1,'\0'),"stable ID saved");
 host.state[key]=std::string("npno-missing\0",13);check(state->restore(instance,Host::retrieve,&host,0,features)==LV2_STATE_SUCCESS,"restore missing");check(run(true)==0,"missing silence");
 check(state->save(instance,Host::store,&host,0,features)==LV2_STATE_SUCCESS,"save missing");
 check(host.state.at(key)==std::string("npno-missing\0",13),"missing ID preserved");
 // Recreate the plugin: exercise actual session reopening and file relocation.
 d->deactivate(instance);d->cleanup(instance);

 controls.fill(0);
 instance=d->instantiate(d,44100,bundle.c_str(),features);check(instance,"reinstantiate");
 d->connect_port(instance,0,left.data());d->connect_port(instance,1,right.data());d->connect_port(instance,2,input.data());d->connect_port(instance,3,output.data());for(size_t i=0;i<controls.size();++i)d->connect_port(instance,uint32_t(4+i),&controls[i]);
 d->activate(instance);
 host.state[key]=identity+std::string(1,'\0');check(state->restore(instance,Host::retrieve,&host,0,features)==LV2_STATE_SUCCESS,"restore ID");controls=savedControls;check(audible(),"restored PCM with restored host control ports");
 // A registered but missing file must preserve State and stay silent.
 if(!real){
  d->deactivate(instance);d->cleanup(instance);
  fs::remove(testBundle/"instruments/Upright Piano/Test.npno");
  instance=d->instantiate(d,44100,bundle.c_str(),features);check(instance,"instantiate missing registered file");
  d->connect_port(instance,0,left.data());d->connect_port(instance,1,right.data());d->connect_port(instance,2,input.data());d->connect_port(instance,3,output.data());for(size_t i=0;i<controls.size();++i)d->connect_port(instance,uint32_t(4+i),&controls[i]);
  check(state->restore(instance,Host::retrieve,&host,0,features)==LV2_STATE_SUCCESS,"restore missing registered ID");
  d->activate(instance);
  for(int i=0;i<20;++i){check(run(true)==0,"registered missing and unregistered ignored");std::this_thread::sleep_for(std::chrono::milliseconds(2));}
  check(state->save(instance,Host::store,&host,0,features)==LV2_STATE_SUCCESS&&host.state.at(key)==identity+std::string(1,'\0'),"missing registered state remains");
 }
 d->deactivate(instance);d->cleanup(instance);dlclose(module);
 fs::remove_all(root);
 std::cout<<"LV2 instantiate, MIDI PCM, State save/restore and missing selection passed"<<(real?" (real instrument)":"")<<'\n';
 }catch(const std::exception& e){fs::remove_all(root);std::cerr<<e.what()<<'\n';return 1;}
}
