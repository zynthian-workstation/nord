// NSMP4 port based on nord-format 0.8.0 (BSD-3-Clause).
// See licenses/nord-format-BSD3.txt and README.md.
#include "Nsmp4Reader.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <limits>
namespace nord::synth {
namespace {
[[noreturn]] void fail(ReadError c,const char* s){throw Nsmp4Error(c,s);}
struct View {
 const uint8_t* p;size_t n;
 void need(size_t at,size_t count) const {if(at>n||count>n-at)fail(ReadError::InvalidStructure,"NSMP4 truncated/out-of-range");}
 uint8_t u8(size_t at)const {need(at,1);return p[at];}
 uint32_t be(size_t at,size_t bytes=4)const {need(at,bytes);uint32_t v=0;for(size_t i=0;i<bytes;++i)v=v*256+p[at+i];return v;}
 uint32_t le(size_t at,size_t bytes=4)const {need(at,bytes);uint32_t v=0;for(size_t i=bytes;i>0;--i)v=v*256+p[at+i-1];return v;}
 View sub(size_t at,size_t count)const {need(at,count);return {p+at,count};}
 bool tag(size_t at,const char* s)const {need(at,4);return !std::memcmp(p+at,s,4);}
 float f32(size_t at)const {auto bits=be(at);float v;std::memcpy(&v,&bits,4);return v;}
};
uint32_t crc32(View v){uint32_t c=~0u;for(size_t i=0;i<v.n;++i){c^=v.p[i];for(int b=0;b<8;++b)c=(c>>1)^(0xedb88320u&uint32_t(-int32_t(c&1)));}return ~c;}
uint16_t crc16(View v){uint16_t c=0xffff;for(size_t i=0;i<v.n;++i){c^=uint16_t(v.p[i])<<8;for(int b=0;b<8;++b)c=uint16_t((c<<1)^((c&0x8000)?0x1021:0));}return c;}
int64_t sat(__int128 v){return int64_t(std::clamp(v,__int128(INT64_MIN),__int128(INT64_MAX)));}
struct Section {View data;size_t at;uint32_t version;};
constexpr uint32_t fieldRate=44100u*17501u/22050u;
static_assert(fieldRate==35002); // NSMP lattice, derived independently of NPNO.
uint32_t resolve(uint32_t pointer,size_t strokeAt){return uint32_t((pointer+65536-(strokeAt+68)/4%65536)%65536);}
bool terminal(uint32_t raw){return raw==0x800020||raw==0x800040;}
int field(View data,size_t bit,unsigned width){data.need(bit/8,(bit%8+width+7)/8);uint32_t v=0;for(unsigned i=0;i<width;++i)v=v*2+((data.p[(bit+i)/8]>>(7-(bit+i)%8))&1);return (v&(1u<<(width-1)))?int(v)-int(1u<<width):int(v);}
std::shared_ptr<const SampleData> decode(const Section& section,const std::function<bool()>& cancelled,bool decodePcm){
 auto s=section.data;s.need(0,68);if((s.n-68)%4)fail(ReadError::UnsupportedCodec,"NSMP4 partial stream word");
 const View stream=s.sub(68,s.n-68);const size_t words=stream.n/4;
 size_t first=resolve(s.be(20,2),section.at),last=resolve(s.be(47,2),section.at);
 if(last<words)while(last+65536<words)last+=65536;
 if(first>=words||last>=words||first>last||!terminal(stream.be(last*4)))fail(ReadError::UnsupportedCodec,"NSMP4 invalid record directory/terminator");
 unsigned channels=stream.be(last*4)==0x800040?2:1;
 int32_t peak=int32_t(s.be(13,3));if(peak>=0x800000)peak-=0x1000000;
 uint32_t magnitude=uint32_t(std::max<int64_t>(1,std::abs(int64_t(peak))));int bits=0;for(auto v=magnitude;v;v>>=1)++bits;
 int shift=int(s.u8(12))+bits-22-((magnitude&(magnitude-1))==0);
 if(shift< -32||shift>32)fail(ReadError::UnsupportedCodec,"NSMP4 invalid quantizer shift");
 auto pcm=std::make_shared<SampleData>();pcm->channels=channels;pcm->sampleRate=fieldRate;
 std::array<std::array<int64_t,4>,2> history{};
 const int coeff[5][5]={{1},{1,-1},{1,-2,1},{1,-3,3,-1},{1,-4,6,-4,1}};
 size_t mark=resolve(s.be(38,2),section.at);bool marked=false;
 for(size_t i=first;i<last;){
  if(cancelled&&cancelled())fail(ReadError::Cancelled,"NSMP4 load cancelled");
  uint32_t raw=stream.be(i*4);bool one=(raw&0x800000)!=0;unsigned width=((raw>>19)&15)+1,order=(raw>>14)&7;size_t count=raw&0x3fff;
  if((raw>>24)||(raw&0x20000)||order>4||!count||(channels==2&&count%2)||(!one&&count%32)||terminal(raw))fail(ReadError::UnsupportedCodec,"NSMP4 malformed record");
  size_t per=count/channels,halfWords=(per*width+31)/32,span=1+(channels==2?2*halfWords:(count*width+31)/32);
  if(span>last-i)fail(ReadError::UnsupportedCodec,"NSMP4 record desync");
  if(i==mark){if(!(raw&0x40000))fail(ReadError::UnsupportedCodec,"NSMP4 loop pointer lacks mark");pcm->loop={true,pcm->frames,0};marked=true;}
  if((raw&0x40000)&&i!=mark)fail(ReadError::UnsupportedCodec,"NSMP4 unexpected loop mark");
  if((pcm->frames+per)*channels*sizeof(float)>Nsmp4Reader::maxPcmBytes)fail(ReadError::Limit,"NSMP4 PCM limit");
  if(decodePcm)for(unsigned ch=0;ch<channels;++ch){auto& out=ch?pcm->right:pcm->left;auto& h=history[ch];
   for(size_t k=0;k<per;++k){
    size_t bit=k*width;uint32_t val=0;
    if(channels==1)val=uint32_t(field(stream,(i+1)*32+bit,width));
    else {
     // V4 alternates independently padded channel words, for every record.
     for(unsigned b=0;b<width;++b){size_t pos=bit+b;size_t at=(i+1+2*(pos/32)+ch)*4;val=(val<<1)|((stream.u8(at+(pos%32)/8)>>(7-pos%8))&1);}
    }
    int32_t residual=int32_t(val);if(channels==2&&(val&(1u<<(width-1))))residual-=int32_t(1u<<width);
    int64_t value=residual;
    for(unsigned j=1;j<=(one?0:order);++j){int c=coeff[order][j];auto term=sat(__int128(std::abs(c))*h[j-1]);value=sat(__int128(value)+(c<0?__int128(term):-__int128(term)));}
    for(int j=3;j>0;--j)h[j]=h[j-1];h[0]=value;
    int64_t scaled=shift>=0?sat(__int128(value)*(__int128(1)<<shift)):(value>>-shift);
    out.push_back(float(std::clamp(scaled,int64_t(-32768),int64_t(32767)))/32768.f);
   }
  }
  pcm->frames+=per;
  i+=span;while(i<last&&stream.be(i*4)==0)i++;
 }
 if(mark!=last&&!marked)fail(ReadError::UnsupportedCodec,"NSMP4 loop pointer not a record");
 if(!pcm->frames||(decodePcm&&channels==2&&pcm->right.size()!=pcm->frames))fail(ReadError::InvalidStructure,"NSMP4 empty/inconsistent PCM");
 if(marked){pcm->loop.end=pcm->frames;if(pcm->loop.begin>=pcm->loop.end)fail(ReadError::InvalidStructure,"NSMP4 invalid loop");}
 return pcm;
}
}
static std::shared_ptr<const Instrument> readInstrument(const std::string& path,const std::function<bool()>& cancelled,bool decodePcm){
 std::ifstream file(path,std::ios::binary|std::ios::ate);if(!file)fail(ReadError::InvalidContainer,"NSMP4 cannot open file");auto length=file.tellg();
 if(length<44||uint64_t(length)>Nsmp4Reader::maxPcmBytes)fail(ReadError::Limit,"NSMP4 file size limit");
 std::vector<uint8_t> bytes(size_t(length),uint8_t{});file.seekg(0);if(!file.read(reinterpret_cast<char*>(bytes.data()),length))fail(ReadError::InvalidContainer,"NSMP4 read failed");View v{bytes.data(),bytes.size()};
 if(!v.tag(0,"CBIN")||!v.tag(8,"nsmp"))fail(ReadError::InvalidContainer,"NSMP4 requires CBIN/nsmp");
 uint32_t generation=v.le(4),version=v.le(20);if(version<400||version>=500)fail(ReadError::UnsupportedVersion,"NSMP4 UnsupportedVersion");
 size_t base=0,end=bytes.size();
 if(generation==1){base=44;for(size_t i=28;i<44;++i)if(v.u8(i))fail(ReadError::InvalidContainer,"NSMP4 nonzero CBIN pad");if(crc32(v.sub(base,end-base))!=v.le(24))fail(ReadError::Checksum,"NSMP4 CRC32 mismatch");}
 else if(generation==0){base=24;end-=2;if(crc16(v.sub(0,end))!=v.le(end,2))fail(ReadError::Checksum,"NSMP4 CRC16 mismatch");}
 else fail(ReadError::InvalidContainer,"NSMP4 unsupported CBIN generation");
 std::map<uint32_t,Section> strokes;Section map{{nullptr,0},0,0};bool opener=false;
 for(size_t at=base;at<end;){if(end-at<12)fail(ReadError::InvalidStructure,"NSMP4 partial section header");v.need(at,12);uint32_t n=v.be(at+8);if(n>end-at-12)fail(ReadError::InvalidStructure,"NSMP4 section exceeds body");
  if(at==base){if(!v.tag(at,"NSMP"))fail(ReadError::InvalidStructure,"NSMP4 missing NSMP section");opener=true;}
  Section sec{v.sub(at+12,n),at+12-base,v.be(at+4)};
  if(v.tag(at,"\0stk")){sec.data.need(0,68);if(!strokes.emplace(sec.data.be(0),sec).second)fail(ReadError::InvalidStructure,"NSMP4 duplicate stroke gid");}
  if(v.tag(at,"\0map")){if(map.data.p)fail(ReadError::InvalidStructure,"NSMP4 duplicate map");map=sec;}
  at+=12+n;
 }
 if(!opener||!map.data.p||strokes.empty()||strokes.size()>Nsmp4Reader::maxSamples)fail(ReadError::InvalidStructure,"NSMP4 missing/excessive sections");
 size_t record=0,gidAt=0,countAt=0;bool wide=false;
 if(map.version==12){record=11;gidAt=5;}
 else if(map.version==14||map.version==21){record=16;gidAt=8;wide=true;countAt=map.version==14?774:1317;}
 else fail(ReadError::UnsupportedVersion,"NSMP4 unsupported map version");
 auto fits=[&](size_t count){if(count>=map.data.n||map.data.u8(count)!=strokes.size()||strokes.size()*record>map.data.n-count-1)return false;
  for(size_t i=0;i<strokes.size();++i){size_t at=count+1+i*record;auto found=strokes.find(map.data.be(at+gidAt));if(found==strokes.end()||found->second.data.u8(5)!=map.data.u8(at))return false;}return true;};
 if(!wide){bool located=false;for(size_t tail=0;tail<=8;++tail){size_t need=strokes.size()*record+tail+1;if(need>map.data.n)continue;size_t c=map.data.n-need;if(fits(c)){countAt=c;located=true;break;}}if(!located)fail(ReadError::InvalidStructure,"NSMP4 map v12 location");}
 if(!fits(countAt))fail(ReadError::InvalidStructure,"NSMP4 map/stroke mismatch");
 auto instrument=std::make_shared<Instrument>();instrument->contentVersion=version;instrument->mapVersion=map.version;
 // Pinned nord-format zone.rs documents the v21 level-first keyboard table.
 // These are playback gains, not PCM normalization or a region's stroke dB.
 if(map.version==21){
  map.data.need(0,6+128*10);
  instrument->instrumentGain=float(map.data.be(0,3))/1048576.f;
  for(unsigned key=0;key<128;++key)instrument->keyGain[key]=float(map.data.be(6+10*key,3))/1048576.f;
 }

 std::map<uint32_t,uint32_t> sampleIds;
 for(size_t i=0;i<strokes.size();++i){size_t at=countAt+1+i*record;Zone z;z.rootKey=map.data.u8(at);z.keyHigh=map.data.u8(at+1);z.keyLow=wide?map.data.u8(at+2):0;
  if(wide){z.velLow=std::max(uint8_t(1),map.data.u8(at+14));z.velHigh=map.data.u8(at+15);}
  if(z.rootKey>127||z.keyHigh>127||z.keyLow>z.keyHigh||z.velHigh>127||z.velLow>z.velHigh)fail(ReadError::InvalidStructure,"NSMP4 invalid zone range");
  auto gid=map.data.be(at+gidAt);const auto& sec=strokes.at(gid);auto old=sampleIds.find(gid);
  if(old==sampleIds.end()){z.sampleIndex=uint32_t(instrument->samples.size());auto pcm=decode(sec,cancelled,decodePcm);instrument->bytes+=(pcm->left.capacity()+pcm->right.capacity())*sizeof(float)+sizeof(SampleData);if(instrument->bytes>Nsmp4Reader::maxPcmBytes)fail(ReadError::Limit,"NSMP4 instrument memory limit");instrument->samples.push_back(std::move(pcm));sampleIds[gid]=z.sampleIndex;}
  else z.sampleIndex=old->second;
  float gain=sec.data.f32(57);if(std::isnan(gain)||gain==std::numeric_limits<float>::infinity())fail(ReadError::InvalidStructure,"NSMP4 invalid zone gain");z.gainLinear=std::pow(10.f,gain/20.f);if(!std::isfinite(z.gainLinear))fail(ReadError::InvalidStructure,"NSMP4 gain overflow");
  instrument->zones.push_back(z);
 }
 if(!wide)for(auto& z:instrument->zones){int lower=-1;for(const auto& other:instrument->zones)if(other.keyHigh<z.keyHigh)lower=std::max(lower,int(other.keyHigh));z.keyLow=uint8_t(lower+1);}
 instrument->bytes+=sizeof(Instrument)+instrument->zones.capacity()*sizeof(Zone)+instrument->samples.capacity()*sizeof(std::shared_ptr<const SampleData>);
 return instrument;
}
std::shared_ptr<const Instrument> Nsmp4Reader::read(const std::string& path,const std::function<bool()>& cancelled){return readInstrument(path,cancelled,true);}
std::shared_ptr<const Instrument> Nsmp4Reader::inspect(const std::string& path){return readInstrument(path,{},false);}

}
