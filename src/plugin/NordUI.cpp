#include "DistrhoUI.hpp"
#include "NordControls.hpp"
#include "InstrumentLibrary.hpp"
#include <array>
#include <vector>
#include <string>
#include <cmath>
START_NAMESPACE_DISTRHO
class NordUI final:public UI {
 nord::InstrumentLibrary library{{}};std::array<float,nord::NordControls::count> values{};
 std::vector<unsigned> pages;unsigned page=0;int dragging=-1;unsigned scope=nord::NordControls::All;
 std::string instrument="Select an instrument preset in the host";
 void rebuild(){pages.clear();for(unsigned g=0;g<nord::NordControls::groupCount;++g)if((nord::NordControls::group(g).scope&scope)!=0)pages.push_back(g);page=0;dragging=-1;repaint();}
 std::vector<unsigned> controls()const{std::vector<unsigned> out;if(pages.empty())return out;for(unsigned i=0;i<values.size();++i)if(nord::NordControls::definitions()[i].parameter.group==pages[page]&&((nord::NordControls::controlScope(i)&scope)||(!scope&&i==nord::NordControls::Gain)))out.push_back(i);return out;}
 static void text(cairo_t*cr,double x,double y,const std::string&s,int size=15){cairo_set_font_size(cr,size);cairo_move_to(cr,x,y);cairo_show_text(cr,s.c_str());}
 static std::string label(unsigned i,float value){auto&d=nord::NordControls::definitions()[i].parameter;
  if(*d.labels&&std::string(d.labels)!="integer"){std::string labels=d.labels;size_t at=0;for(int n=0;n<int(value);++n){auto next=labels.find('|',at);if(next==std::string::npos)break;at=next+1;}return labels.substr(at,labels.find('|',at)-at);}
  char buffer[40];if(i==nord::NordControls::BendUp)std::snprintf(buffer,sizeof(buffer),"%+.0f",double(value));else if(i==nord::NordControls::BendDown)std::snprintf(buffer,sizeof(buffer),"%.0f",value==0?0.:double(value));else std::snprintf(buffer,sizeof(buffer),"%.3g",double(value));return buffer;
 }
 void change(unsigned i,float x){auto&d=nord::NordControls::definitions()[i].parameter;float n=std::clamp((x-30)/750.f,0.f,1.f);float v=d.minimum+n*(d.maximum-d.minimum);
  if(d.minimum>0&&std::string(d.symbol).find("_hz")!=std::string::npos)v=d.minimum*std::pow(d.maximum/d.minimum,n);
  values[i]=nord::NordControls::clamp(i,v);setParameterValue(i,values[i]);repaint();}
public:
 NordUI():UI(820,450){for(unsigned i=0;i<values.size();++i)values[i]=nord::NordControls::definitions()[i].parameter.initial;library.loadCatalog(getBundlePath()?getBundlePath():"");scope=0;pages={0};setGeometryConstraints(820,450,false);}
protected:
 void parameterChanged(uint32_t i,float v)override{if(i<values.size()&&std::isfinite(v)){values[i]=nord::NordControls::clamp(i,v);repaint();}}
 void stateChanged(const char*key,const char*id)override{if(std::string(key)!="instrument_id")return;auto*entry=library.resolve(id);instrument=entry?entry->name:"Instrument unavailable";
  scope=!entry?0:entry->format==nord::InstrumentFormat::BuiltinPure?nord::NordControls::Pure:entry->engine==nord::EngineKind::Piano?nord::NordControls::Piano:nord::NordControls::Sample;
  if(scope)rebuild();else{pages={0};page=0;repaint();}}
 void onCairoDisplay(const DGL_NAMESPACE::CairoGraphicsContext&ctx)override{
  auto*cr=ctx.handle;cairo_set_source_rgb(cr,.065,.072,.085);cairo_paint(cr);cairo_select_font_face(cr,"sans",CAIRO_FONT_SLANT_NORMAL,CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_source_rgb(cr,.9,.15,.2);text(cr,30,35,"NORD",25);cairo_set_source_rgb(cr,.9,.92,.94);text(cr,150,35,instrument);
  if(!pages.empty())text(cr,30,85,nord::NordControls::group(pages[page]).name,19);
  auto list=controls();for(unsigned k=0;k<list.size();++k){unsigned i=list[k];auto&d=nord::NordControls::definitions()[i].parameter;float y=130+65*k;
   text(cr,30,y,d.name);text(cr,590,y,label(i,values[i]));cairo_set_source_rgb(cr,.2,.23,.27);cairo_rectangle(cr,30,y+12,750,7);cairo_fill(cr);
   float n=(values[i]-d.minimum)/(d.maximum-d.minimum);if(d.minimum>0&&std::string(d.symbol).find("_hz")!=std::string::npos)n=std::log(values[i]/d.minimum)/std::log(d.maximum/d.minimum);
   cairo_set_source_rgb(cr,.85,.15,.2);cairo_rectangle(cr,30,y+12,750*n,7);cairo_fill(cr);cairo_set_source_rgb(cr,.9,.92,.94);
  }
  text(cr,30,425,"< Previous");text(cr,650,425,"Next >");text(cr,380,425,std::to_string(page+1)+" / "+std::to_string(pages.size()),13);
 }
 bool onMouse(const MouseEvent&e)override{
  if(e.button!=1)return false;if(!e.press){if(dragging>=0){editParameter(dragging,false);dragging=-1;return true;}return false;}
  if(e.pos.getY()>395){if(e.pos.getX()<200&&page>0)--page;else if(e.pos.getX()>600&&page+1<pages.size())++page;repaint();return true;}
  auto list=controls();for(unsigned k=0;k<list.size();++k)if(e.pos.getY()>=110+65*k&&e.pos.getY()<=155+65*k){dragging=list[k];editParameter(dragging,true);change(dragging,e.pos.getX());return true;}return false;
 }
 bool onMotion(const MotionEvent&e)override{if(dragging<0)return false;change(dragging,e.pos.getX());return true;}
};
UI*createUI(){return new NordUI();}
END_NAMESPACE_DISTRHO
