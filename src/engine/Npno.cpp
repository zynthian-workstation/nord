#include "Npno.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <cstring>
#include <chrono>
#include <limits>
namespace nord {
namespace {
uint16_t be16(const std::vector<uint8_t>& b,size_t i){if(i>b.size() || b.size()-i<2)throw std::runtime_error("NPNO truncado");return (uint16_t(b[i])<<8)|b[i+1];}
uint32_t be32(const std::vector<uint8_t>& b,size_t i){return (uint32_t(be16(b,i))<<16)|be16(b,i+2);}
int16_t signed16(uint16_t v){return v<32768?int16_t(v):int16_t(int(v)-65536);}
}
void Npno::open(const std::string& path){
 using Clock=std::chrono::steady_clock;
 const auto started=Clock::now();
 strokes_.clear();zones_.clear();noteZones_.fill(-1);timings_={};
 std::ifstream f(path,std::ios::binary|std::ios::ate);
 if(!f)throw std::runtime_error("Nao abriu NPNO: "+path);
 const auto length=f.tellg();
 if(length<0 || uint64_t(length)>std::numeric_limits<size_t>::max())throw std::runtime_error("Tamanho NPNO invalido");
 bytes_.resize(size_t(length));f.seekg(0);
 if(!f.read(reinterpret_cast<char*>(bytes_.data()),length))throw std::runtime_error("Leitura NPNO incompleta");
 const auto read=Clock::now();timings_.readMs=std::chrono::duration<double,std::milli>(read-started).count();
 if(bytes_.size()<0x1000 || std::memcmp(bytes_.data(),"CBIN",4)!=0 || std::memcmp(bytes_.data()+8,"npno",4)!=0)throw std::runtime_error("Cabecalho NPNO invalido");
 size_t base=0;bool found=false;for(size_t i=0;i<0x80 && i+4<=bytes_.size();++i)if(std::memcmp(bytes_.data()+i,"CNSP",4)==0){base=i;found=true;break;}
 if(!found)throw std::runtime_error("CNSP nao encontrado");
 damperTop_=bytes_[base+0x40d];if(damperTop_>127)throw std::runtime_error("Damper Top invalido");
 libraryGain_=int8_t(bytes_[base+0x40c]);
 version_=be16(bytes_,base+0x61c);channels_=be16(bytes_,base+0x61e);const int count=be16(bytes_,base+0x620);
 if((channels_!=1&&channels_!=2)||count<=0||base+0x732+size_t(count)*0x76>bytes_.size())throw std::runtime_error("Diretorio CNSP invalido");
 int roots[128]{}; int sum=0;for(int i=0;i<128;++i){roots[i]=be16(bytes_,base+0x622+2*i);sum+=roots[i];}
 if(sum!=count)throw std::runtime_error("Contagem de strokes diverge da tabela de roots");
 zones_.clear();for(int r=0;r<128;++r)if(roots[r])zones_.push_back(Zone{r,128,-1});
 for(int key=0;key<128;++key){const int root=bytes_[base+0x8c+key];if(root==255)continue;for(auto& z:zones_)if(z.root==root){z.low=std::min(z.low,key);z.high=std::max(z.high,key);noteZones_[key]=int(&z-zones_.data());break;}}
 strokes_.clear();strokes_.reserve(count);int z=0,left=roots[zones_.at(0).root];
 for(int i=0;i<count;++i){while(left==0 && z+1<int(zones_.size())){++z;left=roots[zones_[z].root];}--left;
  const size_t p=base+0x732+size_t(i)*0x76;Stroke s;
  s.offset=uint64_t(base)+be32(bytes_,p);s.flags=bytes_[p+4];s.floor=bytes_[p+5];s.frames=be32(bytes_,p+6);s.blocks=be16(bytes_,p+10);s.loopStart=be32(bytes_,p+0x1c);s.loopLength=be32(bytes_,p+0x20);s.zone=z;
  for(int c=0;c<channels_;++c)for(int k=0;k<4;++k)s.history[c][k]=signed16(be16(bytes_,p+(c?0x14:0x0c)+2*(3-k)));
  std::copy_n(bytes_.data()+p,s.record.size(),s.record.data());
  s.decay=be32(bytes_,p+0x2e);s.trim=be16(bytes_,p+0x34);
  for(size_t i=0;i<s.decayLadder.size();++i)s.decayLadder[i]=be32(bytes_,p+0x36+i*4);
  strokes_.push_back(s);
 }
 if(strokes_.size()<2||!strokes_[0].blocks)throw std::runtime_error("Sem blocos para inferir tamanho");
 const auto delta=int64_t(strokes_[1].offset)-strokes_[0].offset;
 if(delta<=0||delta%(2*strokes_[0].blocks))throw std::runtime_error("Bloco nao alinhado");
 blockWords_=size_t(delta/(2*strokes_[0].blocks));
 if(blockWords_<2)throw std::runtime_error("Bloco invalido");
 for(const auto& s:strokes_)if(s.offset>bytes_.size() || uint64_t(s.blocks)*blockWords_*2>bytes_.size()-s.offset)throw std::runtime_error("Blocos fora do arquivo");
 zoneStrokes_.clear();zoneStrokes_.resize(zones_.size());
 for(size_t i=0;i<strokes_.size();++i){const auto& stroke=strokes_[i];if(stroke.flags<3)zoneStrokes_[size_t(stroke.zone)][stroke.flags].push_back(i);}
 timings_.parseMs=std::chrono::duration<double,std::milli>(Clock::now()-read).count();
}
int Npno::zoneFor(int midiNote) const noexcept {return midiNote>=0&&midiNote<128?noteZones_[size_t(midiNote)]:-1;}
bool Npno::hasBank(int note,SampleBank bank) const noexcept {
 const int zi=zoneFor(note);const auto group=size_t(bank);
 return zi>=0&&size_t(zi)<zoneStrokes_.size()&&group<3&&!zoneStrokes_[size_t(zi)][group].empty();
}
const Stroke& Npno::select(int note,int velocity,SampleBank bank)const{
 const int zi=zoneFor(note);
 if(!hasBank(note,bank))throw std::runtime_error("Nota ou banco sem stroke no NPNO");
 // The experimental 31-layer transform excludes many factory IDs (up to 142).
 // Retain the existing attenuation rule, independently per bank, without four-probe truncation.
 const Stroke* picked=nullptr;int prev=0;
 const double v=std::clamp(velocity,1,127);const double att=std::clamp(-200.0*std::log10(v/127.0),0.0,200.0);
 for(size_t index:zoneStrokes_[size_t(zi)][size_t(bank)]){const auto& s=strokes_[index];const int floor=int(s.floor)*(version_<0x464?5:1);if(!picked)picked=&s;else if(floor-(floor-prev)/2<=att)picked=&s;prev=floor;}
 if(!picked)throw std::runtime_error("Zona sem stroke do banco solicitado");
 return *picked;
}
PCM Npno::decode(const Stroke& s,const std::function<bool()>& cancelled)const{
 if((channels_!=1&&channels_!=2)||blockWords_<2||s.offset>bytes_.size()||uint64_t(s.blocks)*blockWords_*2>bytes_.size()-s.offset)throw std::runtime_error("Stroke invalido");
 if(s.frames>20000000)throw std::runtime_error("Stroke excessivamente longo");
 PCM out;out.left.reserve(s.frames);if(channels_==2)out.right.reserve(s.frames);
 int64_t hist[2][4]{};for(int c=0;c<channels_;++c)for(int k=0;k<4;++k)hist[c][k]=s.history[c][k];
 constexpr int coef[5][4]={{0,0,0,0},{1,0,0,0},{2,-1,0,0},{3,-3,1,0},{4,-6,4,-1}};
 for(int b=0;b<s.blocks;++b){const size_t at=size_t(s.offset)+size_t(b)*blockWords_*2;const int hdr=be16(bytes_,at),width=hdr&31,order=(hdr>>5)&7;
  if(width<1||width>30||order>4)throw std::runtime_error("Header de bloco invalido");
  const int64_t own=(int64_t(blockWords_-1)*16)/(width*channels_)-64;
  if(own<=0 || uint64_t(own)>s.frames-out.left.size())throw std::runtime_error("Frames/bloco inconsistentes");
  if(cancelled&&cancelled())throw std::runtime_error("Carregamento cancelado");
  uint64_t bit=0;
  for(int64_t n=0;n<own;++n){if(out.left.size()>=s.frames)throw std::runtime_error("Frames excedem diretorio");
   for(int c=0;c<channels_;++c){uint32_t raw=0;int got=0;
    while(got<width){const int offs=int(bit&15),take=std::min(16-offs,width-got);const uint32_t w=be16(bytes_,at+2+(bit>>4)*2);raw|=((w>>offs)&((1u<<take)-1u))<<got;got+=take;bit+=take;}
    int64_t x=raw>=(1u<<(width-1))?int64_t(raw)-(int64_t(1)<<width):raw;
    for(int k=0;k<order;++k)x+=int64_t(coef[order][k])*hist[c][k];
    for(int k=3;k>0;--k)hist[c][k]=hist[c][k-1];
    hist[c][0]=x;
    if(x<INT32_MIN||x>INT32_MAX)throw std::runtime_error("Overflow no predictor");
    (c==0?out.left:out.right).push_back(int32_t(x));
   }
  }
 }
 if(out.left.size()!=s.frames)throw std::runtime_error("Contagem de frames inconsistente");
 return out;
}
}
