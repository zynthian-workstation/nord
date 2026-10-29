#include "ScxtBlockAdapter.hpp"
#include <vector>
#include <stdexcept>
#include <iostream>
using namespace nord::synth;
int main(){try{std::vector<float> reference;
 for(unsigned block:{1,7,15,16,17,31,64,127,512}){ScxtBlockAdapter a;a.prepare(512);bool on=false;float phase=0;std::vector<float> out(4096),right(4096);
 for(unsigned base=0;base<4096;base+=block){unsigned n=std::min(block,4096-base);MidiVoiceEvent events[2];unsigned count=0;for(auto pair:{std::pair<unsigned,bool>{7,true},{1025,false}})if(pair.first>=base&&pair.first<base+n)events[count++]={pair.second?MidiVoiceEvent::Type::NoteOn:MidiVoiceEvent::Type::NoteOff,pair.first-base,0,60,100,0};RenderContext c{44100,n,events,count,out.data()+base,right.data()+base};a.render(c,[&](auto e){on=e.type==MidiVoiceEvent::Type::NoteOn;},[&](float*l,float*r,uint64_t){for(int i=0;i<16;++i){l[i]=r[i]=on?++phase:0;}});}
 if(out[15]!=0||out[16]!=1||out[1039]==0||out[1040]!=0||a.overflows())throw std::runtime_error("event quantization");if(reference.empty())reference=out;else if(reference!=out)throw std::runtime_error("host partitions");}
 {ScxtBlockAdapter a;a.prepare(512);float output[1],right[1],value=1;RenderContext c{44100,1,nullptr,0,output,right};auto generate=[&](float*l,float*r,uint64_t){std::fill_n(l,16,value);std::fill_n(r,16,value);};a.render(c,[](auto){},generate);value=2;for(int i=1;i<16;++i){a.render(c,[](auto){},generate);if(output[0]!=1)throw std::runtime_error("parameter changed buffered audio");}a.render(c,[](auto){},generate);if(output[0]!=2)throw std::runtime_error("parameter not applied to next quantum");}
 std::cout<<"T5: 9 partitions identical, event error 9/15 frames, no anticipation/overflow PASS\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
