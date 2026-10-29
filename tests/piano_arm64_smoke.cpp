// Portable DSP smoke test; also built with Bookworm's AArch64 GCC and run in QEMU.
#include "PianoRenderer.hpp"
#include "npno_fixture.hpp"
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(){try{
 nord::Bank bank;bank.serial=1;nord::Sample sample;sample.root=60;sample.l.assign(70004,8192);sample.r.assign(70004,4096);
 for(auto& row:bank.maps[0])row.fill(&sample);
 for(double rate:{44100.,48000.,96000.})for(int velocity:{32,64,96,112,127}){
  nord::PianoRenderer renderer;renderer.setSampleRate(rate);renderer.setBank(&bank);renderer.midi(0x90,60,velocity);
  float l,r;renderer.render(l,r);check(std::abs(l-.5f*velocity/127)<1e-7f,"nominal attack level");check(std::abs(r-l*.5f)<1e-7f,"stereo");
  renderer.midi(0xb0,64,127);renderer.midi(0x80,60,0);
  for(int i=0;i<1000;++i)renderer.render(l,r);check(renderer.activeVoices()==1,"sustain");
  renderer.midi(0xb0,64,0);for(int i=0;i<int(rate*4);++i)renderer.render(l,r);
  check(renderer.activeVoices()==0,"release");
 }
 std::string name=(std::filesystem::temp_directory_path()/"nord-portable-XXXXXX").string();check(mkdtemp(name.data()),"temporary directory");
 auto root=std::filesystem::path(name);
 try{auto path=root/"fixture.npno";richFixture(path);nord::Npno decoder;decoder.open(path.string());
  for(int velocity:{32,64,96,112,127}){auto& stroke=decoder.select(60,velocity);auto pcm=decoder.decode(stroke);check(pcm.left.size()==stroke.frames,"decoded frames");}
 }catch(...){std::filesystem::remove_all(root);throw;}
 std::filesystem::remove_all(root);std::cout<<"Portable NPNO decode, fixed scale, stereo, velocity, attack and pedals PASS\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
