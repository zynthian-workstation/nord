#include "PianoRenderer.hpp"
#include "npno_fixture.hpp"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <thread>
namespace fs=std::filesystem;
static void check(bool ok,const char* m){if(!ok)throw std::runtime_error(m);}
static const nord::Bank* ready(nord::PianoEngine& e,int index){
 for(int i=0;i<3000;++i){const auto* b=e.beginBlock();if(b&&b->instrument==index&&e.stats().token==b->requestToken)return b;e.endBlock();std::this_thread::sleep_for(std::chrono::milliseconds(1));}throw std::runtime_error("load timeout");
}
static void tick(nord::PianoRenderer& r,int frames){float l,b;for(int i=0;i<frames;++i){r.render(l,b);check(std::isfinite(l)&&std::isfinite(b),"finite PCM");}}
static std::unique_ptr<nord::Bank> renderBank(){
 auto b=std::make_unique<nord::Bank>();b->serial=1;b->damperTop=90;
 for(int group=0;group<3;++group){
  auto s=std::make_unique<nord::Sample>();s->l.assign(16,int16_t((group+1)*1000));s->root=60;s->bank=group;
  if(group<2){s->loopStart=4;s->loopLength=8;}
  for(int note=0;note<128;++note)for(int v=1;v<128;++v)b->maps[group][note][v]=s.get();
  b->owned.push_back(std::move(s));
 }
 return b;
}
int main(){fs::path temp;try{
 std::string pattern=(fs::temp_directory_path()/"piano-regression-XXXXXX").string();check(mkdtemp(pattern.data()),"temporary directory");temp=pattern;
 const auto a=temp/"a.npno",b=temp/"b.npno";richFixture(a);richFixture(b,true);
 nord::Npno npno;npno.open(a.string());check(npno.damperTop()==90,"damper metadata");
 check(npno.strokes().size()==15&&npno.strokes()[8].decay==0&&npno.strokes()[8].trim==0xffec,"decay/trim preserved without unverified scaling");
 check(npno.strokes()[8].decayLadder[13]==0x800000&&npno.strokes()[14].record[4]==3,"ladder and unknown bank preserved");
 check(npno.zoneFor(20)==-1&&npno.zoneFor(109)==-1,"uncovered endpoints");
 nord::PianoEngine engine({a.string(),b.string(),(temp/"missing.npno").string()},4*1024*1024);engine.select(0);
 auto bank=ready(engine,0);check(bank->owned.size()==7&&bank->metadata.size()==15,"only Attack decoded; other bank records preserved");
 std::set<size_t> selected;
 for(int n=0;n<128;++n)for(int group=0;group<3;++group)for(int v=1;v<=127;++v){
  const auto* actual=bank->sampleFor(n,v,nord::SampleBank(group));
  if(group!=0||!npno.hasBank(n,nord::SampleBank(group))){check(!actual,"missing bank remains null");continue;}
  const auto& expected=npno.select(n,v,nord::SampleBank(group));
  check(actual&&actual->sourceStroke==size_t(&expected-npno.strokes().data()),"every velocity and boundary matches parser");
  selected.insert(actual->sourceStroke);check(actual->l[0]==20+int(actual->sourceStroke),"PCM matches source stroke");
 }
 check(selected.size()==7,"no reachable layer discarded");
 check(!bank->sampleFor(-1,127)&&!bank->sampleFor(128,1)&&!bank->sampleFor(60,0)&&!bank->sampleFor(60,128),"range guards");
 const auto serial=bank->serial;engine.endBlock();engine.select(0);bank=ready(engine,0);check(bank->serial==serial,"same selection no republish");engine.endBlock();
 engine.select(1);bank=ready(engine,1);check(!bank->sampleFor(60,127)->hasLoop(),"invalid loop one-shot fallback");engine.endBlock();
 engine.select(0);bank=ready(engine,0);engine.endBlock();check(engine.stats().cacheHit,"LRU cache reuse");check(engine.stats().cacheBytes<=engine.cacheBudget(),"explicit memory budget");
 // Hold the single reader while a worker withdraws a bank: data must stay alive.
 bank=ready(engine,0);const auto* held=bank->sampleFor(60,127);engine.select(1);std::this_thread::sleep_for(std::chrono::milliseconds(5));check(held->l[0]==20,"reader lifetime");engine.endBlock();bank=ready(engine,1);engine.endBlock();
 for(int i=0;i<100;++i){engine.select(i%3);}
 engine.select(0);bank=ready(engine,0);check(bank->instrument==0,"rapid selection latest wins");engine.endBlock();
 {nord::PianoEngine noCache({a.string(),b.string()},0);noCache.select(0);ready(noCache,0);noCache.endBlock();noCache.select(1);ready(noCache,1);noCache.endBlock();noCache.select(0);ready(noCache,0);noCache.endBlock();check(!noCache.stats().cacheHit&&noCache.stats().cacheBytes==0,"zero cache budget");}
 {nord::PianoEngine lru({a.string(),b.string(),a.string()},600000);
  for(int index:{0,1,2,0}){lru.select(index);ready(lru,index);lru.endBlock();}
  check(!lru.stats().cacheHit&&lru.stats().cacheBytes<=600000,"LRU eviction under finite budget");}
 // Truncation, inconsistent counts, invalid blocks and decode cancellation.
 const auto bad=temp/"bad.npno";{std::ofstream f(bad);f<<"CBIN";}
 bool rejected=false;try{nord::Npno broken;broken.open(bad.string());}catch(...){rejected=true;}check(rejected,"truncated header");
 rejected=false;try{npno.decode(npno.strokes()[0],[]{return true;});}catch(...){rejected=true;}check(rejected,"decode cancels at block boundary");
 fs::copy_file(a,bad,fs::copy_options::overwrite_existing);{std::fstream f(bad,std::ios::binary|std::ios::in|std::ios::out);f.seekp(4096);f.put(0);f.put(0);}
 rejected=false;try{nord::Npno broken;broken.open(bad.string());broken.decode(broken.strokes()[0]);}catch(...){rejected=true;}check(rejected,"invalid codec header");
 rejected=false;try{nord::Npno empty;empty.select(60,127);}catch(...){rejected=true;}check(rejected,"select before open safe");
 fs::copy_file(a,bad,fs::copy_options::overwrite_existing);fs::resize_file(bad,4097);
 rejected=false;try{nord::Npno broken;broken.open(bad.string());}catch(...){rejected=true;}check(rejected,"truncated audio span");
 fs::copy_file(a,bad,fs::copy_options::overwrite_existing);{std::fstream f(bad,std::ios::binary|std::ios::in|std::ios::out);f.seekp(16+0x8c+61);f.put(char(255));}
 {nord::Npno hole;hole.open(bad.string());check(hole.zoneFor(60)>=0&&hole.zoneFor(61)==-1&&hole.zoneFor(62)>=0,"key-map hole is not bridged by min/max zones");}
 fs::copy_file(a,bad,fs::copy_options::overwrite_existing);{std::fstream f(bad,std::ios::binary|std::ios::in|std::ios::out);f.seekp(16+0x732);for(int i=0;i<4;++i)f.put(char(255));}
 rejected=false;try{nord::Npno broken;broken.open(bad.string());}catch(...){rejected=true;}check(rejected,"offset addition does not wrap into header");
 // Intentional level correction: nominal NPNO full scale 8192 with 6 dB headroom
 // maps to 16384, rather than the former 32768. Timing/envelope assertions remain.
 // Voice tests use the production renderer, not a duplicated DSP implementation.
 auto rb=renderBank();nord::PianoRenderer r;r.setBank(rb.get());float l,rr;
 r.midi(0xb0,64,127);r.midi(0x90,60,127);check(r.newestSample()->bank==0,"pedal must not change Attack sample");
 const auto* attack=r.newestSample();r.midi(0x80,60,0);tick(r,4);check(r.newestSample()==attack,"Note Off under pedal must not restart sample");
 r.midi(0xb0,64,0);check(r.newestSample()==attack,"pedal release must not trigger Release");tick(r,32);check(!r.activeVoices(),"finite PCM ends under sustain");
 r.reset();r.midi(0x90,60,127);r.midi(0x90,60,127);r.midi(0x80,60,0);r.render(l,rr);check(l<2000.f/16384+1e-7,"Note Off adds no release attack");r.midi(0x80,60,0);tick(r,32);check(!r.activeVoices(),"no stuck retriggers");
 r.reset();r.midi(0x90,60,127);r.midi(0x91,60,127);r.midi(0xb0,120,0);check(r.activeVoices()==1,"channels independent");r.midi(0x81,60,0);tick(r,32);check(!r.activeVoices(),"channel release");
 r.reset();r.midi(0x90,100,127);r.midi(0x80,100,0);check(r.newestSample()->bank==0,"high notes keep their original sample");tick(r,30);check(!r.activeVoices(),"finite high-note PCM");
 r.reset();r.midi(0xb0,64,127);r.midi(0x90,60,127);r.midi(0x90,61,127);r.midi(0xb0,123,0);check(r.activeVoices()==2,"all notes off respects pedal");r.midi(0xb0,64,0);tick(r,32);check(!r.activeVoices(),"simultaneous pedal release");
 r.reset();r.midi(0xb0,64,127);r.setBank(nullptr);r.setBank(rb.get());r.midi(0x90,61,127);check(r.newestSample()->bank==0,"bank change does not introduce Resonance");r.midi(0xb0,121,0);r.midi(0x80,61,0);tick(r,32);check(!r.activeVoices(),"controller reset and key-off");
 // Valid/invalid/minimum loop markers must not alter the stable one-shot path.
 auto* sample=rb->owned[0].get();sample->l={100,200,300,400,500};
 for(auto marker:std::vector<std::pair<uint32_t,uint32_t>>{{1,2},{4,1},{5,1},{0,0}}){
  sample->loopStart=marker.first;sample->loopLength=marker.second;r.reset();r.midi(0x90,60,127);
  for(int i=0;i<7;++i)r.render(l,rr);check(!r.activeVoices(),"one-shot finishes");
 }
 // Pitch and fractional interpolation at 88.2 kHz.
 sample->loopLength=0;sample->l={0,1000,2000,3000,4000};r.reset();r.setSampleRate(88200);r.midi(0x90,60,127);r.render(l,rr);r.render(l,rr);check(std::abs(l-(1000.f*35002/88200)/16384)<1e-7,"sample-rate pitch interpolation");
 sample->loopStart=0;sample->loopLength=5;r.reset();r.setSampleRate(44100);for(int n=21;n<70;++n)r.midi(0x90,n,127);check(r.activeVoices()==48,"bounded polyphony/stealing");check(!r.keyActive(0,21)&&r.keyActive(0,22)&&r.keyActive(0,69),"steal oldest key, not repeatedly slot zero");r.midi(0xb0,120,0);check(!r.activeVoices(),"all sound off");
 r.midi(0x90,60,127);r.midi(0x90,60,0);tick(r,90000);check(!r.activeVoices(),"velocity zero is note-off");
 sample->l.assign(132300,1000);r.reset();r.midi(0x90,60,127);r.midi(0xb0,64,127);r.midi(0x80,60,0);tick(r,1000);r.render(l,rr);check(std::abs(l-1000.f/16384)<1e-6,"pedal retains envelope");r.midi(0xb0,64,0);tick(r,4410);r.render(l,rr);check(std::abs(l-(1000.f/16384)*std::exp(-0.1f/0.22f))<1e-5,"legacy release time constant");tick(r,90000);check(!r.activeVoices(),"released voices terminate");
 fs::remove_all(temp);std::cout<<"All velocity/bank, loop, sustain, renderer, cache and concurrency regressions passed\n";
 }catch(const std::exception& e){if(!temp.empty())fs::remove_all(temp);std::cerr<<e.what()<<'\n';return 1;}}
