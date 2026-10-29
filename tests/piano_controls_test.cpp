#include "PianoRenderer.hpp"
#include "PianoSustain.hpp"
#include "EngineResolver.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace nord;
static void ck(bool b,const char* m){if(!b)throw std::runtime_error(m);}
static double render(PianoRenderer& r,int frames){double q=0;for(int i=0;i<frames;++i){float l,h;r.render(l,h);ck(std::isfinite(l)&&std::isfinite(h),"finite");q+=double(l)*l;}return std::sqrt(q/frames);}
int main(){try{
 ck(resolveEngine("Organ/Anything.NpNo")==EngineKind::Piano,"format selects engine");ck(resolveEngine("x.nsmp4")==EngineKind::SampleSynth&&resolveEngine("x.nsmp3")==EngineKind::Unsupported,"format routes without fake older engine");
 for(int v=1;v<128;++v){ck(touchVelocity(v,0)==v,"default touch");ck(touchVelocity(v,1)<=touchVelocity(v,2)&&touchVelocity(v,2)<=touchVelocity(v,3),"ordered touch");if(v>1)for(int m=0;m<4;++m)ck(touchVelocity(v,m)>=touchVelocity(v-1,m),"monotonic");}
 Bank b;b.serial=1;Sample s;s.root=60;s.l.assign(2000,8192);s.loopStart=500;s.loopLength=1000;s.decay=8388500;s.decayLadder.fill(8388550);
 for(auto& row:b.maps[0])for(auto& p:row)p=&s;
 for(double rate:{44100.,48000.})for(int model:{3}){
 PianoRenderer r;r.setBank(&b);r.setSampleRate(rate);PianoControls c;r.setControls(c);r.midi(0x90,60,127);
 double early=render(r,1000);double late=render(r,int(rate));ck(early>0,"attack");ck((r.activeVoices()>0)==true,"loop versus finite");
 r.midi(0x80,60,0);render(r,int(4*rate));ck(!r.activeVoices(),"release completes");
 }
 // Attack is untouched; a single Ladder pole begins at loop entry.
 {PianoRenderer r;r.setBank(&b);r.midi(0x90,60,127);float l,h;r.render(l,h);double initial=l;
 for(int i=1;i<44100;++i)r.render(l,h);
 double elapsed=std::max(0.,44100.*kNpnoRate/44100-s.loopStart);
 ck(std::abs(l/initial-std::pow(sustainCoefficient(s),elapsed))<.002,"single pole after entry");}
 {auto decay=s.decay;s.decay=0;PianoRenderer r;r.setBank(&b);PianoControls c;r.setControls(c);r.midi(0x90,60,100);ck(render(r,10000)>0&&r.activeVoices()==1,"zero Decay sentinel uses ladder");s.decay=decay;}
 // No loop or invalid/one-frame loop cannot be extended by the experimental path.
 for(auto limits:{std::pair<unsigned,unsigned>{0,0},{2000,2},{1900,500},{500,1}}){auto old=s.loopStart,len=s.loopLength;s.loopStart=limits.first;s.loopLength=limits.second;PianoRenderer r;r.setBank(&b);PianoControls c;r.setControls(c);r.midi(0x90,60,127);render(r,3000);ck(!r.activeVoices(),"invalid loop fallback");s.loopStart=old;s.loopLength=len;}
 // Soft Release extends only the release envelope; it never changes the sample.
 double tails[2];for(int soft=0;soft<2;++soft){PianoRenderer r;r.setBank(&b);PianoControls c;c.softRelease=soft;r.setControls(c);r.midi(0x90,60,100);render(r,1000);auto* sample=r.newestSample();r.midi(0x80,60,0);ck(r.newestSample()==sample&&r.activeVoices()==1,"no release attack");render(r,10000);tails[soft]=render(r,1000);}ck(tails[1]>tails[0]*1.1,"soft release functional");
 for(int mode=0;mode<3;++mode){PianoRenderer r;r.setBank(&b);PianoControls c;c.timbre=mode;r.setControls(c);r.midi(0x90,60,127);render(r,1000);r.midi(0xb0,64,127);r.midi(0x80,60,0);render(r,44100);ck(r.activeVoices()==1,"pedal holds loop");r.midi(0xb0,64,0);render(r,4*44100);ck(!r.activeVoices(),"pedal up releases");}
 // Timbre must have an effect, with exact neutral bypass covered by legacy regression.
 s.l.resize(44100);for(size_t i=0;i<s.l.size();++i)s.l[i]=i%2?8192:-8192;s.loopStart=100;s.loopLength=1000;
 double tone[3];for(int t=0;t<3;++t){PianoRenderer r;r.setBank(&b);PianoControls c;c.timbre=t;r.setControls(c);r.midi(0x90,60,127);render(r,10000);tone[t]=render(r,10000);}ck(tone[1]<tone[0]&&tone[2]>tone[0],"timbre functional");
 // Fractional source clock and block independence, including exact-timestamp MIDI.
 for(double rate:{44100.,48000.}){
 auto sequence=[&](int block){PianoRenderer r;r.setBank(&b);r.setSampleRate(rate);PianoControls c;r.setControls(c);std::vector<float> out;for(int f=0;f<int(4*rate);){int end=std::min(f+block,int(4*rate));for(;f<end;++f){if(f==0)r.midi(0x90,61,100);if(f==int(rate))r.midi(0x80,61,0);float l,h;r.render(l,h);ck(std::isfinite(l),"fractional finite");out.push_back(l);}}return out;};auto reference=sequence(1);ck(reference==sequence(256)&&reference==sequence(1024),"block invariant");
 }
 // Mid-voice host-rate changes preserve source-time decay, not the old output rate.
 s.l.assign(5000,8192);s.loopStart=100;s.loopLength=1000;
 auto afterSecond=[&](bool switchRate){PianoRenderer r;r.setBank(&b);PianoControls c;r.setControls(c);r.midi(0x90,60,127);render(r,22050);if(switchRate)r.setSampleRate(48000);render(r,switchRate?24000:22050);float l,h;r.render(l,h);return l;};
 // The intended x2 output correction also doubles absolute float rounding.
 // Preserve the previous RELATIVE bound: .0002/.25 = .0008 of nominal level.
 const double rateError=std::abs(afterSecond(true)-afterSecond(false))/.5;
 std::cout<<"RATE_CHANGE relative_error="<<rateError<<" bound=.0008\n";
 ck(rateError<.0008,"rate change preserves decay time (same relative bound)");
 s.loopLength=0;
 for(double rate:{44100.,48000.}){PianoRenderer r;r.setBank(&b);r.setSampleRate(rate);PianoControls c;r.setControls(c);r.midi(0x90,60,127);int frames=0;while(r.activeVoices()&&frames<100000){render(r,1);++frames;}ck(std::abs(frames-std::ceil(s.l.size()*rate/35002.))<=1,"native duration");}
 std::cout<<"Touch, Q23, loop bounds, soft release, timbre, CC64, native clock and partitions PASS\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
