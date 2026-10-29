#include "SampleSynthEngine.hpp"
#include "PianoEngine.hpp"
#include <vector>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace nord::synth;
int main(){try{
 nord::Bank bank;bank.pure=true;SampleSynthEngine engine;engine.prepare(48000,256);engine.setBank(&bank);SynthParameters p;p.set(pure_osc1_waveform,0);p.set(bend_range,2);p.set(bend_down,-8);engine.setParameters(p);
 float l[256],r[256];
 auto event=[&](MidiVoiceEvent::Type t,int value){MidiVoiceEvent e;e.type=t;e.channel=0;e.note=t==MidiVoiceEvent::Type::NoteOn?69:0;e.value=t==MidiVoiceEvent::Type::NoteOn?100:t==MidiVoiceEvent::Type::Bend?0:uint8_t(value);e.pitchBend14=int16_t(value);engine.render({48000,256,&e,1,l,r});};
 auto measure=[&](double expected){std::vector<double>cross;double previous=0;for(int b=0;b<260;++b){engine.render({48000,256,nullptr,0,l,r});for(int i=0;i<256;++i){if(!std::isfinite(l[i])||!std::isfinite(r[i]))throw std::runtime_error("non-finite");if(b>40&&previous<=0&&l[i]>0)cross.push_back(b*256+i-l[i]/(l[i]-previous));previous=l[i];}}if(cross.size()<10)throw std::runtime_error("no tone");double hz=48000*(cross.size()-1)/(cross.back()-cross.front());double cents=1200*std::log2(hz/expected);std::cout<<"expected="<<expected<<" hz="<<hz<<" error_cents="<<cents<<'\n';if(std::abs(cents)>1)throw std::runtime_error("pitch error");};
 event(MidiVoiceEvent::Type::NoteOn,0);measure(440);
 event(MidiVoiceEvent::Type::Bend,8191);measure(440*std::exp2(2./12));
 p.set(bend_range,12);engine.setParameters(p);measure(440*std::exp2(2./12));
 event(MidiVoiceEvent::Type::Bend,8191);measure(880);
 event(MidiVoiceEvent::Type::Bend,-8192);measure(440*std::exp2(-8./12));
 event(MidiVoiceEvent::Type::ResetControllers,0);measure(440);
 p.set(bend_range,2);engine.setParameters(p);event(MidiVoiceEvent::Type::CC1,127);
 std::vector<double>cents;double previous=0,last=-1;for(int b=0;b<300;++b){engine.render({48000,256,nullptr,0,l,r});for(int i=0;i<256;++i){if(previous<=0&&l[i]>0){double at=b*256+i-l[i]/(l[i]-previous);if(b>50&&last>=0)cents.push_back(1200*std::log2((48000/(at-last))/440));last=at;}previous=l[i];}}
 double lo=*std::min_element(cents.begin(),cents.end()),hi=*std::max_element(cents.begin(),cents.end());std::cout<<"CC1 vibrato cents "<<lo<<" .. "<<hi<<'\n';if(lo> -45||hi<45||lo< -55||hi>55)throw std::runtime_error("vibrato depth");
 event(MidiVoiceEvent::Type::ResetControllers,0);measure(440);std::cout<<"Pure/MIDI asymmetric Bend, range-only edit, CC1/reset PASS\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
