#include "qarvtk/gl.hpp"
#include "qarvtk/host_bridge.h"
#include "qarvtk/host_frame_lease.hpp"
#include "qarvtk/host_stream_scheduler.hpp"
#include "qarvtk/host_stream_routes.hpp"
#include "qarvtk/host_stream_startup.hpp"
#include "qarvtk/host_panels.hpp"
#include "qarvtk/host_wgl.hpp"
#include "qarvtk/gl_copy.hpp"
#include "qarvtk/gl_composite.hpp"
#include "../apps/wgl_callbacks.hpp"
#include "../apps/legacy_peer_observer.hpp"
#include "../apps/legacy_frame_wait.hpp"
#include "../apps/host_config.hpp"
#if QARVTK_HOST_ONBOARDING_API
#include "../apps/host_hub.hpp"
#endif
#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <sstream>
#include <iomanip>
#include <thread>
#include <vector>

QAR_IMPLEMENT_DYNAMIC_LOADING()

namespace {
using Clock=std::chrono::steady_clock;
double milliseconds(Clock::time_point from,Clock::time_point to=Clock::now()){
  return std::chrono::duration<double,std::milli>(to-from).count();
}
struct FrameTimings {
  double host_sync{},composite{},copy{},copy_finish{};
  FrameTimings& operator+=(const FrameTimings& other){
    host_sync+=other.host_sync;composite+=other.composite;copy+=other.copy;copy_finish+=other.copy_finish;return *this;
  }
};
std::atomic<bool> active{false};
void message(char* target,uint32_t size,const std::string& text){
  if(target&&size){const auto count=std::min<std::size_t>(text.size(),size-1);std::memcpy(target,text.data(),count);target[count]=0;}
}
void checked(QarResult result,const char* operation){
  if(qar_result_is_success(result))return;
  char text[1024]{};qar_result_message(result,text,sizeof(text));
  throw std::runtime_error(std::string(operation)+" ("+std::to_string(result.code)+"): "+text);
}
template<class Id> std::string id_text(const Id& id){
  std::ostringstream text;text<<std::hex<<std::setfill('0');
  for(auto b:id.data)text<<std::setw(2)<<static_cast<unsigned>(b);
  return text.str();
}
bool extension(const char* wanted){
  GLint count=0;glGetIntegerv(GL_NUM_EXTENSIONS,&count);
  for(GLint i=0;i<count;++i){const auto* name=glGetStringi(GL_EXTENSIONS,i);if(name&&std::strcmp(reinterpret_cast<const char*>(name),wanted)==0)return true;}
  return false;
}
QarGraphicsDeviceId device(){
  if(!extension("GL_EXT_memory_object")||!extension("GL_EXT_memory_object_win32"))
    throw std::runtime_error("The host GPU does not expose Windows external-memory support required by QAROS");
  using GetBytes=void(APIENTRY*)(GLenum,GLubyte*);
  auto get=reinterpret_cast<GetBytes>(wglGetProcAddress("glGetUnsignedBytevEXT"));
  if(!get)throw std::runtime_error("Cannot query the host GPU LUID");
  auto id=qar_graphics_device_id_default();id.id_type=QAR_GPU_DEVICE_ID_TYPE_LUID;
  get(0x9599,reinterpret_cast<GLubyte*>(&id.luid));
  if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Host GPU identity query failed");
  return id;
}
qarvtk::Frame layout(const QarVideoFrameOpenGl& f){
  if(f.textures_count>QAR_MAX_FRAME_TEXTURES||f.texture_views_count>QAR_MAX_FRAME_VIEWS)
    throw std::runtime_error("Invalid SDK frame array sizes");
  qarvtk::Frame out;
  for(size_t i=0;i<f.textures_count;++i){const auto& t=f.textures[i];out.textures.push_back({t.texture,t.size.width,t.size.height,t.size.array_layers});}
  for(size_t i=0;i<f.texture_views_count;++i){
    const auto& v=f.texture_views[i];
    if(v.eye!=QAR_VIDEO_FRAME_VIEW_EYE_LEFT&&v.eye!=QAR_VIDEO_FRAME_VIEW_EYE_RIGHT)
      throw std::runtime_error("Host bridge requires separate left/right views");
    if(v.data_type!=QAR_VIDEO_FRAME_VIEW_TYPE_COLOR&&v.data_type!=QAR_VIDEO_FRAME_VIEW_TYPE_DEPTH)
      throw std::runtime_error("Invalid SDK frame plane");
    const bool depth=v.data_type==QAR_VIDEO_FRAME_VIEW_TYPE_DEPTH;
    if((!depth&&v.texture_format!=QAR_PIXEL_FORMAT_R8G8B8A8)||
       (depth&&v.texture_format!=QAR_PIXEL_FORMAT_D32_FLOAT&&v.texture_format!=QAR_PIXEL_FORMAT_R32_FLOAT))
      throw std::runtime_error("Unsupported SDK pixel format");
    out.views.push_back({v.eye==QAR_VIDEO_FRAME_VIEW_EYE_LEFT?qarvtk::Eye::left:qarvtk::Eye::right,
      depth?qarvtk::Plane::depth:qarvtk::Plane::color,v.texture_index,v.start_x,v.start_y,v.end_x,v.end_y,v.array_layer_index});
  }
  return out;
}
void on_peer(QarPeerSpec* spec,void* state) noexcept {
  QarPeerId id{};QarAppState app=QAR_APP_STATE_UNKNOWN;
  if(spec&&state&&qar_result_is_success(qar_peer_spec_get_id(spec,&id))&&qar_result_is_success(qar_peer_spec_get_app_state(spec,&app)))
    static_cast<qarvtk::LegacyPeerObserver*>(state)->record(id,app);
}
}

struct QarvtkHost {
  struct Volume {
    QarvtkHostPlacement placement{};
    std::string name;
    std::array<std::unique_ptr<qarvtk::HostSharedWgl>,2> graphics;
    std::array<std::unique_ptr<qarvtk::WglCallbacks>,2> callbacks;
  };
  const DWORD owner=GetCurrentThreadId();
  HGLRC host_context{};
  std::unique_ptr<qarvtk::HostSharedWgl> copying;
  std::unique_ptr<qarvtk::GlCopy> copier;
  std::unique_ptr<qarvtk::GlComposite> compositor;
  std::vector<Volume> volumes;
  std::unique_ptr<qarvtk::HostPanels> panels;
  QarGraphicsDeviceId adapter=qar_graphics_device_id_default();
  std::thread worker;
  std::mutex mutex,cancel_mutex;
  std::condition_variable condition;
  std::atomic<bool> stopping{false},done{false};
  QarCancelToken* operation{};
  std::array<QarCancelToken*,2> connection_operations{}; // Protected by cancel_mutex; one per target.
  bool failed{};
  qarvtk::HostFrameLease lease;
  FrameTimings timings;
  const Clock::time_point created=Clock::now();
  uint64_t sent{};
  QarvtkHostFrame request{};
  qarvtk::Frame frame;
  std::array<qarvtk::EyeViews,2> views{};
  std::string status="Starting live Slicer transport",sdk_root,target,log_folder;
  std::ofstream log;

  ~QarvtkHost(){
    if(worker.joinable())worker.join();
    if(copying){
      try {qarvtk::WglBinding binding(*copying);compositor.reset();copier.reset();}
      catch(...){compositor.release();copier.release();} // Context destruction releases GL objects.
    }
  }
  void gui() const {if(GetCurrentThreadId()!=owner)throw std::runtime_error("Host API must run on its creating GUI thread");}
  void log_line(const std::string& text){ // mutex held by caller
    if(log)log<<"elapsed_ms="<<milliseconds(created)<<" "<<text<<std::endl;
  }
  void note(const std::string& text){std::lock_guard<std::mutex> lock(mutex);log_line(text);}
  void set_status(const std::string& text){
    std::lock_guard<std::mutex> lock(mutex);status=text;log_line(text);
  }
  void fail(const std::string& text){
    {std::lock_guard<std::mutex> lock(mutex);if(!failed){failed=true;status=text;log_line("ERROR: "+text);}lease.cancel();}
    stopping=true;condition.notify_all();
  }
  void stop(){
    gui();stopping=true;
    if(panels)panels->request_stop();
    {std::lock_guard<std::mutex> lock(mutex);lease.cancel();log_line("Stop requested by GUI");}
    condition.notify_all();
    std::lock_guard<std::mutex> lock(cancel_mutex);
    if(operation)qar_cancel_token_cancel(operation);
    for(auto* token:connection_operations)if(token)qar_cancel_token_cancel(token);
  }
  bool pause(std::chrono::milliseconds duration){
    std::unique_lock<std::mutex> lock(mutex);
    condition.wait_for(lock,duration,[&]{return stopping.load();});return !stopping;
  }
  void deadline(uint32_t timeout_ms=60000){
    std::lock_guard<std::mutex> lock(cancel_mutex);
    if(stopping)throw std::runtime_error("Live stream stopped");
    if(operation)qar_cancel_token_handle_destroy(operation);
    operation=nullptr;checked(qar_cancel_token_create_with_timeout(&operation,timeout_ms),"create SDK timeout");
  }
  std::size_t target_count()const{return target=="both"?2:1;}
  void initialize(){
    gui();host_context=wglGetCurrentContext();const auto dc=wglGetCurrentDC();
    if(!host_context||!dc)throw std::runtime_error("Make the Slicer render window current before creating the host bridge");
    if(target!="visualizer"&&target!="quest"&&target!="headset"&&target!="both")
      throw std::runtime_error("Host target must be visualizer, headset or both");
#if !QARVTK_HOST_ONBOARDING_API
    if(target=="both")throw std::runtime_error("Visualizer + headset requires the installed QAROS Hub SDK (dev.2927)");
#endif
    const auto sdk=std::filesystem::u8path(sdk_root);
    if(!std::filesystem::is_regular_file(sdk/"bin"/"qar-streaming-c.dll"))throw std::runtime_error("Missing complete QAROS session runtime");
    std::filesystem::create_directories(std::filesystem::u8path(log_folder));
    log.open(std::filesystem::u8path(log_folder)/"host-bridge.log",std::ios::app);
    if(!log)throw std::runtime_error("Cannot open host bridge log");
    log<<"Host build: installed-hub-update-r8.4 (ABI 5); SDK header " QARVTK_HOST_SDK_HASH<<std::endl;
    log<<"SDK root: "<<sdk_root<<std::endl;
    log<<"Target mode: "<<target<<"; workspaces="<<volumes.size()<<std::endl;
    copying=std::make_unique<qarvtk::HostSharedWgl>(dc,host_context);
    {
      qarvtk::WglBinding binding(*copying);qarvtk::hostgl::load();adapter=device();
      copier=std::make_unique<qarvtk::GlCopy>();compositor=std::make_unique<qarvtk::GlComposite>();
      log<<"Host GPU: "<<glGetString(GL_VENDOR)<<" / "<<glGetString(GL_RENDERER)<<" / "<<glGetString(GL_VERSION)<<std::endl;
    }
    // SDK senders may invoke graphics callbacks concurrently. Give each its
    // own context, all sharing Slicer's objects, instead of competing for one.
    // Reserve the bounded target slots on the GUI thread, including a headset
    // that may join later. No SDK worker creates or destroys Windows windows.
    for(auto& volume:volumes)for(std::size_t t=0;t<target_count();++t){
      volume.graphics[t]=std::make_unique<qarvtk::HostSharedWgl>(dc,host_context);
      volume.callbacks[t]=std::make_unique<qarvtk::WglCallbacks>(volume.graphics[t]->dc,volume.graphics[t]->context);
      const auto probe=volume.callbacks[t]->probe_on_worker();
      if(!probe.get_string_i||!probe.memory_import)throw std::runtime_error("Shared SDK context lacks required OpenGL interop functions");
    }
    panels=std::make_unique<qarvtk::HostPanels>([this](const std::string& text){note(text);});
    worker=std::thread([this]{run();});
  }
  void run() noexcept;
  void copy(uint64_t serial,unsigned eye,qarvtk::RenderedEye source,bool depth,unsigned vc=0,unsigned vd=0){
    gui();
    if(wglGetCurrentContext()!=host_context)throw std::runtime_error("Slicer changed the host OpenGL context; restart streaming");
    std::lock_guard<std::mutex> lock(mutex);
    if(stopping)throw std::runtime_error("Invalid or expired host frame lease");
    lease.check_plane(serial,eye,depth);
    if(!source.width||!source.height||source.width>request.eyes[eye].width||source.height>request.eyes[eye].height)
      throw std::runtime_error("Host render size must be positive and no larger than the requested eye");
    auto stamp=Clock::now();
    glFinish(); // Complete host writes before the sharing context reads them.
    timings.host_sync+=milliseconds(stamp);
    qarvtk::WglBinding binding(*copying);
    if((vc==0)!=(vd==0))throw std::runtime_error("Incomplete volume depth textures");
    if(depth&&vc){
      stamp=Clock::now();
      if(!glIsTexture(vc)||!glIsTexture(vd))throw std::runtime_error("Volume textures are not shared");
      source=compositor->combine(source,vc,vd);
      timings.composite+=milliseconds(stamp);
    }
    stamp=Clock::now();
    copier->copy_plane(source,frame,views[eye],depth);
    timings.copy+=milliseconds(stamp);stamp=Clock::now();
    copier->finish(); // Borrowed host textures may be overwritten immediately.
    timings.copy_finish+=milliseconds(stamp);
    lease.complete_plane(serial,eye,depth);
  }
};

namespace {
// One worker pumps frames; another negotiates senders. A blocked later sender
// must never prevent a ready earlier sender from delivering its first frame.
// SDK graphics callbacks use per-sender contexts; no VTK runs on either worker.
struct Session {
  struct AppVolume {QarAppVolumeId id=qar_app_volume_id_default();bool created{};};
  struct Stream {
    QarRenderSender* sender{};
    uint64_t sent{};
    Clock::time_point retry_after{};
    qarvtk::HostStreamStartup::State reported=qarvtk::HostStreamStartup::State::pending;
  };
  struct Target {
    QarPeerId peer=qar_peer_id_default();
    std::string name,discovery_error;
    std::unique_ptr<qarvtk::HostStreamStartup> startup;
    bool panels_added{};
  };
  QarvtkHost& host;
  bool loaded{},initialized{};
  QarRuntime* runtime{};QarSession* session{};
  QarCancelToken* subscription{};
  qarvtk::HostStreamRoutes routes;
  std::vector<AppVolume> volumes;
  std::vector<Stream> streams;
  std::array<Target,2> targets;
  std::vector<QarPeerId> panel_peers;
  Clock::time_point waiting_since=Clock::now();
#if QARVTK_HOST_ONBOARDING_API
  QarPeerId source_peer=qar_peer_id_default();
  qarvtk::HostTargetRegistry target_registry;
  std::string peer_inventory;
#endif
  bool panels_started{};
  qarvtk::LegacyPeerObserver peers;
  explicit Session(QarvtkHost& h):host(h),routes(h.volumes.size(),h.target_count()),
    volumes(h.volumes.size()),streams(routes.size()){}
  ~Session(){
    for(auto& target:targets)if(target.startup)target.startup->request_stop();
    {std::lock_guard<std::mutex> lock(host.cancel_mutex);
     for(auto* token:host.connection_operations)if(token)qar_cancel_token_cancel(token);}
    for(auto& target:targets)if(target.startup)target.startup->join();
    // Panel video uses this session concurrently, but never outlives it.
    if(host.panels){host.panels->request_stop();host.panels->join();}
    if(subscription)qar_cancel_token_cancel(subscription);
    for(auto& stream:streams)if(stream.sender)qar_render_stream_handle_destroy(stream.sender);
    // A workspace is shared by the target senders. Close it only after every
    // sender is destroyed, and exactly once.
    for(auto& volume:volumes)if(volume.created)qar_app_volumes_close_volume(session,&volume.id);
#if QARVTK_HOST_ONBOARDING_API
    if(session)qar_session_handle_destroy(session);
#else
    if(session)qar_session_destroy(session);
#endif
    // We own this client handle, not the shared Hub/runtime or its targets.
    // Use the handle API, which is also the only form in dev.3008.
    if(runtime)qar_runtime_handle_destroy(runtime);
    if(subscription)qar_cancel_token_handle_destroy(subscription);
    {std::lock_guard<std::mutex> lock(host.cancel_mutex);if(host.operation)qar_cancel_token_handle_destroy(host.operation);host.operation=nullptr;}
    if(initialized)qar_library_destroy();
    if(loaded)qar_library_unload();
  }
  void open(){
    const auto library=(std::filesystem::u8path(host.sdk_root)/"bin"/"qar-streaming-c.dll").u8string();
    if(const auto existing=GetModuleHandleW(L"qar-streaming-c.dll")){
      wchar_t path[32768]{};
      const auto length=GetModuleFileNameW(existing,path,32768);
      std::error_code error;
      if(!length||length>=32768||!std::filesystem::equivalent(std::filesystem::path(path),std::filesystem::u8path(library),error)||error)
        throw std::runtime_error("A different QAROS runtime is already loaded in Slicer. Close Slicer completely, then reopen QAROS Live Renderer with the installed SDK.");
    }
    if(!qar_library_load(library.c_str()))throw std::runtime_error("Cannot load matching QAROS session runtime");
    loaded=true;auto init=qar_library_init_default();init.enable_console_logging=false;init.log_folder_path=host.log_folder.c_str();
    checked(qar_library_init(&init),"initialize SDK");initialized=true;
    const auto folder=(std::filesystem::u8path(host.sdk_root)/"bin").u8string();
    auto ri=qar_runtime_init_default();
#if !QARVTK_HOST_ONBOARDING_API
    ri.runtime_binaries_folder_path=folder.c_str();
#endif
    checked(qar_runtime_create(&ri,&runtime),"create SDK runtime");
    if(!runtime)throw std::runtime_error("SDK returned a null runtime");
#if QARVTK_HOST_ONBOARDING_API
    QarRuntimeInfo version{};checked(qar_runtime_info(&version),"read installed runtime version");
    host.note(std::string("QAROS runtime: ")+(version.version?version.version:"unknown"));
    auto local=qar_onboard_local_app_ext_default();local.approval_timeout_ms=120000;
    local.header.next=&host.adapter.header;
    auto onboard=qar_onboard_init_default();onboard.header.next=&local.header;
    onboard.presentation.display_name="Slicer live renderer";
    QarOnboardingId identity{}; // Local app enrollment is stateless; this remains zero.
    host.deadline(125000);
    host.set_status("Joining installed QAROS Hub. Approve Slicer in QAROS if prompted.");
    const auto progress=[](QarActionSeverity,float,const char* message,void* context) noexcept {
      if(message&&*message)try{static_cast<QarvtkHost*>(context)->set_status(message);}catch(...){}
    };
    checked(qar_runtime_onboard(runtime,&onboard,progress,&host,host.operation,&identity,&session),"join installed QAROS Hub");
    if(!session)throw std::runtime_error("SDK returned a null session");
    auto adapter=qar_graphics_device_id_default();bool present=false;
    checked(qar_session_get_graphics_device_id(session,&adapter,&present),"read session GPU");
    if(!present||adapter.id_type!=QAR_GPU_DEVICE_ID_TYPE_LUID||
       std::memcmp(&adapter.luid,&host.adapter.luid,sizeof(adapter.luid))!=0)
      throw std::runtime_error("QAROS Hub and Slicer must use the same GPU. Select the Slicer GPU for this session and restart streaming.");
    QarPeerSpec* raw{};checked(qar_session_get_my_spec(session,&raw),"read Slicer peer");
    std::unique_ptr<QarPeerSpec,decltype(&qar_peer_spec_handle_destroy)> own(raw,qar_peer_spec_handle_destroy);
    if(!own)throw std::runtime_error("SDK returned a null Slicer peer");
    checked(qar_peer_spec_get_id(own.get(),&source_peer),"read Slicer peer ID");
#else
    auto create=qar_session_create_init_default();create.header.next=&host.adapter.header;
    QarSessionInvite* raw{};checked(qar_runtime_create_session(runtime,&create,&raw),"create session");
    std::unique_ptr<QarSessionInvite,decltype(&qar_session_invite_handle_destroy)> invite(raw,qar_session_invite_handle_destroy);
    std::vector<uint8_t> bytes(1024*1024);size_t size{};
    checked(qar_session_invite_serialize(invite.get(),bytes.data(),bytes.size(),&size),"serialize local session");
    if(!size||size>bytes.size())throw std::runtime_error("Invalid session invite size");
    auto join=qar_session_join_init_default();join.header.next=&host.adapter.header;
    join.invite_data=bytes.data();join.invite_data_size=size;join.peer_spec_init.display_name="Slicer live renderer";
    checked(qar_session_join(&join,&session),"join session");
    if(!session)throw std::runtime_error("SDK returned a null session");
#endif
  }
#if QARVTK_HOST_ONBOARDING_API
  static qarvtk::HostTarget read_target(QarPeerSpec* spec){
    qarvtk::HostTarget peer;char name[256]{},device[256]{};
    checked(qar_peer_spec_get_id(spec,&peer.id),"read target ID");
    checked(qar_peer_spec_get_role(spec,&peer.role),"read peer role");
    checked(qar_peer_spec_get_category(spec,&peer.category),"read peer category");
#if QARVTK_HOST_PEER_PRESENCE
    checked(qar_peer_spec_get_presence(spec,&peer.presence),"read peer presence");
#else
    checked(qar_peer_spec_get_app_state(spec,&peer.state),"read peer state");
#endif
    checked(qar_peer_spec_get_display_name(spec,name,sizeof(name)),"read target name");peer.name=name;
    if(qar_result_is_success(qar_peer_spec_get_device_name(spec,device,sizeof(device))))peer.device=device;
    return peer;
  }
  void discover(){
    checked(qar_cancel_token_create(&subscription),"create target subscription");
    const auto update=[](QarPeerSpec* spec,void* context)noexcept{
      if(!spec||!context)return;
      auto& registry=*static_cast<qarvtk::HostTargetRegistry*>(context);
      try{registry.record(read_target(spec));}
      catch(const std::exception& e){try{registry.fail(e.what());}catch(...){}}
      catch(...){try{registry.fail("Cannot read QAROS target update");}catch(...){}}
    };
    // Subscribe first, then seed the inbox. A headset joining after Start is
    // offered by the same path as one already connected to the Hub.
    checked(qar_peer_subscribe_updates(session,update,&target_registry,subscription),"subscribe QAROS targets");
    while(!host.stopping){
      size_t count{};checked(qar_query_peer_specs_count(session,&count),"count QAROS peers");
      if(count>4096)throw std::runtime_error("Too many QAROS peers");
      struct Snapshot {
        std::vector<QarPeerSpec*> handles;
        ~Snapshot(){for(auto* p:handles)if(p)qar_peer_spec_handle_destroy(p);}
      } snapshot{std::vector<QarPeerSpec*>(count+16,nullptr)};
      size_t written{};
      checked(qar_query_peer_specs(session,snapshot.handles.data(),snapshot.handles.size(),&written),"list QAROS peers");
      if(written>snapshot.handles.size())continue; // A growing list is retried with a fresh count.
      for(size_t i=0;i<written;++i){
        auto* spec=snapshot.handles[i];if(!spec)continue;
        target_registry.seed(read_target(spec));
      }
      return;
    }
  }
#else
  QarPeerId viewer(){
    checked(qar_cancel_token_create(&subscription),"create peer subscription");
    checked(qar_peer_subscribe_updates(session,on_peer,&peers,subscription),"subscribe peers");
    char connection[1024]{};
    if(host.target=="visualizer")checked(qar_session_invite_connection_string_visualizer(connection,sizeof(connection)),"visualizer address");
    else checked(qar_session_invite_connection_string_quest(connection,sizeof(connection)),"Quest address");
    auto init=qar_session_invite_peer_init_default();init.connection_string=connection;
    QarPeerId peer=qar_peer_id_default();host.deadline();host.set_status("Inviting "+host.target);
    checked(qar_session_invite_peer(session,&init,&peer,host.operation),"invite viewer");
    const auto end=Clock::now()+std::chrono::seconds(60);
    while(!host.stopping&&Clock::now()<end){
      if(const auto state=peers.state(peer)){
        if(*state==QAR_APP_STATE_SHUTTING_DOWN)throw std::runtime_error("Viewer is shutting down");
        host.set_status("Viewer observed; allowing receiver subscriptions to settle");
        if(!host.pause(std::chrono::seconds(2)))throw std::runtime_error("Live stream stopped");
        if(peers.state(peer)==QAR_APP_STATE_SHUTTING_DOWN)throw std::runtime_error("Viewer closed during startup");
        return peer;
      }
      host.pause(std::chrono::milliseconds(20));
    }
    throw std::runtime_error(host.stopping?"Live stream stopped":"No viewer peer update before timeout");
  }
#endif
  std::string target_label(std::size_t slot)const{
    return (host.target=="visualizer"||(host.target=="both"&&slot==0))?"Visualizer":"Headset";
  }
  void start(){
#if QARVTK_HOST_ONBOARDING_API
    discover();
    const QarPeerId* initial=nullptr; // Each sender attaches its target, as in the SDK example.
#else
    const auto peer=viewer();const QarPeerId* initial=&peer;
#endif
    // Announce the full workspace set before negotiating any sender, so the
    // receiver need not discover another volume halfway through stream setup.
    for(std::size_t i=0;i<volumes.size()&&!host.stopping;++i)add_volume(initial,i);
    if(host.stopping)return;
    waiting_since=Clock::now();
#if !QARVTK_HOST_ONBOARDING_API
    start_target(0,peer,target_label(0));
#endif
    host.set_status(progress());
  }
  void start_target(std::size_t slot,const QarPeerId& peer,const std::string& name){
    auto& target=targets[slot];target.peer=peer;target.name=name;target.discovery_error.clear();
    host.note("TARGET_SELECTED type="+target_label(slot)+" peer="+id_text(peer)+" name="+name);
    target.startup=std::make_unique<qarvtk::HostStreamStartup>(volumes.size());
    target.startup->start([this,slot](std::size_t i){start_stream(slot,i);},[this]{return host.stopping.load();});
  }
  void add_volume(const QarPeerId* peer,std::size_t index){
    auto& volume=volumes[index];const auto& config=host.volumes[index];
    const QarPeerId* initial[]={peer};auto app=qar_app_volume_init_default();
    app.display_name=config.name.c_str();
    qarvtk::apply_host_placement(app,config.placement);
    app.initial_peers=peer?initial:nullptr;app.initial_peer_count=peer?1:0;
#if QARVTK_HOST_ONBOARDING_API
    const auto common=qarvtk::host_resource_name(source_peer,"volume",index);app.common_name=common.c_str();
    checked(qar_app_volumes_get_or_create(session,&app,&volume.id),"create live app volume");
#else
    checked(qar_app_volumes_add_volume(session,&app,&volume.id),"create live app volume");
#endif
    volume.created=true;
    for(std::size_t i=0;i<index;++i)if(qar_app_volume_id_equals(&volume.id,&volumes[i].id))
      throw std::runtime_error("SDK returned a duplicate app volume ID");
    {
      std::ostringstream line;line<<"Volume "<<index<<" ("<<config.name<<") id="<<id_text(volume.id)
        <<" workspace metres XYZ="<<app.size.width_meters<<','
        <<app.size.height_meters<<','<<app.size.length_meters<<"; room centre="
        <<app.pose.position.x<<','<<app.pose.position.y<<','<<app.pose.position.z;
      host.note(line.str());
    }
  }
  void start_stream(std::size_t slot,std::size_t volume){
    const auto index=routes.index(slot,volume);
    auto& stream=streams[index];const auto& config=host.volumes[volume];const auto& target=targets[slot];
    // This token belongs only to this blocking connection attempt. GUI Stop
    // and Session teardown can cancel it without touching a running sender.
    struct Operation {
      QarvtkHost& host;std::size_t slot;QarCancelToken* token{};
      Operation(QarvtkHost& h,std::size_t s,const qarvtk::HostStreamStartup& startup):host(h),slot(s){
        std::lock_guard<std::mutex> lock(host.cancel_mutex);
        if(host.stopping||startup.stopping())throw std::runtime_error("Live stream stopped");
        checked(qar_cancel_token_create_with_timeout(&token,60000),"create sender timeout");
        host.connection_operations[slot]=token;
      }
      ~Operation(){
        std::lock_guard<std::mutex> lock(host.cancel_mutex);
        host.connection_operations[slot]=nullptr;if(token)qar_cancel_token_handle_destroy(token);
      }
    } operation(host,slot,*target.startup);
    auto gl=config.callbacks[slot]->parameters();
    auto init=qar_render_sender_init_default();init.graphics_api=QAR_GRAPHICS_API_OPENGL;init.header.next=&gl.header;
    init.peer_id=target.peer;init.app_volume_id=&volumes[volume].id;init.enable_auto_reconnects=true;
    init.texture_layout=QAR_FRAME_LAYOUT_SEPARATED_TEXTURES;
    init.color_format=QAR_PIXEL_FORMAT_R8G8B8A8;init.depth_format=QAR_PIXEL_FORMAT_D32_FLOAT;init.frame_views_count=4;
    for(unsigned i=0;i<4;++i){init.frame_views[i]=qar_render_frame_view_default();
      init.frame_views[i].eye=i<2?QAR_VIDEO_FRAME_VIEW_EYE_LEFT:QAR_VIDEO_FRAME_VIEW_EYE_RIGHT;
      init.frame_views[i].data_type=i%2?QAR_VIDEO_FRAME_VIEW_TYPE_DEPTH:QAR_VIDEO_FRAME_VIEW_TYPE_COLOR;init.frame_views[i].texture_index=i;}
    host.note("SENDER_CONNECT index="+std::to_string(index)+" target="+target_label(slot)+
      " peer="+id_text(target.peer)+" volume="+std::to_string(volume)+" app_volume="+id_text(volumes[volume].id)+" name="+config.name);
    QarRenderSender* raw{};
    const auto result=qar_render_sender_create(session,&init,operation.token,&raw);
    std::unique_ptr<QarRenderSender,decltype(&qar_render_stream_handle_destroy)> sender(raw,qar_render_stream_handle_destroy);
    checked(result,"create live sender");
    if(!sender)throw std::runtime_error("SDK returned a null sender");
    auto current=qar_video_frame_layout_default();
    checked(qar_render_sender_layout(sender.get(),&current),"read negotiated layout");
    const auto reduced=qarvtk::half_depth_layout(current);
    checked(qar_render_sender_change_layout(sender.get(),&reduced),"set half-resolution depth layout");
    stream.sender=sender.release(); // Published by HostStreamStartup's ready handoff.
    host.note("Sender "+std::to_string(index)+" ready; half-resolution depth layout set");
  }
  bool ready(std::size_t index)const{
    const auto route=routes.address(index);const auto& startup=targets[route.target].startup;
    return startup&&startup->ready(route.volume);
  }
  std::size_t target_ready_count(std::size_t slot)const{
    std::size_t count=0;
    if(targets[slot].startup)for(std::size_t v=0;v<volumes.size();++v)if(targets[slot].startup->ready(v))++count;
    return count;
  }
  std::size_t ready_count() const {
    std::size_t count=0;for(std::size_t t=0;t<host.target_count();++t)count+=target_ready_count(t);return count;
  }
  std::string progress() const {
    std::string text;
    for(std::size_t t=0;t<host.target_count();++t){
      const auto& target=targets[t];if(t)text+=" | ";text+=target_label(t)+": ";
      if(!target.startup){
        text+=target.discovery_error.empty()?"waiting for an available target in QAROS":target.discovery_error;
        continue;
      }
      uint64_t frames=0;for(std::size_t v=0;v<volumes.size();++v)frames+=streams[routes.index(t,v)].sent;
      text+=target.name+"; "+std::to_string(target_ready_count(t))+"/"+std::to_string(volumes.size())+" volumes; "+std::to_string(frames)+" frames";
      for(std::size_t v=0;v<volumes.size();++v)
        if(target.startup->state(v)==qarvtk::HostStreamStartup::State::failed)
          text+="; FAILED "+host.volumes[v].name+": "+target.startup->error(v).substr(0,220);
    }
    return text;
  }
  void refresh(){
    bool changed=false;
#if QARVTK_HOST_ONBOARDING_API
    const auto available=target_registry.snapshot();
    // Record discovery changes once, including why a headset is filtered out.
    // Callbacks still only copy metadata; no logging or SDK work on that thread.
    std::string inventory;
    for(const auto& peer:available)if(peer.role==QAR_PEER_ROLE_TARGET || peer.category==QAR_DEVICE_CATEGORY_HEADSET){
      inventory+="PEER_OBSERVED peer="+id_text(peer.id)+" role="+std::to_string(int(peer.role))+
        " category="+std::to_string(int(peer.category))+
#if QARVTK_HOST_PEER_PRESENCE
        " presence="+qarvtk::host_peer_presence_name(peer.presence)+
#else
        " state="+qarvtk::host_peer_state_name(peer.state)+
#endif
        " eligible="+(qarvtk::host_target_available(peer)?"yes":"no")+" name="+peer.name+" device="+peer.device+"\n";
    }
    if(inventory.empty())inventory="PEER_DISCOVERY no target peers in this Hub session\n";
    if(inventory!=peer_inventory){host.note(inventory);peer_inventory=inventory;}
    for(std::size_t t=0;t<host.target_count()&&!host.stopping;++t){
      auto& target=targets[t];if(target.startup)continue; // Pin a selected peer until Stop; never silently switch viewers.
      const auto category=target_label(t)=="Visualizer"?QAR_DEVICE_CATEGORY_VISUALIZER:QAR_DEVICE_CATEGORY_HEADSET;
      std::optional<qarvtk::HostTarget> selected;
      try{selected=qarvtk::select_host_target(available,category);}
      catch(const std::runtime_error& e){
        if(target.discovery_error!=e.what()){
          target.discovery_error=e.what();changed=true;
          host.note("TARGET_WAIT type="+target_label(t)+" reason="+target.discovery_error);
        }
        continue;
      }
      if(selected){
        start_target(t,selected->id,selected->name+(selected->device.empty()?"":" ("+selected->device+")"));changed=true;
      }else{
        const std::string reason=target_label(t)=="Headset"?
          "No available headset target in this QAROS session; connect the headset to the same Hub":
          "No available Visualizer receiver in this QAROS session; create a receiver in QAROS";
        if(target.discovery_error!=reason){target.discovery_error=reason;changed=true;
          host.note("TARGET_WAIT type="+target_label(t)+" reason="+reason);}
      }
    }
#endif
    for(std::size_t i=0;i<streams.size();++i){
      const auto route=routes.address(i);const auto& startup=targets[route.target].startup;
      if(!startup)continue;
      const auto state=startup->state(route.volume);
      if(state==streams[i].reported)continue;
      streams[i].reported=state;changed=true;
      if(state==qarvtk::HostStreamStartup::State::failed)
        host.note("WORKSPACE_FAILED index="+std::to_string(i)+" target="+target_label(route.target)+
          " peer="+id_text(targets[route.target].peer)+" name="+host.volumes[route.volume].name+" error="+startup->error(route.volume));
    }
    if(changed)host.set_status(progress());
    bool all_done=true;
    for(std::size_t t=0;t<host.target_count();++t){
      auto& target=targets[t];
      if(!target.startup||!target.startup->done()){all_done=false;continue;}
      if(!target.panels_added&&target_ready_count(t)&&!host.stopping){
        panel_peers.push_back(target.peer);target.panels_added=true;
        if(!panels_started){host.panels->start(session,panel_peers);panels_started=true;}
        else host.panels->add_peers(panel_peers);
      }
    }
    if(!host.stopping&&!ready_count()&&(all_done||Clock::now()-waiting_since>std::chrono::seconds(120)))
      throw std::runtime_error("No usable rendering sender. "+progress());
  }
};
}

void QarvtkHost::run() noexcept {
  try {
    Session sdk(*this);sdk.open();if(!stopping)sdk.start();
    auto last=Clock::now(),cycle_start=last,window_start=last;
    FrameTimings totals;
    double wait_ms=0,metadata_ms=0,gui_ms=0,show_ms=0;
    uint64_t measured=0;
    qarvtk::HostStreamScheduler scheduler;
    auto report=[&](bool force){
      const double elapsed=milliseconds(window_start);
      if(!measured||(!force&&elapsed<5000))return;
      const double count=static_cast<double>(measured);
      std::ostringstream line;line<<std::fixed<<std::setprecision(2)
        <<"PERF_NATIVE frames="<<measured<<" submitted_fps="<<count*1000/elapsed
        <<" eye_size="<<request.eyes[0].width<<'x'<<request.eyes[0].height
        <<" depth_size="<<request.eyes[0].depth_width<<'x'<<request.eyes[0].depth_height
        <<" sdk_wait_ms="<<wait_ms/count<<" metadata_ms="<<metadata_ms/count
        <<" gui_wait_ms="<<gui_ms/count<<" sdk_show_ms="<<show_ms/count
        <<" host_sync_ms="<<totals.host_sync/count<<" composite_cpu_ms="<<totals.composite/count
        <<" copy_cpu_ms="<<totals.copy/count<<" copy_finish_ms="<<totals.copy_finish/count
        <<" sender_frames=";
      for(std::size_t i=0;i<sdk.streams.size();++i)line<<(i?",":"")<<i<<':'<<sdk.streams[i].sent;
      note(line.str());totals={};wait_ms=metadata_ms=gui_ms=show_ms=0;measured=0;window_start=Clock::now();
    };
    while(!stopping){
      sdk.refresh();
      if(!sdk.ready_count())last=Clock::now(); // Pose deadline starts when a sender is usable.
      else if(Clock::now()-last>std::chrono::seconds(60))throw std::runtime_error("No viewer frame/pose for 60 seconds");
      std::unique_ptr<QarRenderFrameInfo,decltype(&qar_render_frame_info_handle_destroy)> info(nullptr,qar_render_frame_info_handle_destroy);
      const auto selected=scheduler.select(sdk.streams.size(),[&](std::size_t index){
        auto& stream=sdk.streams[index];
        if(stopping||!sdk.ready(index)||Clock::now()<stream.retry_after)return false;
        QarRenderFrameInfo* raw{};bool ready=false;
        const auto result=qar_render_sender_try_begin_frame(stream.sender,&raw,&ready);
        info.reset(raw);
        if(qarvtk::legacy_begin_frame_wait(result,stream.sent==0)!=qarvtk::LegacyFrameWait::none){
          stream.retry_after=Clock::now()+std::chrono::milliseconds(100);info.reset();return false;
        }
        if(qar_result_has_code(result,QAR_STATUS_MEDIA_STREAMING_STREAM_IS_RECONNECTING)||
           qar_result_has_code(result,QAR_STATUS_MEDIA_STREAMING_NO_ACTIVE_TRANSFERS)){
          stream.retry_after=Clock::now()+std::chrono::milliseconds(2);info.reset();return false;
        }
        checked(result,"begin live frame");
        if(!ready){info.reset();return false;}
        if(!info)throw std::runtime_error("Ready frame has no metadata");
        return true;
      });
      if(!selected){pause(std::chrono::milliseconds(2));continue;}
      auto& stream=sdk.streams[*selected];
      const auto route=sdk.routes.address(*selected);
      const auto& placement=volumes[route.volume].placement;
      const auto ready_at=Clock::now();
      auto access=qar_video_frame_opengl_default();checked(qar_render_sender_frame_opengl(stream.sender,&access),"access frame textures");
      auto next=layout(access);const auto next_views=qarvtk::validate_stereo(next);
      QarvtkHostFrame next_request{};next_request.serial=sent+1;
      next_request.volume_index=static_cast<uint32_t>(route.volume);
      next_request.near_plane=static_cast<float>(.05/placement.app_scale);
      next_request.far_plane=static_cast<float>(20./placement.app_scale);
      for(unsigned i=0;i<2;++i){
        const auto& view=next.views[next_views[i].color];auto& eye=next_request.eyes[i];
        QarPose pose=qar_pose_default();QarFov fov=qar_fov_default();
        checked(qar_render_frame_info_get_view_pose(info.get(),next_views[i].color,&pose),"eye pose");
        checked(qar_render_frame_info_get_view_fov(info.get(),next_views[i].color,&fov),"eye FOV");
        eye.position[0]=pose.position.x;eye.position[1]=pose.position.y;eye.position[2]=pose.position.z;
        eye.orientation[0]=pose.orientation.x;eye.orientation[1]=pose.orientation.y;eye.orientation[2]=pose.orientation.z;eye.orientation[3]=pose.orientation.w;
        eye.fov[0]=fov.angle_left;eye.fov[1]=fov.angle_right;eye.fov[2]=fov.angle_up;eye.fov[3]=fov.angle_down;
        eye.width=qarvtk::width(view);eye.height=qarvtk::height(view);
        const auto& depth=next.views[next_views[i].depth];
        eye.depth_width=qarvtk::width(depth);eye.depth_height=qarvtk::height(depth);
        if(eye.depth_width!=(eye.width+1)/2||eye.depth_height!=(eye.height+1)/2)
          throw std::runtime_error("SDK did not retain the requested half-resolution depth layout");
      }
      const auto posted_at=Clock::now();
      FrameTimings copied;
      {
        std::unique_lock<std::mutex> lock(mutex);
        frame=std::move(next);views=next_views;request=next_request;timings={};lease.post(request.serial);
        condition.wait(lock,[&]{return lease.submitted()||(stopping&&!lease.acquired());});
        if(stopping)break;
        copied=timings;lease.consume();
      }
      const auto show_at=Clock::now();
      auto show=qar_render_frame_show_default();
      show.rendered_near_far={static_cast<float>(request.near_plane),static_cast<float>(request.far_plane)};show.depth_scale=1;
      checked(qar_render_sender_show_frame(stream.sender,&show),"submit live stereo frame");
      ++sent;++stream.sent;last=Clock::now();
      if(stream.sent==1)note("FIRST_FRAME sender="+std::to_string(*selected)+" target="+sdk.target_label(route.target)+
        " peer="+id_text(sdk.targets[route.target].peer)+" volume="+std::to_string(route.volume)+" name="+volumes[route.volume].name);
      totals+=copied;++measured;wait_ms+=milliseconds(cycle_start,ready_at);
      metadata_ms+=milliseconds(ready_at,posted_at);gui_ms+=milliseconds(posted_at,show_at);
      show_ms+=milliseconds(show_at,last);cycle_start=last;report(false);
      if(sent==1||sent%120==0)set_status(sdk.progress()+"; submitted "+std::to_string(sent)+" stereo frames total");
    }
    report(true);
  } catch(const std::exception& e){if(!stopping)fail(e.what());}
    catch(...){if(!stopping)fail("Unknown native host transport failure");}
  // Session destruction (and all SDK workers) must finish before destroy is allowed.
  note("SDK worker exited; submitted total="+std::to_string(sent));
  done=true;condition.notify_all();
}

extern "C" {
const char* qarvtk_host_info(){
  return "{\"host_abi\":5,\"modern_api\":0,\"panel_probe\":0,\"panel_size_on_start\":1,\"panel_image_region\":1,\"target_discovery\":"
#if QARVTK_HOST_PEER_PRESENCE
    "3"
#else
    "2"
#endif
    ",\"runtime_handle_only\":1,\"multi_target\":"
#if QARVTK_HOST_ONBOARDING_API
    "1"
#else
    "0"
#endif
    ",\"header_sha256\":\"" QARVTK_HOST_SDK_HASH "\",\"eye_size\":104,\"frame_size\":240,\"placement_size\":112,\"volume_size\":120}";
}
int qarvtk_host_create(const char* sdk,const char* target,const char* folder,const QarvtkHostPlacement* placement,
                      QarvtkHost** result,char* error,uint32_t size){
  QarvtkHostVolume volume{};if(placement)volume.placement=*placement;volume.name="Slicer live renderer";
  return qarvtk_host_create_volumes(sdk,target,folder,placement?&volume:nullptr,1,result,error,size);
}
int qarvtk_host_create_volumes(const char* sdk,const char* target,const char* folder,
    const QarvtkHostVolume* volumes,uint32_t count,QarvtkHost** result,char* error,uint32_t size){
  if(result)*result=nullptr;
  bool expected=false;
  if(!active.compare_exchange_strong(expected,true)){message(error,size,"Only one live host transport may run in this Slicer process");return -1;}
  try {
    if(!sdk||!target||!folder||!volumes||!count||count>32||!result)throw std::invalid_argument("Expected 1 to 32 host workspaces");
    auto host=std::make_unique<QarvtkHost>();host->sdk_root=sdk;host->target=target;host->log_folder=folder;
    for(uint32_t i=0;i<count;++i){
      auto probe=qar_app_volume_init_default();qarvtk::apply_host_placement(probe,volumes[i].placement);
      QarvtkHost::Volume volume;volume.placement=volumes[i].placement;
      volume.name=volumes[i].name&&*volumes[i].name?volumes[i].name:"Slicer volume "+std::to_string(i+1);
      host->volumes.push_back(std::move(volume));
    }
    host->initialize();
    *result=host.release();return 0;
  } catch(const std::exception& e){message(error,size,e.what());active=false;return -1;}
    catch(...){message(error,size,"Cannot initialize host transport");active=false;return -1;}
}
int qarvtk_host_poll(QarvtkHost* h,QarvtkHostFrame* out){
  if(!h||!out)return -1;
  try {
    h->gui();std::lock_guard<std::mutex> lock(h->mutex);
    if(h->failed)return -1;if(h->done)return 2;
    if(h->stopping||!h->lease.acquire())return 0;
    *out=h->request;return 1;
  } catch(const std::exception& e){h->fail(e.what());return -1;}
}
int qarvtk_host_color(QarvtkHost* h,uint64_t serial,uint32_t eye,uint32_t color,uint32_t depth,uint32_t width,uint32_t height){
  if(!h)return -1;try{h->copy(serial,eye,{color,depth,width,height},false);return 0;}
  catch(const std::exception& e){h->fail(e.what());return -1;}
}
int qarvtk_host_depth(QarvtkHost* h,uint64_t serial,uint32_t eye,uint32_t color,uint32_t depth,uint32_t vc,uint32_t vd,uint32_t width,uint32_t height){
  if(!h)return -1;try{h->copy(serial,eye,{color,depth,width,height},true,vc,vd);return 0;}
  catch(const std::exception& e){h->fail(e.what());return -1;}
}
int qarvtk_host_finish(QarvtkHost* h,uint64_t serial){
  if(!h)return -1;
  try {
    h->gui();std::lock_guard<std::mutex> lock(h->mutex);
    if(h->stopping)throw std::runtime_error("Incomplete host stereo frame");
    h->lease.finish(serial);h->condition.notify_all();return 0;
  } catch(const std::exception& e){h->fail(e.what());return -1;}
}
void qarvtk_host_stop(QarvtkHost* h){if(h)try{h->stop();}catch(const std::exception& e){h->fail(e.what());}}
int qarvtk_host_destroy(QarvtkHost* h){
  if(!h)return 1;
  if(GetCurrentThreadId()!=h->owner||!h->done)return 0;
  delete h;active=false;return 1;
}
void qarvtk_host_status(QarvtkHost* h,char* text,uint32_t size){
  if(!h){message(text,size,"Stopped");return;}
  std::lock_guard<std::mutex> lock(h->mutex);message(text,size,h->status);
}
int qarvtk_host_panel_create(QarvtkHost* h,const char* name,uint32_t w,uint32_t height,
    double x,double y,double z,double width_meters,QarvtkHostPanel** out,char* error,uint32_t size){
  if(out)*out=nullptr;
  try{
    if(!h||!out||!h->panels||h->stopping)throw std::invalid_argument("No active panel transport");
    *out=reinterpret_cast<QarvtkHostPanel*>(h->panels->create(name,w,height,x,y,z,width_meters));return 0;
  }catch(const std::exception& e){message(error,size,e.what());return -1;}
}
int qarvtk_host_panel_capture(QarvtkHost* h,QarvtkHostPanel* p,uint32_t color,uint32_t w,uint32_t height,char* error,uint32_t size){
  return qarvtk_host_panel_capture_region(h,p,color,w,height,0,0,w,height,error,size);
}
int qarvtk_host_panel_capture_region(QarvtkHost* h,QarvtkHostPanel* p,uint32_t color,uint32_t w,uint32_t height,
    uint32_t x,uint32_t y,uint32_t crop_width,uint32_t crop_height,char* error,uint32_t size){
  try{
    if(!h||!h->panels)throw std::invalid_argument("No active panel transport");
    if(h->stopping)return 0;
    const qarvtk::ColorRegion region{x,y,crop_width,crop_height};
    return h->panels->capture(reinterpret_cast<qarvtk::HostPanel*>(p),color,w,height,&region);
  }catch(const std::exception& e){message(error,size,e.what());return -1;}
}
int qarvtk_host_panel_visible(QarvtkHost* h,QarvtkHostPanel* p,uint32_t visible){
  try{if(!h||!h->panels)return -1;h->panels->visible(reinterpret_cast<qarvtk::HostPanel*>(p),visible!=0);return 0;}
  catch(...){return -1;}
}
void qarvtk_host_panel_close(QarvtkHost* h,QarvtkHostPanel* p){
  if(h&&h->panels)try{h->panels->close(reinterpret_cast<qarvtk::HostPanel*>(p));}catch(...){}
}
void qarvtk_host_panel_status(QarvtkHost* h,QarvtkHostPanel* p,char* text,uint32_t size){
  try{message(text,size,h&&h->panels?h->panels->status(reinterpret_cast<qarvtk::HostPanel*>(p)):"Stopped");}
  catch(const std::exception& e){message(text,size,e.what());}
}

}
static_assert(sizeof(QarvtkHostEye)==104&&sizeof(QarvtkHostFrame)==240&&sizeof(QarvtkHostPlacement)==112&&sizeof(QarvtkHostVolume)==120,"Unexpected host C ABI packing");
