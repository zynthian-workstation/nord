#include "PianoRenderer.hpp"
#include "npno_fixture.hpp"
#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
static void ck(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(){try{
 nord::Bank bank;bank.serial=1;nord::Sample sample;sample.root=60;
 sample.l.assign(140008,8192);sample.r.assign(140008,4096);
 for(auto& row:bank.maps[0])row.fill(&sample);
 // Independent known PCM scale: nominal full scale at velocity127 produces .5
 // before the unchanged public Gain .3. No fade, pan attenuation or per-note AGC.
 for(double rate:{44100.,48000.,96000.})for(int velocity:{32,64,96,112,127}){
  nord::PianoRenderer renderer;renderer.setSampleRate(rate);renderer.setBank(&bank);renderer.midi(0x90,60,velocity);
  float l,r;renderer.render(l,r);float want=.5f*velocity/127;
  ck(std::abs(l-want)<1e-7f&&std::abs(r-.5f*want)<1e-7f,"nominal PCM/velocity/stereo/attack");
  renderer.midi(0xb0,64,127);renderer.midi(0x80,60,0);
  for(int i=0;i<int(.1*rate);++i)renderer.render(l,r);
  ck(std::abs(l-want)<1e-7f,"pedal retains attack gain");
  renderer.midi(0xb0,64,0);for(int i=0;i<int(.22*rate);++i)renderer.render(l,r);
  ck(std::abs(l/want-std::exp(-1.f))<.001f,"unchanged release time");
 }
 auto root=std::filesystem::temp_directory_path()/"nord-level-XXXXXX";auto name=root.string();ck(mkdtemp(name.data()),"temporary path");root=name;
 try{
  auto path=root/"factory.npno";richFixture(path);
  {std::fstream file(path,std::ios::binary|std::ios::in|std::ios::out);file.seekp(16+0x40c);file.put(char(50));}
  nord::PianoEngine engine({path.string()},0);engine.select(0);const nord::Bank* ready=nullptr;
  for(int i=0;i<5000;++i){ready=engine.beginBlock();if(ready)break;engine.endBlock();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  ck(ready,"fixture load");ck(ready->metadata.size()==15,"raw factory metadata preserved without guessing trim/level interaction");engine.endBlock();
 }catch(...){std::filesystem::remove_all(root);throw;}
 std::filesystem::remove_all(root);std::cout<<"NPNO known scale, all velocities, stereo, attack, headroom and sustain PASS\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
