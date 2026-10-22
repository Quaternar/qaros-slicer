#include "qarvtk/gl.hpp"
#include "qarvtk/host_panels.hpp"
#include "qarvtk/host_wgl.hpp"
#include "qarvtk/gl_copy.hpp"
#include "../apps/wgl_callbacks.hpp"
#include "../apps/host_panel_config.hpp"
#if QARVTK_HOST_ONBOARDING_API
#include "../apps/host_hub.hpp"
#endif
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <thread>
#include <vector>

namespace qarvtk {
namespace {
using Clock=std::chrono::steady_clock;
template<class Id> std::string id_text(const Id& id){
  std::ostringstream out;out<<std::hex<<std::setfill('0');
  for(auto b:id.data)out<<std::setw(2)<<unsigned(b);
  return out.str();
}
void checked(QarResult result,const char* operation){
  if(qar_result_is_success(result))return;
  char text[1024]{};qar_result_message(result,text,sizeof(text));
  throw std::runtime_error(std::string(operation)+" ("+std::to_string(result.code)+"): "+text);
}
Frame mono_frame(const QarVideoFrameOpenGl& frame){
  if(frame.texture_views_count!=1||frame.textures_count<1||frame.textures_count>QAR_MAX_FRAME_TEXTURES)
    throw std::runtime_error("Panel sender did not provide one color view");
  const auto& v=frame.texture_views[0];
  if(v.data_type!=QAR_VIDEO_FRAME_VIEW_TYPE_COLOR||v.texture_format!=QAR_PIXEL_FORMAT_R8G8B8A8||
     v.eye!=QAR_VIDEO_FRAME_VIEW_EYE_NONE)
    throw std::runtime_error("Panel sender requires mono RGBA color");
  Frame out;
  for(size_t i=0;i<frame.textures_count;++i){const auto& t=frame.textures[i];
    out.textures.push_back({t.texture,t.size.width,t.size.height,t.size.array_layers});}
  out.views.push_back({Eye::left,Plane::color,v.texture_index,v.start_x,v.start_y,v.end_x,v.end_y,v.array_layer_index});
  return out;
}
}

struct HostPanel {
  std::string name;
  std::size_t index{};
  unsigned width{},height{},staging{};
  double x{},y{},z{},width_meters{};
  HGLRC source_context{};
  std::unique_ptr<HostSharedWgl> copying,graphics;
  std::unique_ptr<GlCopy> copier;
  std::unique_ptr<WglCallbacks> callbacks;
  Frame stage;
  // GUI only tries this lock. A busy encoder cannot stall the render thread.
  std::mutex gpu,status_mutex;
  std::atomic<bool> visible{true},closed{false},failed{false};
  std::atomic<uint64_t> generation{0};
  std::string state="Waiting for a rendered slice";
  ~HostPanel(){
    if(copying)try{WglBinding binding(*copying);copier.reset();if(staging)glDeleteTextures(1,&staging);}
      catch(...){copier.release();} // Owned context destruction releases its GL resources.
  }
  void initialize(){
    source_context=wglGetCurrentContext();const auto dc=wglGetCurrentDC();
    if(!source_context||!dc)throw std::runtime_error("Make the slice render window current first");
    copying=std::make_unique<HostSharedWgl>(dc,source_context);
    graphics=std::make_unique<HostSharedWgl>(dc,source_context);
    callbacks=std::make_unique<WglCallbacks>(graphics->dc,graphics->context);
    WglBinding binding(*copying);
    copier=std::make_unique<GlCopy>();
    glGenTextures(1,&staging);glBindTexture(GL_TEXTURE_2D,staging);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Cannot allocate panel staging image");
    stage.textures.push_back({staging,width,height,1});
    stage.views.push_back({Eye::left,Plane::color,0,0,0,width,height,0});
  }
  void status(const std::string& value){std::lock_guard<std::mutex> lock(status_mutex);state=value;}
};

namespace {
// Lives only on the panel worker. The GUI never invokes SDK video/panel calls.
struct PanelStream {
  HostPanel& panel;
  QarSession* session{};
  QarVideoSender* sender{};
  QarGuiPanelId id=qar_gui_panel_id_default();
  QarStreamId stream_id=qar_stream_id_default();
  std::function<void(const std::string&)> log;
  bool created{},shown=true;
  bool begin_logged{},first_frame_logged{};
  uint64_t last_generation{},sent{};
  Clock::time_point last_send{};
  std::vector<QarPeerId> audience;
  PanelStream(HostPanel& p,QarSession* s,std::function<void(const std::string&)> l):panel(p),session(s),log(std::move(l)){}
  ~PanelStream(){
    if(created)qar_gui_panels_close_panel(session,&id);
    if(sender)qar_video_sender_handle_destroy(sender);
  }
  void open(const std::vector<QarPeerId>& peers){
    if(peers.empty())throw std::invalid_argument("A slice panel needs a rendering target");
    log("PANEL_SENDER_CREATE name="+panel.name+" size="+std::to_string(panel.width)+"x"+std::to_string(panel.height)+" api=OpenGL views=1 eye=none format=RGBA8");
    auto gl=panel.callbacks->parameters();
    auto init=qar_video_sender_init_default();init.graphics_api=QAR_GRAPHICS_API_OPENGL;init.header.next=&gl.header;
    init.layout.textures_count=1;init.layout.views_count=1;
    init.layout.textures[0]={QAR_PIXEL_FORMAT_R8G8B8A8,panel.width,panel.height,1};
    init.layout.views[0]={0,0,panel.width,panel.height,0,0,QAR_PIXEL_FORMAT_R8G8B8A8,QAR_VIDEO_FRAME_VIEW_TYPE_COLOR,QAR_VIDEO_FRAME_VIEW_EYE_NONE};
    checked(qar_video_sender_create(session,&init,&sender),"create panel video sender");
    if(!sender)throw std::runtime_error("SDK returned a null panel sender");
    auto content=qar_gui_panel_stream_id_ext_default();
    checked(qar_video_sender_stream_id(sender,&content.stream_id),"get panel stream ID");
    stream_id=content.stream_id;
    log("PANEL_CONTENT_REQUEST name="+panel.name+" type=stream stream="+id_text(stream_id)+" targets="+std::to_string(peers.size()));
    auto gui=qar_gui_panel_init_default();gui.header.next=&content.header;gui.display_name=panel.name.c_str();
    gui.pose.position={float(panel.x),float(panel.y),float(panel.z)};
    gui.size={float(panel.width_meters),float(panel.width_meters*panel.height/panel.width),1.F};
    gui.visible_to_peers=peers.data();gui.visible_to_peer_count=peers.size();
#if QARVTK_HOST_ONBOARDING_API
    QarPeerSpec* raw{};checked(qar_session_get_my_spec(session,&raw),"read panel owner");
    std::unique_ptr<QarPeerSpec,decltype(&qar_peer_spec_handle_destroy)> own(raw,qar_peer_spec_handle_destroy);
    if(!own)throw std::runtime_error("SDK returned a null panel owner");
    QarPeerId source{};checked(qar_peer_spec_get_id(own.get(),&source),"read panel owner ID");
    const auto common=host_resource_name(source,"panel",panel.index);gui.common_name=common.c_str();
    log("PANEL_CREATE_BEGIN name="+panel.name+" common="+common);
#else
    log("PANEL_CREATE_BEGIN name="+panel.name);
#endif
    checked(register_host_panel(session,gui,id,created),"register slice preview panel and apply physical size");
    log("PANEL_SIZE_APPLIED name="+panel.name+" width_m="+std::to_string(gui.size.width_meters)+
        " height_m="+std::to_string(gui.size.height_meters)+" diagonal_inches="+
        std::to_string(std::hypot(gui.size.width_meters,gui.size.height_meters)/.0254));
    audience=peers;
    for(const auto& peer:audience)log("PANEL_TARGET name="+panel.name+" peer="+id_text(peer));
    log("PANEL_CREATED name="+panel.name+" id="+id_text(id));
    panel.status("Waiting for panel receiver");
  }
  void add_peers(const std::vector<QarPeerId>& peers){
    std::vector<const QarPeerId*> additions;
    for(const auto& peer:peers)
      if(std::none_of(audience.begin(),audience.end(),[&](const auto& p){return qar_peer_id_equals(&p,&peer);}))
        additions.push_back(&peer);
    if(additions.empty())return;
    checked(qar_gui_panels_update_visible_to(session,&id,additions.data(),additions.size(),nullptr,0),"add panel targets");
    for(const auto* peer:additions){audience.push_back(*peer);log("PANEL_TARGET_ADDED name="+panel.name+" peer="+id_text(*peer));}
    last_generation=0; // Send a static slice again for the new receiver.
  }
  void tick(){
    const bool visible=panel.visible.load();
    if(visible!=shown){
      checked(qar_gui_panels_set_state(session,&id,visible?QAR_GUI_PANEL_STATE_VISIBLE:QAR_GUI_PANEL_STATE_HIDDEN),"set panel visibility");
      shown=visible;if(visible)last_generation=0;
    }
    if(!visible)return;
    // The latest staged image replaces older video frames. A slow receiver
    // never builds a queue. Refresh a static image every 2 s for reconnects.
    const auto generation=panel.generation.load();
    if(!generation||(generation==last_generation&&Clock::now()-last_send<std::chrono::seconds(2)))return;
    std::unique_lock<std::mutex> lock(panel.gpu,std::try_to_lock);
    if(!lock)return;
    if(!begin_logged){log("PANEL_BEGIN_POLL name="+panel.name);begin_logged=true;}
    bool ready=false;auto result=qar_video_sender_try_begin_frame(sender,&ready);
    if(qar_result_has_code(result,QAR_STATUS_MEDIA_STREAMING_NO_ACTIVE_TRANSFERS)||
       qar_result_has_code(result,QAR_STATUS_MEDIA_STREAMING_STREAM_IS_RECONNECTING))return;
    checked(result,"begin panel frame");if(!ready)return;
    if(!first_frame_logged)log("PANEL_FRAME_ACQUIRED name="+panel.name);
    auto access=qar_video_frame_opengl_default();
    checked(qar_video_sender_frame_opengl(sender,&access),"access panel frame");
    {
      WglBinding binding(*panel.copying);
      panel.copier->copy_color({panel.staging,0,panel.width,panel.height},mono_frame(access),true);
      panel.copier->finish();
    }
    if(!first_frame_logged)log("PANEL_FRAME_COPIED name="+panel.name);
    checked(qar_video_sender_show_frame(sender),"show panel frame");
    if(!first_frame_logged){log("PANEL_FIRST_FRAME name="+panel.name);first_frame_logged=true;}
    last_generation=panel.generation.load();last_send=Clock::now();++sent;
    if(sent==1||sent%30==0)panel.status("Live "+std::to_string(panel.width)+" x "+std::to_string(panel.height)+"; "+std::to_string(sent)+" frames");
  }
};
}

struct HostPanels::Impl {
  DWORD owner=GetCurrentThreadId();
  std::function<void(const std::string&)> log;
  std::mutex list_mutex;
  // Contexts/windows are created and destroyed on the GUI thread. Closed
  // sources stay owned until Stop; the bounded list prevents layout churn leaks.
  std::vector<std::unique_ptr<HostPanel>> panels;
  std::vector<QarPeerId> peers;
  std::thread worker;
  std::atomic<bool> stopping{false};
  void gui()const{if(GetCurrentThreadId()!=owner)throw std::runtime_error("Panel API must run on the Slicer GUI thread");}
  HostPanel& get(HostPanel* p){
    gui();std::lock_guard<std::mutex> lock(list_mutex);
    if(!p||std::none_of(panels.begin(),panels.end(),[&](const auto& v){return v.get()==p;}))throw std::invalid_argument("Invalid preview panel handle");
    return *p;
  }
  void run(QarSession* session)noexcept{
    std::vector<std::unique_ptr<PanelStream>> streams;
    try{
      while(!stopping){
        std::vector<HostPanel*> snapshot;
        std::vector<QarPeerId> audience;
        {std::lock_guard<std::mutex> lock(list_mutex);for(auto& p:panels)snapshot.push_back(p.get());audience=peers;}
        streams.resize(snapshot.size());
        for(size_t i=0;i<snapshot.size()&&!stopping;++i){
          auto& p=*snapshot[i];
          if(p.closed||p.failed){streams[i].reset();continue;}
          try{
            if(streams[i])streams[i]->add_peers(audience);
            if(!p.generation||!p.visible){if(streams[i])streams[i]->tick();continue;}
            if(!streams[i]){
              auto next=std::make_unique<PanelStream>(p,session,log);
              next->open(audience);streams[i]=std::move(next);
              log("PANEL_OPEN "+p.name+" "+std::to_string(p.width)+"x"+std::to_string(p.height));
            }
            streams[i]->tick();
          }catch(const std::exception& e){p.failed=true;p.status(std::string("Error: ")+e.what());streams[i].reset();log("PANEL_ERROR "+p.name+": "+e.what());}
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
    }catch(const std::exception& e){
      stopping=true;
      log(std::string("PANEL_WORKER_ERROR ")+e.what());
      std::lock_guard<std::mutex> lock(list_mutex);for(auto& p:panels){p->failed=true;p->status(e.what());}
    }
    streams.clear(); // Close panels and senders before session/library teardown.
    log("Panel worker exited");
  }
};
HostPanels::HostPanels(std::function<void(const std::string&)> log):impl_(std::make_unique<Impl>()){impl_->log=std::move(log);}
HostPanels::~HostPanels(){request_stop();join();}
HostPanel* HostPanels::create(const char* name,unsigned w,unsigned h,double x,double y,double z,double width_meters){
  impl_->gui();
  if(impl_->stopping)throw std::runtime_error("Panel transport is stopping");
  if(!name||!*name||std::string(name).size()>128||w<16||h<16||w>768||h>768||w%2||h%2)
    throw std::invalid_argument("Panel needs a name and even 16..768 pixel dimensions");
  for(double v:{x,y,z,width_meters})if(!std::isfinite(v)||std::abs(v)>1e12)throw std::invalid_argument("Invalid panel placement");
  if(width_meters<=0)throw std::invalid_argument("Invalid panel width");
  {std::lock_guard<std::mutex> lock(impl_->list_mutex);if(impl_->panels.size()>=16)throw std::runtime_error("Slice views were recreated too often; restart streaming to recreate panels");}
  auto p=std::make_unique<HostPanel>();p->name=name;p->width=w;p->height=h;p->x=x;p->y=y;p->z=z;p->width_meters=width_meters;p->initialize();
  auto* result=p.get();{std::lock_guard<std::mutex> lock(impl_->list_mutex);p->index=impl_->panels.size();impl_->panels.push_back(std::move(p));}return result;
}
int HostPanels::capture(HostPanel* handle,unsigned texture,unsigned w,unsigned h,const ColorRegion* region){
  auto& p=impl_->get(handle);
  if(p.failed)throw std::runtime_error(status(handle));
  if(impl_->stopping||p.closed||!p.visible)return 0;
  if(wglGetCurrentContext()!=p.source_context)throw std::runtime_error("Slice OpenGL context changed; recreate its panel");
  if(!texture||!w||!h)throw std::invalid_argument("Empty slice texture");
  std::unique_lock<std::mutex> lock(p.gpu,std::try_to_lock);if(!lock)return 0;
  glFinish(); // Finish only existing host writes; never ask VTK to render again.
  {WglBinding binding(*p.copying);p.copier->copy_color({texture,0,w,h},p.stage,false,true,region);p.copier->finish();}
  ++p.generation;return 1;
}
void HostPanels::visible(HostPanel* p,bool visible){impl_->get(p).visible=visible;}
void HostPanels::close(HostPanel* p){impl_->get(p).closed=true;}
std::string HostPanels::status(HostPanel* handle){auto& p=impl_->get(handle);std::lock_guard<std::mutex> lock(p.status_mutex);return p.state;}
void HostPanels::start(QarSession* session,const std::vector<QarPeerId>& peers){
  if(impl_->worker.joinable())throw std::runtime_error("Panel worker already started");
  add_peers(peers);
  impl_->worker=std::thread([this,session]{impl_->run(session);});
}
void HostPanels::add_peers(const std::vector<QarPeerId>& peers){
  // An empty QAROS audience means everyone, not nobody. Never create a panel
  // with an empty list or remove its last target during a disconnect.
  if(peers.empty())throw std::invalid_argument("A slice panel needs a rendering target");
  std::lock_guard<std::mutex> lock(impl_->list_mutex);
  for(const auto& peer:peers)
    if(std::none_of(impl_->peers.begin(),impl_->peers.end(),[&](const auto& p){return qar_peer_id_equals(&p,&peer);}))
      impl_->peers.push_back(peer);
}
void HostPanels::request_stop(){impl_->stopping=true;}
void HostPanels::join(){if(impl_->worker.joinable())impl_->worker.join();}
}
