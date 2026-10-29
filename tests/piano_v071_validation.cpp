#include "PianoRenderer.hpp"
#include "PianoSustain.hpp"
#include <thread>
#include <chrono>
#include <stdexcept>
#include <iostream>
using namespace nord;
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(int argc,char**argv){try{
 // Known fundamental, independent of NPNO tuning metadata and dominant partials.
 Bank synthetic;synthetic.serial=1;Sample sine;sine.root=60;sine.l.resize(70004);
 for(size_t i=0;i<sine.l.size();++i)sine.l[i]=int16_t(12000*std::sin(2*3.141592653589793*261.625565*i/kNpnoRate));
 for(auto& row:synthetic.maps[0])row.fill(&sine);
 for(double rate:{44100.,48000.})for(int note:{60,72}){PianoRenderer r;r.setBank(&synthetic);r.setSampleRate(rate);r.midi(0x90,note,127);float prev=0,l,h;std::vector<double> crossings;
 for(int i=0;i<int(.8*rate);++i){r.render(l,h);if(prev<0&&l>=0)crossings.push_back(i-l/(l-prev));prev=l;}
 double hz=(crossings.size()-1)*rate/(crossings.back()-crossings.front());double cents=1200*std::log2(hz/(261.625565*std::exp2((note-60)/12.)));
 check(std::abs(cents)<.02,"synthetic tuning error");std::cout<<"PITCH,"<<rate<<','<<note<<','<<hz<<','<<cents<<" cents\n";}
 // Signal-derived compensation is bounded, cyclic and never edits PCM.
 {Sample s;s.loopStart=3500;s.loopLength=35002;s.decay=8388500;s.decayLadder.fill(8388600);s.l.resize(38502);s.r.resize(38502);
 for(size_t i=0;i<s.l.size();++i){double a=.6+.4*std::cos(2*3.141592653589793*(double(i)-s.loopStart)/s.loopLength);s.l[i]=int16_t(12000*a*std::sin(2*3.141592653589793*100*i/s.loopLength));s.r[i]=s.l[i]/2;}
 auto left=s.l,right=s.r;prepareLoopGain(s);check(!s.loopGain.empty(),"modulated sample needs correction");check(s.l==left&&s.r==right,"PCM mutated");
 for(float gain:s.loopGain)check(gain>=.25118f&&gain<=1,"gain bound");
 check(std::abs(loopGainAt(s,s.loopStart)-loopGainAt(s,s.loopStart+s.loopLength-.01))<.00001,"cyclic gain seam");}
 for(int arg=1;arg<argc;++arg){PianoEngine engine({argv[arg]},0);engine.select(0);const Bank* b=nullptr;
 for(int i=0;i<120000;++i){b=engine.beginBlock();if(b)break;engine.endBlock();std::this_thread::sleep_for(std::chrono::milliseconds(1));}check(b,"load failed");
 for(int note:{30,36,60,70,80,84})for(int vel:{50,100}){const auto* sample=b->sampleFor(note,vel);if(!sample)continue;
 for(double rate:{44100.,48000.}){
 std::vector<float> reference;
 for(int block:{1,32,256,1024}){PianoRenderer r;r.setBank(b);r.setSampleRate(rate);std::vector<float> out;out.reserve(size_t(19*rate));
 for(int base=0;base<int(19*rate);base+=block){r.setBank(b);for(int f=base;f<std::min(base+block,int(19*rate));++f){
 if(f==0)r.midi(0x90,note,vel);
 if(f==int(rate))r.midi(0xb0,64,127);
 if(f==int(2*rate)){auto* before=r.newestSample();r.midi(0x80,note,0);check(r.newestSample()==before,"pedal note off restarted");}
 if(f==int(4*rate))check(r.activeVoices()==1,"pedal failed to sustain");
 if(f==int(15*rate)){auto* before=r.newestSample();r.midi(0xb0,64,0);check(r.newestSample()==before,"pedal up restarted");}
 float l,h;r.render(l,h);check(std::isfinite(l)&&std::isfinite(h),"nonfinite");out.push_back(l);
 }}check(!r.activeVoices(),"stuck pedal release");if(reference.empty())reference=std::move(out);else check(reference==out,"block dependence");
 // Retrigger and FIFO note-offs, followed by exact release completion.
 r.reset();r.midi(0x90,note,vel);r.midi(0x90,note,vel);r.midi(0x80,note,0);check(r.keyActive(0,note),"retrigger FIFO");r.midi(0x80,note,0);check(!r.keyActive(0,note),"stuck key");for(int f=0;f<int(4*rate);++f){float l,h;r.render(l,h);}check(!r.activeVoices(),"retrigger stuck");
 }
 }}engine.endBlock();std::cout<<"LIFECYCLE,"<<argv[arg]<<",PASS\n";}
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
