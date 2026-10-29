#include "PianoRenderer.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>
#include <stdexcept>
#include <vector>
#include <time.h>
// Reference voice core transcribed from the existing bootstrap plugin (v0.4),
// corroborated by the v0.5.1 report. Not an exact recovered v0.5.1 plugin revision.
struct LegacyRenderer {
 struct Voice{bool active=false,released=false,held=false;int note=0;double pos=0,step=1;float amp=0,env=1;};
 std::array<Voice,48> voices{};std::vector<float> l,r;bool pedal=false,endedLastSample=false;
 explicit LegacyRenderer(const nord::Sample& sample){for(auto v:sample.l)l.push_back(v/32768.f);for(auto v:sample.r.empty()?sample.l:sample.r)r.push_back(v/32768.f);}
 void midi(int status,int note,int value){
  int type=status&0xf0;
  if(type==0x90&&value){Voice* slot=&voices[0];for(auto& v:voices)if(!v.active){slot=&v;break;}*slot=Voice{true,false,false,note,0,35002./44100*std::pow(2.,(note-60)/12.),value/127.f,1};}
  else if(type==0x80||(type==0x90&&!value)){for(auto& v:voices)if(v.active&&v.note==note){if(pedal)v.held=true;else v.released=true;}}
  else if(type==0xb0&&note==64){if(pedal&&value<64)for(auto& v:voices)if(v.held){v.held=false;v.released=true;}pedal=value>=64;}
 }
 void render(float& left,float& right){
  left=right=0;endedLastSample=false;const float coef=std::exp(-1.f/(0.22f*44100));
  for(auto& v:voices){if(!v.active)continue;size_t at=size_t(v.pos);if(at>=l.size()){endedLastSample=endedLastSample||at<l.size();v=Voice{};continue;}size_t next=std::min(at+1,l.size()-1);float t=float(v.pos-at),scale=v.amp*v.env;
   left+=(l[at]+(l[next]-l[at])*t)*scale;right+=(r[at]+(r[next]-r[at])*t)*scale;v.pos+=v.step;
   if(v.released){v.env*=coef;if(v.env<0.0001f)v=Voice{};}
  }
 }
};
static void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
static std::unique_ptr<nord::Bank> synthetic(){
 auto b=std::make_unique<nord::Bank>();b->serial=1;b->damperTop=90;
 for(int group=0;group<3;++group){auto s=std::make_unique<nord::Sample>();s->root=60;s->bank=group;s->l.resize(88200);
  for(size_t i=0;i<s->l.size();++i)s->l[i]=group==2?24000:int16_t(12000*std::exp(-double(i)/44100)*std::sin(i*6.283185307179586/100));
  if(group<2){s->loopStart=22050;s->loopLength=22050;}
  for(auto& row:b->maps[group])for(int v=1;v<128;++v)row[v]=s.get();b->owned.push_back(std::move(s));}
 return b;
}
static double cpuTime(){timespec t{};clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t);return t.tv_sec*1e3+t.tv_nsec/1e6;}
template<class Renderer>static void benchmark(Renderer& r,const char* mode,int block,bool spread=false){
 for(int i=0;i<48;++i)r.midi(0x90,spread?21+i:60,100);
 std::vector<double> wall,cpu;float l,b;double sum=0;size_t missed=0;
 for(int i=0;i<64;++i){auto start=std::chrono::steady_clock::now();auto c=cpuTime();for(int j=0;j<block;++j){r.render(l,b);sum+=l;}cpu.push_back(cpuTime()-c);double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();wall.push_back(elapsed);missed+=elapsed>1000.*block/44100;}
 std::sort(wall.begin(),wall.end());double avg=0;for(double v:wall)avg+=v/wall.size();
 std::cout<<"BENCH,"<<mode<<','<<block<<','<<avg<<','<<wall.back()<<','<<wall[62]<<','<<1000.*block/44100<<','<<missed<<','<<*std::max_element(cpu.begin(),cpu.end())<<','<<sum<<'\n';
}
int main(int argc,char** argv){try{
 bool observe=false,bench=false;int model=0;std::string path;
 for(int i=1;i<argc;++i){std::string a=argv[i];if(a=="--observe")observe=true;else if(a=="--bench")bench=true;else if(a.rfind("--model=",0)==0)model=std::stoi(a.substr(8));else path=a;}
 std::unique_ptr<nord::Bank> artificial;std::unique_ptr<nord::PianoEngine> engine;const nord::Bank* bank=nullptr;
 if(path.empty()){artificial=synthetic();bank=artificial.get();}else{
  engine=std::make_unique<nord::PianoEngine>(std::vector<std::string>{path},0);engine->select(0);
  for(int i=0;i<120000;++i){bank=engine->beginBlock();if(bank)break;engine->endBlock();std::this_thread::sleep_for(std::chrono::milliseconds(1));}check(bank,"load timeout");}
 const auto* sample=bank->sampleFor(60,100);check(sample&&!sample->l.empty(),"C4 sample required");
 // One held note beyond physical PCM end; compare to the former finite playback.
 size_t pcmEnd=size_t(std::ceil(sample->l.size()*44100./35002));
 size_t frames=pcmEnd+44100;std::vector<float> reference;double maxDiff=0,afterEnd=0;
 for(int block:{1,32,64,128,256,1024}){
  nord::PianoRenderer r;r.setBank(bank);r.midi(0x90,60,100);std::vector<float> output;output.reserve(frames);
  for(size_t offset=0;offset<frames;offset+=block){r.setBank(bank);for(size_t j=offset;j<std::min(frames,offset+size_t(block));++j){float l,b;r.render(l,b);check(std::isfinite(l)&&std::isfinite(b),"finite audio");output.push_back(l);if(j>=pcmEnd)afterEnd=std::max(afterEnd,std::abs(double(l)));}}
  if(reference.empty())reference=output;else check(reference==output,"PCM depends on block partition");
 }
 // Keep the historical core unchanged; only its expected output is calibrated.
 const double calibration=2;
 LegacyRenderer old(*sample);old.midi(0x90,60,100);
 for(size_t f=0;f<frames;++f){float l,b;old.render(l,b);if(f!=sample->l.size()-1)maxDiff=std::max(maxDiff,std::abs(double(l)*calibration-reference[f]));}
 nord::PianoRenderer off;off.setBank(bank);off.midi(0x90,60,100);float l=0,b=0;
 for(size_t f=0;f<std::min(size_t(22050),sample->l.size()/2);++f)off.render(l,b);
 const auto* before=off.newestSample();size_t voices=off.activeVoices();off.midi(0x80,60,0);const auto* after=off.newestSample();float next,right;off.render(next,right);
 bool restarted=after&&after!=before;
 LegacyRenderer offLegacy(*sample);offLegacy.midi(0x90,60,100);
 for(size_t f=0;f<std::min(size_t(22050),sample->l.size()/2);++f){float a,b;offLegacy.render(a,b);}
 offLegacy.midi(0x80,60,0);float expectedNext,expectedRight;offLegacy.render(expectedNext,expectedRight);
 const double offError=std::abs(double(next)-expectedNext*calibration);
 std::cout<<"OBS,"<<(path.empty()?"synthetic":path)<<",before_bank="<<int(before->bank)<<",after_bank="<<(after?int(after->bank):-1)<<",voices="<<voices<<'/'<<off.activeVoices()<<",off_jump="<<std::abs(next-l)<<",off_vs_legacy_error="<<offError<<",post_pcm_peak="<<afterEnd<<",legacy_max_diff="<<maxDiff<<'\n';
 if(!observe){check(!restarted,"Note Off replaced/restarted the sample");check(afterEnd==0,"unexpected PCM replay after end");check(maxDiff<1e-6,"one-shot timing/waveform differs after intentional NPNO scale correction");check(offError<1e-6,"Note Off discontinuity vs legacy");}
 // Deterministic successive notes/chords, CC64, retrigger and boundary-straddling MIDI.
 struct Event{size_t frame;uint8_t status,note,value;};
 for(bool pedal:{false,true}){
  std::vector<Event> events={{0,0xb0,64,uint8_t(pedal?127:0)},{0,0x90,60,1},{137,0x90,64,64},{509,0x90,67,127},{800,0x80,60,0},{1100,0x80,64,0},{2048,0xb0,64,0},{4096,0x80,67,0},{5000,0x90,60,100},{7000,0x80,60,0}};
  std::vector<float> prior;
  for(int block:{32,64,128,256,1024}){
   nord::PianoRenderer r;r.setBank(bank);LegacyRenderer legacy(*sample);size_t event=0;std::vector<float> data;data.reserve(100000);
   for(size_t start=0;start<100000;start+=block){r.setBank(bank);for(size_t f=start;f<std::min(size_t(100000),start+block);++f){
    while(event<events.size()&&events[event].frame==f){auto e=events[event++];r.midi(e.status,e.note,e.value);legacy.midi(e.status,e.note,e.value);}
    float a,b,oldL,oldR;r.render(a,b);legacy.render(oldL,oldR);check(std::isfinite(a)&&std::isfinite(b),"finite chord audio");data.push_back(a);
    if(!observe&&path.empty()&&!legacy.endedLastSample)check(std::abs(a-oldL*calibration)<1e-6&&std::abs(b-oldR*calibration)<1e-6,"chord/sustain/retrigger differs from former envelope");
   }}
   if(prior.empty())prior=data;else check(prior==data,"chord audio depends on block size");
   if(!observe)check(!r.activeVoices(),"voices stuck after deterministic sequence");
  }
 }
 if(bench){
  std::cout<<"BENCH_HEADER,mode,frames,mean_ms,max_ms,p99_ms,budget_ms,wall_overruns,max_thread_cpu_ms,checksum\n";
  for(int block:{32,64,128,256,1024}){nord::PianoRenderer current;current.setBank(bank);nord::PianoControls controls;current.setControls(controls);benchmark(current,"production",block);LegacyRenderer legacy(*sample);benchmark(legacy,"legacy_core",block);nord::PianoRenderer spread;spread.setBank(bank);spread.setControls(controls);benchmark(spread,"production_48_notes",block,true);}
 }
 if(engine)engine->endBlock();
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
