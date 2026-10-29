#pragma once
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>
// Structurally valid, tiny mono NPNO with two normal strokes, 16 frames each.
static void fixture(const std::filesystem::path& path,int value=64){
 std::filesystem::create_directories(path.parent_path());std::vector<unsigned char> b(4260);
 auto word=[&](size_t p,int v){b[p]=v>>8;b[p+1]=v;};
 auto dword=[&](size_t p,int v){word(p,v>>16);word(p+2,v);};
 std::copy_n("CBIN",4,b.begin());std::copy_n("npno",4,b.begin()+8);std::copy_n("CNSP",4,b.begin()+16);
 size_t base=16;word(base+0x61c,0x464);word(base+0x61e,1);word(base+0x620,2);word(base+0x622+120,2);b[base+0x8c+60]=60;
 for(int i=0;i<2;++i){size_t p=base+0x732+i*0x76;dword(p,4096+i*82-16);dword(p+6,16);word(p+10,1);word(4096+i*82,8);for(int j=2;j<82;++j)b[4096+i*82+j]=value;}
 std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<char*>(b.data()),b.size());
}
// Two zones, different velocity structures, all three known banks and one unknown.
static void richFixture(const std::filesystem::path& path,bool invalidLoop=false){
 struct Take{int root,bank,layer;};
 const std::vector<Take> takes={{60,0,0},{60,0,20},{60,0,50},{60,0,90},{60,0,130},{60,1,0},{60,1,40},{60,1,100},{60,2,0},{60,2,50},{60,2,100},{72,0,0},{72,0,60},{72,2,0},{72,3,7}};
 std::filesystem::create_directories(path.parent_path());std::vector<unsigned char> b(4096+takes.size()*82);
 auto word=[&](size_t p,uint16_t v){b[p]=v>>8;b[p+1]=v;};
 auto dword=[&](size_t p,uint32_t v){word(p,v>>16);word(p+2,v);};
 std::copy_n("CBIN",4,b.begin());std::copy_n("npno",4,b.begin()+8);std::copy_n("CNSP",4,b.begin()+16);
 const size_t base=16;word(base+4,0x464);word(base+0x61c,0x464);word(base+0x61e,1);word(base+0x620,takes.size());
 word(base+0x622+120,11);word(base+0x622+144,4);b[base+0x40d]=90;
 std::fill_n(b.begin()+base+0x8c,128,255);
 for(int key=21;key<=108;++key)b[base+0x8c+key]=key<=65?60:72;
 for(size_t i=0;i<takes.size();++i){
  const auto take=takes[i];size_t p=base+0x732+i*0x76;
  dword(p,uint32_t(4096+i*82-base));b[p+4]=take.bank;b[p+5]=take.layer;dword(p+6,16);word(p+10,1);
  if(take.bank<2){dword(p+0x1c,invalidLoop&&i==0?16:4);dword(p+0x20,8);}
  dword(p+0x24,0);dword(p+0x28,16);dword(p+0x2e,take.bank==2?0:0x7fffff);word(p+0x34,take.bank==2?0xffec:3);
  for(int k=0;k<14;++k)dword(p+0x36+k*4,0x800000);
  dword(p+0x6e,uint32_t(1000+i));
  word(4096+i*82,8);std::fill(b.begin()+4096+i*82+2,b.begin()+4096+(i+1)*82,static_cast<unsigned char>(20+i));
 }
 std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),b.size());
}

// Historical category fixtures; production discovers directories dynamically.
static const std::vector<std::string>& fixtureCategories(){
 static const std::vector<std::string> names={"Grand Piano","Upright Piano","Digital Piano","Orquestral","Organ","Strings","Synth","Bass","Brass","Woodwind","Voices_Choir","Percussion Tuned","Accordion","Guitar_Plucked"};return names;
}
