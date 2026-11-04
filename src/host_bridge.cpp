#include "qarvtk/gl.hpp"
#include "qarvtk/host_bridge.h"
#include "qarvtk/host_frame_lease.hpp"
#include "qarvtk/host_render_queue.hpp"
#include "qarvtk/host_stream_scheduler.hpp"
#include "qarvtk/host_stream_routes.hpp"
#include "qarvtk/host_live_selection.hpp"
#include "qarvtk/host_target_retirement.hpp"
#include "qarvtk/host_stream_startup.hpp"
#include "qarvtk/host_panels.hpp"
#include "qarvtk/host_wgl.hpp"
#include "qarvtk/host_gpu_fence.hpp"
#include "qarvtk/host_frame_pacer.hpp"
#include "qarvtk/gl_copy.hpp"
#include "qarvtk/gl_composite.hpp"
#include "../apps/wgl_callbacks.hpp"
#include "../apps/legacy_peer_observer.hpp"
#include "../apps/legacy_frame_wait.hpp"
#include "../apps/host_config.hpp"
#if QARVTK_HOST_ONBOARDING_API
#include "../apps/host_hub.hpp"
#include "../apps/host_device_selection.hpp"
#endif
#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <cstdlib>
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
struct RenderJob {
  QarvtkHostFrame request{};
  qarvtk::Frame frame;
  std::array<qarvtk::EyeViews,2> views{};
  FrameTimings timings;
  std::shared_ptr<qarvtk::HostGpuFence> completion;
  bool gpu_completion_failed=false;
  Clock::time_point posted_at{},finished_at{};
};
using RenderQueue=qarvtk::HostRenderQueue<RenderJob>;
std::atomic<bool> active{false};
std::atomic<bool> sdk_active{false};
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

namespace {
std::string json_text(const std::string& value){
  std::ostringstream out;out<<'"';
  for(unsigned char ch:value){
    if(ch=='"'||ch=='\\')out<<'\\'<<char(ch);
    else if(ch<32)out<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<unsigned(ch)<<std::dec;
    else out<<char(ch);
  }
  out<<'"';return out.str();
}
}

struct HostHubState;
struct QarvtkHub {std::shared_ptr<HostHubState> state;DWORD owner=GetCurrentThreadId();};

#if QARVTK_HOST_DEVICE_SELECTION
// The device browser and all render senders share ONE enrollment. Its worker
// owns SDK initialization and teardown; GUI polling only copies metadata.
struct HostHubState {
  std::string sdk_root,folder,status="Connecting to QAROS";
  std::shared_ptr<qarvtk::HostTargetRegistry> registry=std::make_shared<qarvtk::HostTargetRegistry>();
  std::thread worker;
  std::mutex mutex,cancel_mutex;
  std::condition_variable condition;
  std::atomic<bool> stopping{false},done{false},ready{false};
  bool failed{};unsigned streams{};
  QarCancelToken* operation{};
  QarSession* session{};
  QarPeerId source_peer{};
  std::vector<QarAppVolumeId> workspaces;
  explicit HostHubState(std::string sdk,std::string logs):sdk_root(std::move(sdk)),folder(std::move(logs)){}
  ~HostHubState(){stop();if(worker.joinable())worker.join();}
  void stop(){
    {std::lock_guard<std::mutex> lock(mutex);stopping=true;ready=false;}
    std::lock_guard<std::mutex> lock(cancel_mutex);
    if(operation)qar_cancel_token_cancel(operation);
    condition.notify_all();
  }
  void retain(){
    std::lock_guard<std::mutex> lock(mutex);
    if(!ready||stopping||failed)throw std::runtime_error("Connect to QAROS before starting a stream");
    ++streams;
  }
  void release(){std::lock_guard<std::mutex> lock(mutex);if(streams)--streams;condition.notify_all();}
  void remember_workspace(const QarAppVolumeId& id){
    std::lock_guard<std::mutex> lock(mutex);
    for(const auto& known:workspaces)if(qar_app_volume_id_equals(&known,&id))return;
    workspaces.push_back(id);
  }
  void set_status(const std::string& value){std::lock_guard<std::mutex> lock(mutex);status=value;}
  static qarvtk::HostTarget read(QarPeerSpec* spec){
    qarvtk::HostTarget peer;char name[256]{},device_name[256]{};
    checked(qar_peer_spec_get_id(spec,&peer.id),"read receiver ID");
    checked(qar_peer_spec_get_role(spec,&peer.role),"read receiver role");
    checked(qar_peer_spec_get_category(spec,&peer.category),"read receiver category");
    checked(qar_peer_spec_get_presence(spec,&peer.presence),"read receiver presence");
    checked(qar_peer_spec_get_display_name(spec,name,sizeof(name)),"read receiver name");peer.name=name;
    if(qar_result_is_success(qar_peer_spec_get_device_name(spec,device_name,sizeof(device_name))))peer.device=device_name;
    return peer;
  }
  std::string snapshot(){
    std::lock_guard<std::mutex> lock(mutex);
    std::ostringstream out;out<<"{\"connected\":"<<(ready&&!stopping?"true":"false")
      <<",\"stopping\":"<<(stopping?"true":"false")<<",\"status\":"<<json_text(status)
      <<",\"app_tag\":"<<json_text(qarvtk::host_app_tag)<<",\"source_peer\":"<<json_text(id_text(source_peer))<<",\"peers\":[";
    bool first=true;
    for(const auto& peer:registry->snapshot()){
      if(peer.role!=QAR_PEER_ROLE_TARGET||
         (peer.category!=QAR_DEVICE_CATEGORY_VISUALIZER&&peer.category!=QAR_DEVICE_CATEGORY_HEADSET))continue;
      if(!first)out<<',';first=false;
      out<<"{\"id\":"<<json_text(id_text(peer.id))<<",\"name\":"<<json_text(peer.name)
        <<",\"device\":"<<json_text(peer.device)<<",\"category\":"
        <<json_text(peer.category==QAR_DEVICE_CATEGORY_HEADSET?"headset":"visualizer")
        <<",\"available\":"<<(ready&&!stopping&&qarvtk::host_target_available(peer)?"true":"false")
        <<",\"presence\":"<<json_text(qarvtk::host_peer_presence_name(peer.presence))<<'}';
    }
    out<<"]}";return out.str();
  }
  void run()noexcept{
    bool owned=false,loaded=false,initialized=false;
    QarRuntime* runtime{};QarCancelToken* subscription{};
    try{
      bool expected=false;
      if(!sdk_active.compare_exchange_strong(expected,true))throw std::runtime_error("A QAROS connection is already active in this Slicer process");
      owned=true;
      const auto library=(std::filesystem::u8path(sdk_root)/"bin"/"qar-streaming-c.dll").u8string();
      if(const auto existing=GetModuleHandleW(L"qar-streaming-c.dll")){
        wchar_t path[32768]{};const auto length=GetModuleFileNameW(existing,path,32768);std::error_code error;
        if(!length||length>=32768||!std::filesystem::equivalent(std::filesystem::path(path),std::filesystem::u8path(library),error)||error)
          throw std::runtime_error("A different QAROS runtime is loaded. Close Slicer completely before changing the SDK.");
      }
      std::filesystem::create_directories(std::filesystem::u8path(folder));
      if(!qar_library_load(library.c_str()))throw std::runtime_error("Cannot load the installed QAROS SDK");
      loaded=true;
      auto init=qar_library_init_default();init.enable_console_logging=false;init.log_folder_path=folder.c_str();
      checked(qar_library_init(&init),"initialize QAROS");initialized=true;
      auto ri=qar_runtime_init_default();checked(qar_runtime_create(&ri,&runtime),"create QAROS client");
      if(!runtime)throw std::runtime_error("SDK returned no QAROS client");
      auto local=qar_onboard_local_app_ext_default();local.approval_timeout_ms=120000;
      auto tag=qar_onboard_local_app_tag_ext_default();tag.app_tag=qarvtk::host_app_tag;local.header.next=&tag.header;
      auto onboard=qar_onboard_init_default();onboard.header.next=&local.header;
      onboard.presentation.display_name="Slicer live renderer";
      {std::lock_guard<std::mutex> lock(cancel_mutex);
       if(stopping)throw std::runtime_error("Connection stopped");
       checked(qar_cancel_token_create_with_timeout(&operation,125000),"create QAROS connection timeout");}
      QarOnboardingId identity{};
      const auto progress=[](QarActionSeverity,float,const char* text,void* context)noexcept{
        if(text&&context)try{static_cast<HostHubState*>(context)->set_status(text);}catch(...){}
      };
      checked(qar_runtime_onboard(runtime,&onboard,progress,this,operation,&identity,&session),"join installed QAROS Hub");
      if(!session)throw std::runtime_error("SDK returned no QAROS session");
      QarPeerSpec* raw{};checked(qar_session_get_my_spec(session,&raw),"read Slicer peer");
      {std::unique_ptr<QarPeerSpec,decltype(&qar_peer_spec_handle_destroy)> own(raw,qar_peer_spec_handle_destroy);
       if(!own)throw std::runtime_error("SDK returned no Slicer identity");
       QarPeerId id{};checked(qar_peer_spec_get_id(own.get(),&id),"read Slicer identity");
       std::lock_guard<std::mutex> lock(mutex);source_peer=id;}
      checked(qar_cancel_token_create(&subscription),"create receiver subscription");
      const auto update=[](QarPeerSpec* spec,void* context)noexcept{
        if(!spec||!context)return;
        auto& peers=*static_cast<qarvtk::HostTargetRegistry*>(context);
        try{peers.record(read(spec));}catch(const std::exception& e){try{peers.fail(e.what());}catch(...){}}
        catch(...){try{peers.fail("Cannot read QAROS receiver update");}catch(...){}}
      };
      checked(qar_peer_subscribe_updates(session,update,registry.get(),subscription),"watch QAROS receivers");
      for(;;){
        if(stopping)throw std::runtime_error("Connection stopped");
        size_t count{};checked(qar_query_peer_specs_count(session,&count),"count QAROS peers");
        if(count>4096)throw std::runtime_error("Too many QAROS peers");
        struct Snapshot {std::vector<QarPeerSpec*> handles;~Snapshot(){for(auto* p:handles)if(p)qar_peer_spec_handle_destroy(p);}};
        Snapshot snapshot{std::vector<QarPeerSpec*>(count+16,nullptr)};size_t written{};
        checked(qar_query_peer_specs(session,snapshot.handles.data(),snapshot.handles.size(),&written),"list QAROS peers");
        if(written>snapshot.handles.size())continue;
        for(size_t i=0;i<written;++i)if(snapshot.handles[i])registry->seed(read(snapshot.handles[i]));
        break;
      }
      {std::unique_lock<std::mutex> lock(mutex);
       status="Connected to QAROS. Select the receivers to stream to.";ready=!stopping;
       condition.wait(lock,[&]{return stopping.load()&&streams==0;});ready=false;}
    }catch(const std::exception& e){
      std::lock_guard<std::mutex> lock(mutex);if(!stopping){failed=true;status=e.what();}ready=false;
    }catch(...){std::lock_guard<std::mutex> lock(mutex);failed=true;ready=false;status="Unknown QAROS connection failure";}
    // Only this worker closes SDK objects, and only after every render lease
    // was released. No GUI/VTK thread waits for callback or session shutdown.
    {std::lock_guard<std::mutex> lock(cancel_mutex);if(operation){qar_cancel_token_cancel(operation);qar_cancel_token_handle_destroy(operation);operation=nullptr;}}
    if(subscription)qar_cancel_token_cancel(subscription);
    // A workspace belongs to the persistent Hub connection, not a transient
    // stream. Receivers have already been detached by the last stream's Stop.
    if(session)for(const auto& id:workspaces)qar_app_volumes_close_volume(session,&id);
    if(session)qar_session_handle_destroy(session);
    if(runtime)qar_runtime_handle_destroy(runtime);
    if(subscription)qar_cancel_token_handle_destroy(subscription);
    if(initialized)qar_library_destroy();
    if(loaded)qar_library_unload();
    if(owned)sdk_active=false;
    done=true;
  }
};
#endif

struct QarvtkHost {
  struct Volume {
    QarvtkHostPlacement placement{};
    std::string name;
    std::vector<std::unique_ptr<qarvtk::HostSharedWgl>> graphics;
    std::vector<std::unique_ptr<qarvtk::HostSharedWgl>> completions;
    std::vector<std::unique_ptr<qarvtk::WglCallbacks>> callbacks;
  };
  const DWORD owner=GetCurrentThreadId();
  HGLRC host_context{};
  HDC host_dc{};
  std::unique_ptr<qarvtk::HostSharedWgl> copying; // Diagnostic conservative mode only.
  bool asynchronous_copies=true;
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
  std::vector<QarCancelToken*> connection_operations; // Protected by cancel_mutex; one per target.
  std::shared_ptr<qarvtk::HostLiveSelection> live_selection;
  std::shared_ptr<HostHubState> hub;
  bool failed{};
  std::unique_ptr<RenderQueue> render_queue;
  const Clock::time_point created=Clock::now();
  std::atomic<uint64_t> sent{0};
  std::string status="Starting live Slicer transport",sdk_root,target,log_folder;
  std::ofstream log;

  ~QarvtkHost(){
    if(worker.joinable())worker.join();
#if QARVTK_HOST_DEVICE_SELECTION
    if(hub)hub->release();
#endif
    if(host_context){
      try {
        qarvtk::WglBinding binding(copying?copying->dc:host_dc,copying?copying->context:host_context);
        compositor.reset();copier.reset();
      }
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
    {std::lock_guard<std::mutex> lock(mutex);if(!failed){failed=true;status=text;log_line("ERROR: "+text);}}
    stopping=true;if(render_queue)render_queue->close();condition.notify_all();
  }
  void stop(){
    gui();stopping=true;
    if(panels)panels->request_stop();
    {std::lock_guard<std::mutex> lock(mutex);log_line("Stop requested by GUI");}
    if(render_queue)render_queue->close();
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
  std::size_t target_count()const{return live_selection?qarvtk::HostLiveSelection::capacity:target=="both"?2:1;}
  void prepare_target(std::size_t t){
    gui();
    if(wglGetCurrentContext()!=host_context)throw std::runtime_error("Make the Slicer render window current before changing receivers");
    const auto dc=wglGetCurrentDC();
    for(auto& volume:volumes)if(!volume.graphics[t]){
      auto graphics=std::make_unique<qarvtk::HostSharedWgl>(dc,host_context);
      auto callbacks=std::make_unique<qarvtk::WglCallbacks>(graphics->dc,graphics->context);
      const auto probe=callbacks->probe_on_worker();
      if(!probe.get_string_i||!probe.memory_import)throw std::runtime_error("Shared SDK context lacks required OpenGL interop functions");
      volume.completions[t]=std::make_unique<qarvtk::HostSharedWgl>(dc,host_context);
      volume.graphics[t]=std::move(graphics);volume.callbacks[t]=std::move(callbacks);
    }
  }
  void initialize(){
    gui();host_context=wglGetCurrentContext();const auto dc=wglGetCurrentDC();host_dc=dc;
    if(!host_context||!dc)throw std::runtime_error("Make the Slicer render window current before creating the host bridge");
    char mode[32]{};
    const auto mode_length=GetEnvironmentVariableA("QARVTK_HOST_GPU_COPY_MODE",mode,sizeof(mode));
    if(mode_length>=sizeof(mode))throw std::runtime_error("Invalid diagnostic GPU copy mode");
    if(mode_length){
      if(std::strcmp(mode,"conservative")==0)asynchronous_copies=false;
      else if(std::strcmp(mode,"async")!=0)throw std::runtime_error("Invalid diagnostic GPU copy mode");
    }
    if(target!="visualizer"&&target!="quest"&&target!="headset"&&target!="both"&&target!="selected")
      throw std::runtime_error("Host target must be visualizer, headset or both");
    if(target=="selected"&&(!hub||!live_selection))
      throw std::runtime_error("Explicit receiver selection requires the existing QAROS connection");
#if !QARVTK_HOST_ONBOARDING_API
    if(target=="both")throw std::runtime_error("Visualizer + headset requires the installed QAROS Hub SDK (dev.2927)");
#endif
    const auto sdk=std::filesystem::u8path(sdk_root);
    if(!std::filesystem::is_regular_file(sdk/"bin"/"qar-streaming-c.dll"))throw std::runtime_error("Missing complete QAROS session runtime");
    std::filesystem::create_directories(std::filesystem::u8path(log_folder));
    log.open(std::filesystem::u8path(log_folder)/"host-bridge.log",std::ios::app);
    if(!log)throw std::runtime_error("Cannot open host bridge log");
    log<<"Host build: installed-hub-update-r8.8 (ABI 5); SDK header " QARVTK_HOST_SDK_HASH<<std::endl;
    log<<"SDK root: "<<sdk_root<<std::endl;
    log<<"Target mode: "<<target<<"; workspaces="<<volumes.size()<<std::endl;
    if(!asynchronous_copies)copying=std::make_unique<qarvtk::HostSharedWgl>(dc,host_context);
    {
      qarvtk::WglBinding binding(copying?copying->dc:host_dc,copying?copying->context:host_context);
      qarvtk::hostgl::load();adapter=device();
      copier=std::make_unique<qarvtk::GlCopy>();compositor=std::make_unique<qarvtk::GlComposite>();
      log<<"Host GPU: "<<glGetString(GL_VENDOR)<<" / "<<glGetString(GL_RENDERER)<<" / "<<glGetString(GL_VERSION)<<std::endl;
      log<<"GPU copy mode: "<<(asynchronous_copies?"original-context copies with per-receiver completion fence":"conservative shared-context copies with per-plane glFinish")<<std::endl;
    }
    // SDK senders may invoke graphics callbacks concurrently. Give each its
    // own context, all sharing Slicer's objects, instead of competing for one.
    // Reserve the bounded target slots on the GUI thread, including a headset
    // that may join later. No SDK worker creates or destroys Windows windows.
    connection_operations.resize(target_count(),nullptr);
    for(auto& volume:volumes){volume.graphics.resize(target_count());volume.completions.resize(target_count());volume.callbacks.resize(target_count());}
    const auto wanted=live_selection?live_selection->snapshot():qarvtk::HostLiveSelection::Snapshot{};
    for(std::size_t t=0;t<target_count();++t)if(!live_selection||wanted[t].wanted)prepare_target(t);
    panels=std::make_unique<qarvtk::HostPanels>([this](const std::string& text){note(text);});
    render_queue=std::make_unique<RenderQueue>(volumes.size()*target_count());
    worker=std::thread([this]{run();});
  }
  void run() noexcept;
  void seal_gpu_completion(const RenderQueue::Handle& job){
    if(!asynchronous_copies||job->payload.completion)return;
    if(wglGetCurrentContext()!=host_context)throw std::runtime_error("Host context changed before GPU completion");
    try{job->payload.completion=std::make_shared<qarvtk::HostGpuFence>();}
    catch(const std::exception& error){
      // Allocation failure must not strand an acquired lease. The conservative
      // fallback is used only on this error path, never per plane in normal use.
      for(unsigned n=0;n<8;++n){const auto result=glGetError();
        if(result==0x0507 /* GL_CONTEXT_LOST */)throw;
        if(result==GL_NO_ERROR)break;}
      glFinish();
      if(glGetError()!=GL_NO_ERROR)throw;
      log_line(std::string("GPU_FENCE_FALLBACK: ")+error.what());
    }
  }
  void copy(uint64_t serial,unsigned eye,qarvtk::RenderedEye source,bool depth,unsigned vc=0,unsigned vd=0){
    gui();
    if(wglGetCurrentContext()!=host_context)throw std::runtime_error("Slicer changed the host OpenGL context; restart streaming");
    std::lock_guard<std::mutex> lock(mutex);
    if(stopping)throw std::runtime_error("Invalid or expired host frame lease");
    const auto job=render_queue->check_plane(serial,eye,depth);
    auto& request=job->payload.request;
    auto& timings=job->payload.timings;
    if(!source.width||!source.height||source.width>request.eyes[eye].width||source.height>request.eyes[eye].height)
      throw std::runtime_error("Host render size must be positive and no larger than the requested eye");
    // Copy in the same command stream as Slicer's original drawing. OpenGL
    // orders reads before the next eye overwrites the borrowed host textures;
    // no per-plane CPU drain or context switch is needed.
    auto stamp=Clock::now();
    std::unique_ptr<qarvtk::WglBinding> binding;
    if(!asynchronous_copies){
      glFinish();timings.host_sync+=milliseconds(stamp);
      binding=std::make_unique<qarvtk::WglBinding>(*copying);
    }
    if((vc==0)!=(vd==0))throw std::runtime_error("Incomplete volume depth textures");
    if(depth&&vc){
      stamp=Clock::now();
      if(!glIsTexture(vc)||!glIsTexture(vd))throw std::runtime_error("Volume textures are not shared");
      source=compositor->combine(source,vc,vd);
      timings.composite+=milliseconds(stamp);
    }
    stamp=Clock::now();
    copier->copy_plane(source,job->payload.frame,job->payload.views[eye],depth);
    timings.copy+=milliseconds(stamp);
    if(!asynchronous_copies){stamp=Clock::now();copier->finish();timings.copy_finish+=milliseconds(stamp);}
    render_queue->complete_plane(serial,eye,depth);
  }
};

namespace {
// Each sender owns an independent begin/render-completion/show cycle. The
// coordinator only manages discovery/lifecycle; the original Slicer renderer
// still executes every queued draw on its creating GUI thread.
struct ReceiverPump {
  std::thread worker;
  QarCancelToken* frame_wait{};
  std::atomic<bool> stopping{false},done{false};
  std::atomic<uint64_t> sent{0};
  std::mutex mutex;
  std::condition_variable condition;
  std::string error;
  ~ReceiverPump(){request_stop();join();if(frame_wait)qar_cancel_token_handle_destroy(frame_wait);}
  template<class Run> void start(Run run){
    checked(qar_cancel_token_create(&frame_wait),"create receiver frame-wait cancellation");
    worker=std::thread([this,run]{
      try{run(*this);}
      catch(const std::exception& e){error=e.what();}
      catch(...){error="Unknown receiver frame-cycle failure";}
      done.store(true,std::memory_order_release);
    });
  }
  void request_stop(){stopping=true;if(frame_wait)qar_cancel_token_cancel(frame_wait);condition.notify_all();}
  bool pause(std::chrono::milliseconds duration){
    std::unique_lock<std::mutex> lock(mutex);
    condition.wait_for(lock,duration,[&]{return stopping.load();});return !stopping;
  }
  void join(){if(worker.joinable())worker.join();}
};
struct Session {
  struct AppVolume {QarAppVolumeId id=qar_app_volume_id_default();bool created{};};
  struct Stream {
    QarRenderSender* sender{};
    uint64_t sent{};
    std::unique_ptr<ReceiverPump> pump;
    Clock::time_point retry_after{};
    qarvtk::HostStreamStartup::State reported=qarvtk::HostStreamStartup::State::pending;
  };
  struct Target {
    QarPeerId peer=qar_peer_id_default();
    std::string name,discovery_error;
    std::unique_ptr<qarvtk::HostStreamStartup> startup;
    std::unique_ptr<qarvtk::HostTargetRetirement> retirement;
    Clock::time_point retry_after{};
    bool panels_added{};
  };
  QarvtkHost& host;
  bool loaded{},initialized{},owns_sdk{};
  QarRuntime* runtime{};QarSession* session{};
  QarCancelToken* subscription{};
  qarvtk::HostStreamRoutes routes;
  std::vector<AppVolume> volumes;
  std::vector<Stream> streams;
  std::vector<Target> targets;
  std::vector<QarPeerId> panel_peers;
  qarvtk::HostLiveSelection::Snapshot desired{};
  std::uint64_t selection_generation=0;
  Clock::time_point waiting_since=Clock::now();
#if QARVTK_HOST_ONBOARDING_API
  QarPeerId source_peer=qar_peer_id_default();
  std::shared_ptr<qarvtk::HostTargetRegistry> target_registry=std::make_shared<qarvtk::HostTargetRegistry>();
  std::string peer_inventory;
#endif
  bool panels_started{};
  qarvtk::LegacyPeerObserver peers;
  explicit Session(QarvtkHost& h):host(h),routes(h.volumes.size(),h.target_count()),
    volumes(h.volumes.size()),streams(routes.size()),targets(h.target_count()){}
  ~Session(){
    for(std::size_t i=0;i<streams.size();++i)stop_pump(i);
    host.render_queue->close();
    for(auto& target:targets)if(target.startup)target.startup->request_stop();
    {std::lock_guard<std::mutex> lock(host.cancel_mutex);
     for(auto* token:host.connection_operations)if(token)qar_cancel_token_cancel(token);}
    for(std::size_t t=0;t<targets.size();++t){
      auto& target=targets[t];
      if(target.retirement&&target.retirement->started())target.retirement->join();
      else{
        if(target.startup)target.startup->join();
        for(std::size_t v=0;v<volumes.size();++v){auto& stream=streams[routes.index(t,v)];if(stream.pump)stream.pump->join();}
      }
    }
    // Panel video uses this session concurrently, but never outlives it.
    if(host.panels){host.panels->request_stop();host.panels->join();}
    if(subscription)qar_cancel_token_cancel(subscription);
    // Stop must detach the same audience as a live uncheck. Closing and then
    // reopening a get-or-create workspace alone can leave a receiver mixer
    // waiting on its previous sender registration. Detach all known targets
    // before destroying the senders. The Hub retains the empty workspace.
#if QARVTK_HOST_DEVICE_SELECTION
    std::vector<QarPeerId> departing;
    const auto no_peer=qar_peer_id_default();
    for(const auto& target:targets)
      if(!qar_peer_id_equals(&target.peer,&no_peer))departing.push_back(target.peer);
    for(const auto& volume:volumes)if(volume.created&&!departing.empty()){
      const auto result=qar_app_volumes_update_used_by_peers(session,&volume.id,nullptr,0,departing.data(),departing.size());
      if(!qar_result_is_success(result))host.note("Unable to detach workspace audience during Stop");
    }
#endif
    // Detach while the old producer still exists. Otherwise a mixer can start
    // reconnecting to a sender we have just destroyed before seeing Stop.
    for(auto& stream:streams)if(stream.sender)qar_render_stream_handle_destroy(stream.sender);
    // The connected Hub keeps its stable workspaces with no audience until
    // Disconnect. Legacy standalone sessions still close their own volumes.
    for(auto& volume:volumes)if(volume.created&&!host.hub)qar_app_volumes_close_volume(session,&volume.id);
#if QARVTK_HOST_ONBOARDING_API
    if(session&&!host.hub)qar_session_handle_destroy(session);
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
    if(owns_sdk)sdk_active=false;
  }
  void open(){
#if QARVTK_HOST_DEVICE_SELECTION
    if(host.hub){
      session=host.hub->session;source_peer=host.hub->source_peer;target_registry=host.hub->registry;
      auto adapter=qar_graphics_device_id_default();bool present=false;
      checked(qar_session_get_graphics_device_id(session,&adapter,&present),"read connected QAROS GPU");
      if(!present||adapter.id_type!=QAR_GPU_DEVICE_ID_TYPE_LUID||
         std::memcmp(&adapter.luid,&host.adapter.luid,sizeof(adapter.luid))!=0)
        throw std::runtime_error("QAROS Hub and Slicer must use the same GPU. Select the QAROS GPU for Slicer and reconnect.");
      host.note("Using persistent QAROS connection; app_tag="+std::string(qarvtk::host_app_tag)+" source_peer="+id_text(source_peer));
      return;
    }
#endif
    bool expected=false;
    if(!sdk_active.compare_exchange_strong(expected,true))throw std::runtime_error("Use the existing QAROS connection when starting a stream");
    owns_sdk=true;
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
#if QARVTK_HOST_DEVICE_SELECTION
    auto tag=qar_onboard_local_app_tag_ext_default();tag.app_tag=qarvtk::host_app_tag;
    tag.header.next=local.header.next;local.header.next=&tag.header;
#endif
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
    if(host.hub)return; // Already subscribed by the persistent device browser.
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
    checked(qar_peer_subscribe_updates(session,update,target_registry.get(),subscription),"subscribe QAROS targets");
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
        target_registry->seed(read_target(spec));
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
    if(host.live_selection)return !targets[slot].name.empty()?targets[slot].name:desired[slot].name;
    return (host.target=="visualizer"||(host.target=="both"&&slot==0))?"Visualizer":"Headset";
  }
  void start(){
    if(host.live_selection)host.live_selection->snapshot(desired,selection_generation);
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
#if QARVTK_HOST_DEVICE_SELECTION
    // Publish the complete app-volume membership before negotiating this
    // sender. Do not rely on concurrent create_sender calls to append peers.
    try{
      for(const auto& volume:volumes)
        checked(qar_app_volumes_update_used_by_peers(session,&volume.id,&peer,1,nullptr,0),"attach receiver to workspace");
    }catch(const std::exception& error){
      target.discovery_error=error.what();target.retry_after=Clock::now()+std::chrono::seconds(2);
      host.note("TARGET_WAIT peer="+id_text(peer)+" reason="+target.discovery_error);return;
    }
#endif
    host.note("TARGET_SELECTED type="+target_label(slot)+" peer="+id_text(peer)+" name="+name);
    target.startup=std::make_unique<qarvtk::HostStreamStartup>(volumes.size());
    target.startup->start([this,slot](std::size_t i){start_stream(slot,i);},[this]{return host.stopping.load();});
  }
  void retire_target(std::size_t slot,const std::string& reason){
    auto& target=targets[slot];
    if(!target.startup||target.retirement)return;
    target.startup->request_stop();
    for(std::size_t v=0;v<volumes.size();++v)stop_pump(routes.index(slot,v));
    {std::lock_guard<std::mutex> lock(host.cancel_mutex);
     if(host.connection_operations[slot])qar_cancel_token_cancel(host.connection_operations[slot]);}
    target.retirement=std::make_unique<qarvtk::HostTargetRetirement>();
    auto* retirement=target.retirement.get();
    const auto peer=target.peer;
    host.note("TARGET_RETIRE peer="+id_text(peer)+" reason="+reason);
    retirement->start([this,slot,peer]{
      targets[slot].startup->join();
      std::string error;
      for(std::size_t v=0;v<volumes.size();++v){
        auto& stream=streams[routes.index(slot,v)];
        if(stream.pump)stream.pump->join();
#if QARVTK_HOST_DEVICE_SELECTION
        const auto result=qar_app_volumes_update_used_by_peers(session,&volumes[v].id,nullptr,0,&peer,1);
        if(!qar_result_is_success(result))error="Unable to remove receiver from workspace audience";
#endif
        if(stream.sender){qar_render_stream_handle_destroy(stream.sender);stream.sender=nullptr;}
      }
      if(!error.empty())throw std::runtime_error(error);
    });
  }
  void refresh_live_selection(){
    if(!host.live_selection)return;
    host.live_selection->snapshot(desired,selection_generation);
    for(std::size_t t=0;t<targets.size();++t){
      auto& target=targets[t];
      if(target.retirement){
        if(!target.retirement->ready())continue;
        target.retirement->join();
        if(!target.retirement->error().empty())host.note(target.retirement->error());
        const auto retired=id_text(target.peer);
        target.retirement.reset();target.startup.reset();target.name.clear();target.discovery_error.clear();
        for(std::size_t v=0;v<volumes.size();++v)streams[routes.index(t,v)]={};
        host.live_selection->release_unwanted(t,retired);
      }
      if(!desired[t].wanted){
        if(target.startup)retire_target(t,"receiver unchecked");
        else host.live_selection->release_unwanted(t,desired[t].id);
      }
    }
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
#if QARVTK_HOST_DEVICE_SELECTION
    if(host.hub){
      host.hub->remember_workspace(volume.id);
      // get_or_create ignores init for an existing, open volume. Apply the
      // current Start settings explicitly when reusing it after Stop.
      checked(qar_app_volumes_change_display_name(session,&volume.id,app.display_name),"update workspace name");
      checked(qar_app_volumes_change_size(session,&volume.id,&app.size),"update workspace size");
      checked(qar_app_volumes_change_pose(session,&volume.id,&app.pose),"update workspace pose");
      checked(qar_app_volumes_change_app_pose(session,&volume.id,&app.app_pose),"update workspace app pose");
      checked(qar_app_volumes_change_app_scale(session,&volume.id,app.app_scale),"update workspace app scale");
    }
    if(host.live_selection){
      std::vector<QarPeerId> audience;
      const auto available=target_registry->snapshot();
      for(const auto& choice:desired)if(choice.wanted)
        for(const auto& peer:available)if(id_text(peer.id)==choice.id&&qarvtk::host_target_available(peer))
          audience.push_back(peer.id);
      if(!audience.empty())checked(qar_app_volumes_update_used_by_peers(session,&volume.id,
          audience.data(),audience.size(),nullptr,0),"publish workspace audience");
    }
#endif
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
        // A stale receiver negotiation can wait forever without producing a
        // first frame. Bound this attempt, then let target retirement detach
        // and retry this receiver without replacing any healthy sender.
        checked(qar_cancel_token_create_with_timeout(&token,5000),"create sender timeout");
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
    return !targets[route.target].retirement&&(!host.live_selection||desired[route.target].wanted)&&startup&&startup->ready(route.volume);
  }
  std::size_t target_ready_count(std::size_t slot)const{
    if(targets[slot].retirement||(host.live_selection&&!desired[slot].wanted))return 0;
    std::size_t count=0;
    if(targets[slot].startup)for(std::size_t v=0;v<volumes.size();++v)if(targets[slot].startup->ready(v))++count;
    return count;
  }
  std::size_t ready_count() const {
    std::size_t count=0;for(std::size_t t=0;t<host.target_count();++t)count+=target_ready_count(t);return count;
  }
  void stop_pump(std::size_t index){
    auto& stream=streams[index];
    if(stream.pump)stream.pump->request_stop();
    host.render_queue->disable(index);
  }
  void reconcile_pumps(){
    for(std::size_t i=0;i<streams.size();++i){
      auto& stream=streams[i];
      if(stream.pump)stream.sent=stream.pump->sent.load();
      if(!ready(i)||host.stopping)continue;
      if(stream.pump){
        if(stream.pump->done.load(std::memory_order_acquire)&&!stream.pump->stopping){
          const auto route=routes.address(i);
          const auto error=stream.pump->error.empty()?"Receiver frame cycle ended":stream.pump->error;
          host.note("RECEIVER_FRAME_ERROR sender="+std::to_string(i)+" error="+error);
          targets[route.target].retry_after=Clock::now()+std::chrono::seconds(2);
          retire_target(route.target,error);
        }
        continue;
      }
      const auto route=routes.address(i);
      const auto label=target_label(route.target);
      auto* sender=stream.sender;
      host.render_queue->enable(i);
      stream.pump=std::make_unique<ReceiverPump>();
      stream.pump->start([this,i,sender,label](ReceiverPump& pump){pump_stream(i,sender,label,pump);});
    }
  }
  void pump_stream(std::size_t index,QarRenderSender* sender,const std::string& label,ReceiverPump& pump){
    const auto route=routes.address(index);
    const auto placement=host.volumes[route.volume].placement;
    auto cycle_start=Clock::now(),window_start=cycle_start;
    FrameTimings totals;
    double wait_ms=0,metadata_ms=0,gui_ms=0,gpu_ms=0,show_ms=0;
    std::vector<std::array<double,8>> jitter_samples;
    auto previous_delivery=Clock::time_point{};
    qarvtk::HostFramePacer pacer;
    uint64_t measured=0;
    QarvtkHostFrame last_request{};
    auto report=[&](bool force){
      const double elapsed=milliseconds(window_start);
      if(!measured||(!force&&elapsed<5000))return;
      const double count=static_cast<double>(measured);
      std::ostringstream line;line<<std::fixed<<std::setprecision(2)
        <<"PERF_NATIVE frames="<<measured<<" submitted_fps="<<count*1000/elapsed
        <<" sender="<<index<<" scope=receiver"
        <<" eye_size="<<last_request.eyes[0].width<<'x'<<last_request.eyes[0].height
        <<" depth_size="<<last_request.eyes[0].depth_width<<'x'<<last_request.eyes[0].depth_height
        <<" sdk_wait_ms="<<wait_ms/count<<" metadata_ms="<<metadata_ms/count
        <<" gui_wait_ms="<<gui_ms/count<<" gpu_wait_ms="<<gpu_ms/count<<" sdk_show_ms="<<show_ms/count
        <<" host_sync_ms="<<totals.host_sync/count<<" composite_cpu_ms="<<totals.composite/count
        <<" copy_cpu_ms="<<totals.copy/count<<" copy_finish_ms="<<totals.copy_finish/count
        <<" receiver_frames="<<pump.sent.load();
      host.note(line.str());
      std::ostringstream trace;trace<<std::fixed<<std::setprecision(3)
        <<"JITTER_NATIVE {\"sender\":"<<index<<",\"fields\":[\"serial\",\"end_elapsed_ms\",\"delivery_interval_ms\",\"sdk_begin_ms\",\"metadata_ms\",\"gui_ms\",\"gpu_wait_ms\",\"sdk_show_ms\"],\"samples\":[";
      for(std::size_t n=0;n<jitter_samples.size();++n){
        if(n)trace<<',';trace<<'[';
        for(std::size_t k=0;k<jitter_samples[n].size();++k){if(k)trace<<',';trace<<jitter_samples[n][k];}
        trace<<']';
      }
      trace<<"]}";host.note(trace.str());jitter_samples.clear();
      totals={};wait_ms=metadata_ms=gui_ms=gpu_ms=show_ms=0;measured=0;window_start=Clock::now();
    };
    while(!host.stopping&&!pump.stopping){
      char rate[8]{};
      unsigned fps=0;
      const auto rate_length=GetEnvironmentVariableA("QARVTK_HOST_SOURCE_FPS",rate,sizeof(rate));
      if(rate_length>=sizeof(rate))throw std::runtime_error("Invalid source FPS limit");
      if(rate_length){
        char* end{};const auto value=std::strtoul(rate,&end,10);
        if(end==rate||*end||value>120)throw std::runtime_error("Source FPS limit must be between 0 and 120");
        fps=static_cast<unsigned>(value);
      }
      const auto due=pacer.next(Clock::now(),fps);
      {
        std::unique_lock<std::mutex> lock(pump.mutex);
        pump.condition.wait_until(lock,due,[&]{return pump.stopping.load()||host.stopping.load();});
      }
      if(host.stopping||pump.stopping)break;
      std::unique_ptr<QarRenderFrameInfo,decltype(&qar_render_frame_info_handle_destroy)> info(nullptr,qar_render_frame_info_handle_destroy);
        QarRenderFrameInfo* raw{};
        // Each receiver can block on its own cancellable wait. Finish that
        // wait explicitly before sender destruction instead of abandoning
        // the SDK's hidden pending try_begin_frame operation on Stop.
        const auto begin_at=Clock::now();
        const auto result=qar_render_sender_begin_frame(sender,pump.frame_wait,&raw);info.reset(raw);
        if(!qar_result_is_success(result)&&(host.stopping||pump.stopping))break;
      if(qarvtk::legacy_begin_frame_wait(result,pump.sent.load()==0)!=qarvtk::LegacyFrameWait::none){
        pump.pause(std::chrono::milliseconds(100));continue;
      }
      if(qar_result_has_code(result,QAR_STATUS_MEDIA_STREAMING_STREAM_IS_RECONNECTING)||
         qar_result_has_code(result,QAR_STATUS_MEDIA_STREAMING_NO_ACTIVE_TRANSFERS)){
        pump.pause(std::chrono::milliseconds(2));continue;
      }
      checked(result,"begin receiver frame");
      if(!info)throw std::runtime_error("Ready receiver frame has no metadata");
      const auto ready_at=Clock::now();
      auto access=qar_video_frame_opengl_default();
      checked(qar_render_sender_frame_opengl(sender,&access),"access receiver frame textures");
      RenderJob payload;payload.frame=layout(access);payload.views=qarvtk::validate_stereo(payload.frame);
      auto& request=payload.request;
      request.volume_index=static_cast<uint32_t>(route.volume);
      request.near_plane=static_cast<float>(.05/placement.app_scale);
      request.far_plane=static_cast<float>(20./placement.app_scale);
      for(unsigned i=0;i<2;++i){
        const auto& view=payload.frame.views[payload.views[i].color];auto& eye=request.eyes[i];
        QarPose pose=qar_pose_default();QarFov fov=qar_fov_default();
        checked(qar_render_frame_info_get_view_pose(info.get(),payload.views[i].color,&pose),"receiver eye pose");
        checked(qar_render_frame_info_get_view_fov(info.get(),payload.views[i].color,&fov),"receiver eye FOV");
        eye.position[0]=pose.position.x;eye.position[1]=pose.position.y;eye.position[2]=pose.position.z;
        eye.orientation[0]=pose.orientation.x;eye.orientation[1]=pose.orientation.y;eye.orientation[2]=pose.orientation.z;eye.orientation[3]=pose.orientation.w;
        eye.fov[0]=fov.angle_left;eye.fov[1]=fov.angle_right;eye.fov[2]=fov.angle_up;eye.fov[3]=fov.angle_down;
        eye.width=qarvtk::width(view);eye.height=qarvtk::height(view);
        const auto& depth=payload.frame.views[payload.views[i].depth];
        eye.depth_width=qarvtk::width(depth);eye.depth_height=qarvtk::height(depth);
        if(eye.depth_width!=(eye.width+1)/2||eye.depth_height!=(eye.height+1)/2)
          throw std::runtime_error("SDK did not retain the requested half-resolution depth layout");
      }
      payload.posted_at=Clock::now();
      const auto posted_at=payload.posted_at;
      auto job=host.render_queue->post(index,std::move(payload));
        if(!job){
          // A successful SDK begin owns a frame even if Stop closed the GUI
          // queue before posting. End that cycle before destroying its sender.
          auto show=qar_render_frame_show_default();
          show.rendered_near_far={static_cast<float>(.05/placement.app_scale),static_cast<float>(20./placement.app_scale)};
          show.depth_scale=1;
          checked(qar_render_sender_show_frame(sender,&show),"drain stopped receiver frame");
          host.note("FRAME_DRAIN sender="+std::to_string(index)+" before_queue=1");
          break;
        }
      const bool completed=host.render_queue->wait(job);
      const auto copied=job->payload.timings;
      const auto finished_at=job->payload.finished_at;
      last_request=job->payload.request;
      if(job->payload.gpu_completion_failed){
        host.render_queue->consume(job);
        throw std::runtime_error("Host GPU context was lost; retiring sender without submitting invalid textures");
      }
      const auto gpu_wait_at=Clock::now();
      // The GUI has released its slot but this sender still owns its SDK
      // textures. Wait only on its own shared completion context, never on the
      // GUI or the SDK's graphics callback context.
      try{
        if(job->payload.completion){
          qarvtk::WglBinding binding(*host.volumes[route.volume].completions[route.target]);
          job->payload.completion->wait_and_release();
        }
      }catch(...){host.render_queue->consume(job);throw;}
      const auto gpu_ready_at=Clock::now();
      host.render_queue->consume(job);
        const bool draining=!completed||host.stopping||pump.stopping;
      // The GUI has already released its active slot; other receivers can
      // render while this SDK call is pending. No global frame-pump wait.
      const auto show_at=Clock::now();
      auto show=qar_render_frame_show_default();
      show.rendered_near_far={static_cast<float>(last_request.near_plane),static_cast<float>(last_request.far_plane)};show.depth_scale=1;
      checked(qar_render_sender_show_frame(sender,&show),"submit receiver stereo frame");
        if(draining){host.note("FRAME_DRAIN sender="+std::to_string(index)+" before_queue=0");break;}
      const auto end=Clock::now();const auto count=++pump.sent;++host.sent;
      if(jitter_samples.size()<512)jitter_samples.push_back({static_cast<double>(job->serial),
        milliseconds(host.created,end),previous_delivery==Clock::time_point{}?0:milliseconds(previous_delivery,end),
        milliseconds(begin_at,ready_at),milliseconds(ready_at,posted_at),milliseconds(posted_at,finished_at),
        milliseconds(gpu_wait_at,gpu_ready_at),milliseconds(show_at,end)});
      previous_delivery=end;gpu_ms+=milliseconds(gpu_wait_at,gpu_ready_at);
      if(count==1)host.note("FIRST_FRAME sender="+std::to_string(index)+" target="+label+
        " volume="+std::to_string(route.volume)+" name="+host.volumes[route.volume].name);
      totals+=copied;++measured;wait_ms+=milliseconds(cycle_start,ready_at);
      metadata_ms+=milliseconds(ready_at,posted_at);gui_ms+=milliseconds(posted_at,finished_at);
      show_ms+=milliseconds(show_at,end);cycle_start=end;report(false);
    }
    report(true);
  }
  std::string progress() const {
    std::string text;
    for(std::size_t t=0;t<host.target_count();++t){
      const auto& target=targets[t];
      if(host.live_selection&&!desired[t].wanted&&!target.retirement)continue;
      if(!text.empty())text+=" | ";text+=target_label(t)+": ";
      if(target.retirement){text+="disconnecting";continue;}
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
    return text.empty()?"Streaming is idle. Check a connected receiver to resume.":text;
  }
  void refresh(){
    bool changed=false;
    if(host.live_selection){
      const auto previous=selection_generation;refresh_live_selection();
      changed=previous!=selection_generation;
    }
#if QARVTK_HOST_ONBOARDING_API
    const auto available=target_registry->snapshot();
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
      auto& target=targets[t];
      if(target.startup||target.retirement)continue;
      if(host.live_selection&&(!desired[t].wanted||Clock::now()<target.retry_after))continue;
      const auto category=target_label(t)=="Visualizer"?QAR_DEVICE_CATEGORY_VISUALIZER:QAR_DEVICE_CATEGORY_HEADSET;
      std::optional<qarvtk::HostTarget> selected;
      try{
        if(!host.live_selection)selected=qarvtk::select_host_target(available,category);
        else for(const auto& peer:available)
          if(id_text(peer.id)==desired[t].id&&qarvtk::host_target_available(peer)){selected=peer;break;}
      }
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
        const std::string reason=host.live_selection?"Selected receiver is disconnected; waiting for the same receiver":target_label(t)=="Headset"?
          "No available headset target in this QAROS session; connect the headset to the same Hub":
          "No available Visualizer receiver in this QAROS session; create a receiver in QAROS";
        if(target.discovery_error!=reason){target.discovery_error=reason;changed=true;
          host.note("TARGET_WAIT type="+target_label(t)+" reason="+reason);}
      }
    }
#endif
    for(std::size_t i=0;i<streams.size();++i){
      const auto route=routes.address(i);const auto& startup=targets[route.target].startup;
      if(!startup||targets[route.target].retirement)continue;
      const auto state=startup->state(route.volume);
      if(state==streams[i].reported)continue;
      streams[i].reported=state;changed=true;
      if(state==qarvtk::HostStreamStartup::State::failed)
        host.note("WORKSPACE_FAILED index="+std::to_string(i)+" target="+target_label(route.target)+
          " peer="+id_text(targets[route.target].peer)+" name="+host.volumes[route.volume].name+" error="+startup->error(route.volume));
    }
    if(host.live_selection)for(std::size_t t=0;t<targets.size();++t){
      auto& target=targets[t];
      if(desired[t].wanted&&target.startup&&!target.retirement&&target.startup->done()&&!target_ready_count(t)){
        target.retry_after=Clock::now()+std::chrono::seconds(2);
        retire_target(t,"receiver setup failed; retrying");changed=true;
      }
    }
    if(changed)host.set_status(progress());
    if(host.live_selection){
      std::vector<QarPeerId> next;
      for(std::size_t t=0;t<targets.size();++t)if(desired[t].wanted&&!targets[t].retirement&&target_ready_count(t))next.push_back(targets[t].peer);
      const bool audience_changed=next.size()!=panel_peers.size()||!std::equal(next.begin(),next.end(),panel_peers.begin(),
        [](const auto& a,const auto& b){return qar_peer_id_equals(&a,&b);});
      if(audience_changed){
        if(!panels_started&&!next.empty()){host.panels->start(session,next);panels_started=true;}
        else if(panels_started)host.panels->set_peers(next);
        panel_peers=std::move(next);
      }
      return;
    }
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
    auto last=Clock::now();uint64_t observed=0,published=0;
    while(!stopping){
      sdk.refresh();
      sdk.reconcile_pumps();
      const auto current=sent.load();
      if(current!=observed){observed=current;last=Clock::now();}
      if(!sdk.ready_count())last=Clock::now(); // Pose deadline starts when a sender is usable.
      else if(!live_selection&&Clock::now()-last>std::chrono::seconds(60))throw std::runtime_error("No viewer frame/pose for 60 seconds");
      if((current&&published==0)||current>=published+120){
        set_status(sdk.progress()+"; submitted "+std::to_string(current)+" stereo frames total; independent receiver cycles");
        published=current;
      }
      pause(std::chrono::milliseconds(2));
    }
  } catch(const std::exception& e){if(!stopping)fail(e.what());}
    catch(...){if(!stopping)fail("Unknown native host transport failure");}
  // Session destruction (and all SDK workers) must finish before destroy is allowed.
  note("SDK worker exited; submitted total="+std::to_string(sent.load()));
  done=true;condition.notify_all();
}

extern "C" {
const char* qarvtk_host_info(){
  return "{\"host_abi\":5,\"independent_receiver_cycles\":1,\"stop_detaches_audience\":1,\"frame_abort\":1,\"render_queue_depth_per_sender\":1,\"modern_api\":0,\"panel_probe\":0,\"panel_size_on_start\":1,\"panel_image_region\":1,\"target_discovery\":"
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
    ",\"device_selection\":"
#if QARVTK_HOST_DEVICE_SELECTION
    "1,\"persistent_connection\":1,\"live_selection\":1,\"stable_app_tag\":\"" QARVTK_LOCAL_APP_TAG "\",\"max_selected_targets\":8"
#else
    "0,\"persistent_connection\":0,\"live_selection\":0,\"stable_app_tag\":\"\",\"max_selected_targets\":2"
#endif
    ",\"header_sha256\":\"" QARVTK_HOST_SDK_HASH "\",\"eye_size\":104,\"frame_size\":240,\"placement_size\":112,\"volume_size\":120}";
}
int qarvtk_hub_connect(const char* sdk,const char* folder,QarvtkHub** result,char* error,uint32_t size){
  if(result)*result=nullptr;
#if QARVTK_HOST_DEVICE_SELECTION
  try{
    if(!sdk||!folder||!result)throw std::invalid_argument("SDK and log folder are required");
    auto handle=std::make_unique<QarvtkHub>();handle->state=std::make_shared<HostHubState>(sdk,folder);
    auto* state=handle->state.get();state->worker=std::thread([state]{state->run();});
    *result=handle.release();return 0;
  }catch(const std::exception& e){message(error,size,e.what());return -1;}
   catch(...){message(error,size,"Cannot connect to QAROS");return -1;}
#else
  message(error,size,"Connected receiver selection requires the dev.3184 SDK and r8.6 bridge");return -1;
#endif
}
int qarvtk_hub_poll(QarvtkHub* hub,char* json,uint32_t size){
#if QARVTK_HOST_DEVICE_SELECTION
  if(!hub||!json||!size||hub->owner!=GetCurrentThreadId())return -1;
  try{
    const auto snapshot=hub->state->snapshot();
    if(snapshot.size()>=size)throw std::runtime_error("QAROS receiver list exceeds the snapshot buffer");
    message(json,size,snapshot);
    std::lock_guard<std::mutex> lock(hub->state->mutex);
    return hub->state->failed?-1:hub->state->done?2:hub->state->ready?1:0;
  }catch(const std::exception& e){message(json,size,"{\"connected\":false,\"peers\":[],\"status\":"+json_text(e.what())+"}");return -1;}
#else
  return -1;
#endif
}
void qarvtk_hub_stop(QarvtkHub* hub){
#if QARVTK_HOST_DEVICE_SELECTION
  if(hub&&hub->owner==GetCurrentThreadId())hub->state->stop();
#endif
}
int qarvtk_hub_destroy(QarvtkHub* hub){
  if(!hub)return 1;
#if QARVTK_HOST_DEVICE_SELECTION
  if(hub->owner!=GetCurrentThreadId()||!hub->state->done)return 0;
  delete hub;return 1;
#else
  return 0;
#endif
}
int qarvtk_host_create_selected(QarvtkHub* connection,const char* ids,const char* folder,
    const QarvtkHostVolume* volumes,uint32_t count,QarvtkHost** result,char* error,uint32_t size){
  if(result)*result=nullptr;
#if QARVTK_HOST_DEVICE_SELECTION
  bool expected=false;
  if(!active.compare_exchange_strong(expected,true)){message(error,size,"Only one live stream may run in this Slicer process");return -1;}
  try{
    if(!connection||connection->owner!=GetCurrentThreadId()||!ids||!folder||!volumes||!count||count>32||!result)
      throw std::invalid_argument("Select connected receivers and 1 to 32 workspaces");
    const auto selected=qarvtk::selected_host_targets(connection->state->registry->snapshot(),ids);
    auto host=std::make_unique<QarvtkHost>();connection->state->retain();host->hub=connection->state;
    host->sdk_root=host->hub->sdk_root;host->target="selected";host->log_folder=folder;
    host->live_selection=std::make_shared<qarvtk::HostLiveSelection>();
    std::vector<qarvtk::HostLiveSelection::Choice> choices;
    for(const auto& peer:selected)choices.emplace_back(id_text(peer.id),peer.name+(peer.device.empty()?"":" ("+peer.device+")"));
    host->live_selection->replace(choices,[](std::size_t){});
    for(uint32_t i=0;i<count;++i){
      auto probe=qar_app_volume_init_default();qarvtk::apply_host_placement(probe,volumes[i].placement);
      QarvtkHost::Volume volume;volume.placement=volumes[i].placement;
      volume.name=volumes[i].name&&*volumes[i].name?volumes[i].name:"Slicer volume "+std::to_string(i+1);
      host->volumes.push_back(std::move(volume));
    }
    host->initialize();*result=host.release();return 0;
  }catch(const std::exception& e){message(error,size,e.what());active=false;return -1;}
   catch(...){message(error,size,"Cannot initialize selected receiver streams");active=false;return -1;}
#else
  message(error,size,"Connected receiver selection requires the dev.3184 SDK and r8.6 bridge");return -1;
#endif
}
int qarvtk_host_set_receivers(QarvtkHost* host,const char* ids,char* error,uint32_t size){
#if QARVTK_HOST_DEVICE_SELECTION
  try{
    if(!host||!ids)throw std::invalid_argument("No active receiver selection");
    host->gui();
    if(host->stopping||!host->hub||!host->hub->ready||!host->live_selection)
      throw std::runtime_error("The live QAROS connection is not ready");
    const auto available=host->hub->registry->snapshot();
    const auto reserved=host->live_selection->snapshot();
    std::vector<qarvtk::HostLiveSelection::Choice> choices;
    const std::string keys=ids;
    for(std::size_t offset=0;offset<keys.size();){
      const auto end=keys.find(',',offset);const auto key=keys.substr(offset,end==std::string::npos?end:end-offset);
      const auto known=std::find_if(reserved.begin(),reserved.end(),[&](const auto& s){return s.id==key;});
      const auto peer=std::find_if(available.begin(),available.end(),[&](const auto& p){return id_text(p.id)==key;});
      if(peer!=available.end()&&qarvtk::host_target_available(*peer)&&
         (peer->category==QAR_DEVICE_CATEGORY_HEADSET||peer->category==QAR_DEVICE_CATEGORY_VISUALIZER))
        choices.emplace_back(key,peer->name+(peer->device.empty()?"":" ("+peer->device+")"));
      else if(known!=reserved.end())choices.emplace_back(key,known->name);
      else throw std::runtime_error("A new receiver must be connected before it can be selected");
      if(end==std::string::npos)break;
      offset=end+1;if(offset==keys.size())throw std::invalid_argument("Invalid receiver identity");
    }
    host->live_selection->replace(choices,[&](std::size_t slot){host->prepare_target(slot);});
    return 0;
  }catch(const std::exception& e){message(error,size,e.what());return -1;}
   catch(...){message(error,size,"Cannot update live receivers");return -1;}
#else
  message(error,size,"Live receiver updates require the r8.7 dev.3184 bridge");return -1;
#endif
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
    if(h->stopping)return 0;
    const auto job=h->render_queue->acquire();if(!job)return 0;
    job->payload.request.serial=job->serial;
    *out=job->payload.request;return 1;
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
    const auto job=h->render_queue->active_frame(serial);
    if(wglGetCurrentContext()!=h->host_context)throw std::runtime_error("Host context changed before frame completion");
    h->seal_gpu_completion(job);
    job->payload.finished_at=Clock::now();
    h->render_queue->finish(serial);return 0;
  } catch(const std::exception& e){h->fail(e.what());return -1;}
}
int qarvtk_host_abort(QarvtkHost* h,uint64_t serial){
  if(!h)return -1;
  RenderQueue::Handle job;
  try{
    h->gui();std::lock_guard<std::mutex> lock(h->mutex);
    job=h->render_queue->active_frame_if(serial);
    if(!job)return 0;
    // Partial copies also retain their textures through GPU completion.
    qarvtk::WglBinding binding(h->host_dc,h->host_context);
    h->seal_gpu_completion(job);
    job->payload.finished_at=Clock::now();h->render_queue->abort(serial);return 0;
  }
  catch(const std::exception& e){
    if(GetCurrentThreadId()==h->owner&&job){
      job->payload.gpu_completion_failed=true;
      h->render_queue->abort(serial);
    }
    h->fail(e.what());return -1;
  }
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
