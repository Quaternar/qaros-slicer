#include "qarvtk/gl.hpp"
#include "qarvtk/gl_copy.hpp"
#include "qarvtk/shutdown.hpp"
#include "sdk_config.hpp"
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_EXPOSE_NATIVE_WGL
#else
#define GLFW_EXPOSE_NATIVE_X11
#define GLFW_EXPOSE_NATIVE_GLX
#endif
#include <GLFW/glfw3native.h>
// Both inspected SDK headers emit Windows dllimport annotations on Linux.
// Dynamic loading uses dlsym there; suppress only that declaration annotation
// while including the unmodified SDK header (no ABI/type changes).
#if !defined(_WIN32) && defined(__GNUC__)
#pragma push_macro("__declspec")
#define __declspec(annotation)
#endif
#include <qar_streaming.h>
#if !defined(_WIN32) && defined(__GNUC__)
#pragma pop_macro("__declspec")
#endif
#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#if !QARVTK_MODERN_SDK
#include "legacy_peer_observer.hpp"
#include "legacy_frame_wait.hpp"
#endif
#if defined(_WIN32) && !QARVTK_SPLIT_OPENGL_PARAMS
#include "wgl_callbacks.hpp"
#endif

QAR_IMPLEMENT_DYNAMIC_LOADING()
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
namespace {
volatile std::sig_atomic_t stop_requested = 0;
void on_signal(int) { stop_requested = 1; }

double elapsed_ms(Clock::time_point start){
  return std::chrono::duration<double,std::milli>(Clock::now()-start).count();
}
struct Performance {
  Clock::time_point since=Clock::now();
  unsigned frames{},polls{},retries{},scene_reloads{};
  double begin{},render{},copy{},finish{},show{};
  double housekeeping{},frame_access{},metadata{},release{},present{};
  double poll_sleep{},retry_sleep{},poll_sleep_max{},frame_access_max{};
  double acquire{},acquire_max{};
  std::optional<Clock::time_point> acquire_started;
  void reset(){*this=Performance{};}
  void sleep(std::chrono::milliseconds duration,bool retry){
    const auto started=Clock::now();
    std::this_thread::sleep_for(duration);
    const double actual=elapsed_ms(started);
    if(retry){++retries;retry_sleep+=actual;}
    else {++polls;poll_sleep+=actual;poll_sleep_max=std::max(poll_sleep_max,actual);}
  }
  void ready(){
    if(acquire_started){
      const double wait=elapsed_ms(*acquire_started);
      acquire+=wait;acquire_max=std::max(acquire_max,wait);acquire_started.reset();
    }
  }
  void report(unsigned width,unsigned height){
    const double wall=elapsed_ms(since);
    if(!frames||wall<5000)return;
    // Exclusive phase totals. acquire is a separate, overlapping wall-time
    // observation from the first begin-frame poll until a ready frame arrives.
    const double other=std::max(0.0,wall-begin-render-copy-finish-show
        -housekeeping-frame_access-metadata-release-present-poll_sleep-retry_sleep);
    std::ostringstream line;line<<std::fixed<<std::setprecision(2)
      <<"PERF stereo_fps="<<frames*1000.0/wall<<"; frame_ms="<<wall/frames
      <<"; sdk_begin_ms="<<begin/frames<<"; vtk_render_ms="<<render/frames
      <<"; gpu_copy_ms="<<copy/frames<<"; gpu_finish_ms="<<finish/frames
      <<"; sdk_show_ms="<<show/frames<<"; sdk_frame_access_ms="<<frame_access/frames
      <<"; layout_pose_ms="<<metadata/frames<<"; frame_release_ms="<<release/frames
      <<"; housekeeping_ms="<<housekeeping/frames<<"; preview_ms="<<present/frames
      <<"; poll_sleep_ms="<<poll_sleep/frames<<"; retry_sleep_ms="<<retry_sleep/frames
      <<"; other_ms="<<other/frames
      <<"; idle_polls="<<polls<<"; eye_size="<<width<<'x'<<height
      <<"; frames="<<frames<<"; target60_frame_ms=16.67\n"
      <<"PACING acquire_wall_ms="<<acquire/frames<<"; acquire_max_ms="<<acquire_max
      <<"; poll_sleep_requested_ms=2.00; poll_sleep_avg_ms="<<(polls?poll_sleep/polls:0)
      <<"; poll_sleep_max_ms="<<poll_sleep_max<<"; sdk_frame_access_max_ms="<<frame_access_max
      <<"; retry_polls="<<retries<<"; scene_reloads="<<scene_reloads
      <<"; acquire_overlaps_PERF_phases=true\n";
    std::cout<<line.str();reset();
  }
};

struct FrameInfoDeleter {
  Performance* performance;
  void operator()(QarRenderFrameInfo* info) const {
    const auto started=Clock::now();
    qar_render_frame_info_handle_destroy(info);
    performance->release+=elapsed_ms(started);
  }
};

void checked(QarResult result, const char* operation) {
  if (qar_result_is_success(result)) return;
  char message[1024]{};
  qar_result_message(result, message, sizeof(message));
  throw std::runtime_error(std::string(operation) + " (" + std::to_string(result.code) + "): " + message);
}

struct Options {
  fs::path sdk, scene, identity{"qarvtk.onboarding-id.txt"}, invite, stop_file;
  unsigned long parent_pid{};
  std::string host, target{"visualizer"};
  double scale{1}; int frames{0}, timeout{60}; bool ras{}, preview{}, watch{};
};
void usage() {
  std::cout << "qarvtk-sender --sdk ROOT [--scene model.vtp|scene.vtm] [--meters-per-unit 1] [--ras]\n"
      "  [--preview] [--watch] [--frames N] [--timeout 60]\n"
      "  [--stop-file PATH] [--parent-pid PID] (plugin lifecycle)\n"
#if QARVTK_MODERN_SDK
      "  [--identity qarvtk.onboarding-id.txt] [--hub HOST]\n"
      "Pairing code is read from stdin on first run; later runs rejoin.\n"
#else
      "  [--invite-file invite.json] [--target visualizer|quest|hololens:HOST]\n"
      "Legacy session API: join supplied session or create one, then invite a viewer.\n"
#endif
      "--sdk-info prints the build's header fingerprint without loading the runtime.\n";
}
Options parse(int argc, char** argv) {
  Options o;
  for (int i=1; i<argc; ++i) {
    const std::string a=argv[i];
    auto value=[&]() { if (++i>=argc) throw std::invalid_argument("Missing value: "+a); return std::string(argv[i]); };
    if (a=="--sdk") o.sdk=value();
    else if (a=="--scene") o.scene=value();
    else if (a=="--meters-per-unit") o.scale=std::stod(value());
    else if (a=="--frames") o.frames=std::stoi(value());
    else if (a=="--timeout") o.timeout=std::stoi(value());
    else if (a=="--preview") o.preview=true;
    else if (a=="--ras") o.ras=true;
    else if (a=="--watch") o.watch=true;
    else if (a=="--stop-file") o.stop_file=value();
    else if (a=="--parent-pid") {
      const auto pid=std::stol(value());
      if (pid<=0) throw std::invalid_argument("Parent PID must be positive");
      o.parent_pid=static_cast<unsigned long>(pid);
    }
#if QARVTK_MODERN_SDK
    else if (a=="--identity") o.identity=value();
    else if (a=="--hub") o.host=value();
#else
    else if (a=="--invite-file") o.invite=value();
    else if (a=="--target") o.target=value();
#endif
    else throw std::invalid_argument("Unknown argument: "+a);
  }
  if (o.sdk.empty()) throw std::invalid_argument("--sdk ROOT is required");
  o.sdk=fs::absolute(o.sdk);
  if (o.frames<0 || o.timeout<1 || o.timeout>3600 || !std::isfinite(o.scale) || o.scale<=0)
    throw std::invalid_argument("Invalid frame limit, timeout or scale");
  if (o.watch && o.scene.empty()) throw std::invalid_argument("--watch requires --scene");
  return o;
}

// The application and encoder must select the same GPU. No silent adapter fallback.
QarGraphicsDeviceId graphics_device() {
  auto id=qar_graphics_device_id_default();
  if (!glfwExtensionSupported("GL_EXT_memory_object"))
    throw std::runtime_error("QAROS requires a shared-memory capable GPU; GL_EXT_memory_object is missing. Preview works without it.");
#ifdef _WIN32
  if (!glfwExtensionSupported("GL_EXT_memory_object_win32"))
    throw std::runtime_error("OpenGL driver cannot expose a Windows adapter LUID");
  using GetBytes = void (APIENTRY*)(GLenum, GLubyte*);
  auto get=reinterpret_cast<GetBytes>(glfwGetProcAddress("glGetUnsignedBytevEXT"));
  if (!get) throw std::runtime_error("Cannot query OpenGL GPU LUID");
  id.id_type=QAR_GPU_DEVICE_ID_TYPE_LUID;
  get(0x9599 /* GL_DEVICE_LUID_EXT */, reinterpret_cast<GLubyte*>(&id.luid));
#else
  GLint devices=0;
  glGetIntegerv(0x9596 /* GL_NUM_DEVICE_UUIDS_EXT */,&devices);
  if (devices!=1) throw std::runtime_error("Exactly one OpenGL device UUID is required; multi-device contexts are unsupported");
  using GetBytes = void (APIENTRY*)(GLenum, GLuint, GLubyte*);
  auto get=reinterpret_cast<GetBytes>(glfwGetProcAddress("glGetUnsignedBytei_vEXT"));
  if (!get) throw std::runtime_error("Cannot query OpenGL GPU UUID");
  id.id_type=QAR_GPU_DEVICE_ID_TYPE_UUID;
  get(0x9597 /* GL_DEVICE_UUID_EXT */, 0, id.uuid);
#endif
  if (glGetError()!=GL_NO_ERROR) throw std::runtime_error("OpenGL GPU identity query failed");
  return id;
}

struct NativeContext {
#ifdef _WIN32
  HWND window{}; HDC dc{};
#if QARVTK_SPLIT_OPENGL_PARAMS
  QarStreamParamsOpenGlWgl params=qar_stream_params_opengl_wgl_default();
#else
  QarStreamParamsOpenGl params=qar_stream_params_opengl_default();
  std::unique_ptr<qarvtk::WglCallbacks> callbacks;
#endif
  explicit NativeContext(qarvtk::Renderer& renderer) {
    window=glfwGetWin32Window(renderer.sdk_window()); dc=GetDC(window);
#if QARVTK_SPLIT_OPENGL_PARAMS
    params.device_context=dc;
#else
    params.context_type=QAR_OPENGL_CONTEXT_TYPE_WGL;
    params.display_or_device_context=dc;
#endif
    params.context=glfwGetWGLContext(renderer.sdk_window());
    try {
      if (!dc || !params.context) throw std::runtime_error("WGL shared context unavailable");
#if !QARVTK_SPLIT_OPENGL_PARAMS
      callbacks=std::make_unique<qarvtk::WglCallbacks>(dc,static_cast<HGLRC>(params.context));
      callbacks->configure(params);
      const auto probe=callbacks->probe_on_worker();
      std::cout << "WGL worker probe: GL " << probe.version
                << "; glGetStringi=" << probe.get_string_i
                << "; Win32 memory import=" << probe.memory_import
                << "; semaphore import=" << probe.semaphore_import << '\n';
      if (!probe.get_string_i) throw std::runtime_error("Shared WGL worker context cannot resolve glGetStringi");
#endif
    } catch (...) { if (dc) { ReleaseDC(window,dc); dc=nullptr; } throw; }
  }
  ~NativeContext() { if (dc) ReleaseDC(window,dc); }
#else
#if QARVTK_SPLIT_OPENGL_PARAMS
  QarStreamParamsOpenGlGlx params=qar_stream_params_opengl_glx_default();
#else
  QarStreamParamsOpenGl params=qar_stream_params_opengl_default();
#endif
  explicit NativeContext(qarvtk::Renderer& renderer) {
    auto display=glfwGetX11Display(); auto drawable=glfwGetGLXWindow(renderer.sdk_window());
#if QARVTK_SPLIT_OPENGL_PARAMS
    params.display=display; params.drawable=drawable;
#else
    params.context_type=QAR_OPENGL_CONTEXT_TYPE_GLX;
    params.display_or_device_context=display; params.draw_surface=drawable;
#endif
    params.context=glfwGetGLXContext(renderer.sdk_window());
    if (!display || !drawable || !params.context)
      throw std::runtime_error("GLX shared context unavailable; run in an X11 session");
  }
#endif
  void report_callbacks() const {
#if defined(_WIN32) && !QARVTK_SPLIT_OPENGL_PARAMS
    callbacks->report();
#endif
  }
};

#if QARVTK_MODERN_SDK
struct Requests { std::mutex mutex; std::optional<QarPeerId> first; };
void on_request(QarRenderStreamRequest* request, void* context) {
  auto& state=*static_cast<Requests*>(context);
  QarPeerId peer=qar_peer_id_default();
  auto result=qar_render_request_get_target_peer_id(request,&peer);
  if (qar_result_is_success(result)) {
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.first) state.first=peer; // This prototype serves one viewer per process.
  }
  qar_render_request_handle_destroy(request);
}
#else
void on_peer_update(QarPeerSpec* spec, void* context) noexcept {
  if (!spec || !context) return;
  QarPeerId peer{}; QarAppState state=QAR_APP_STATE_UNKNOWN;
  if (qar_result_is_success(qar_peer_spec_get_id(spec,&peer)) &&
      qar_result_is_success(qar_peer_spec_get_app_state(spec,&state)))
    static_cast<qarvtk::LegacyPeerObserver*>(context)->record(peer,state);
  // The callback borrows the spec; retain only copied id/state values.
}
const char* peer_state_name(QarAppState state) {
  switch (state) {
    case QAR_APP_STATE_UNKNOWN: return "UNKNOWN";
    case QAR_APP_STATE_INITIALIZING: return "INITIALIZING";
    case QAR_APP_STATE_RUNNING: return "RUNNING";
    case QAR_APP_STATE_SHUTTING_DOWN: return "SHUTTING_DOWN";
    default: return "unrecognized";
  }
}
#endif

struct Sdk {
  bool loaded{}, initialized{}, volume_created{};
  QarRuntime* runtime{}; QarSession* session{}; QarRenderSender* sender{};
  QarCancelToken* operation{}; QarCancelToken* subscription{};
  QarAppVolumeId volume=qar_app_volume_id_default();
#if QARVTK_MODERN_SDK
  Requests requests; // Must outlive cancellation and session/runtime destruction.
#else
  qarvtk::LegacyPeerObserver peers; // Same callback lifetime requirement.
#endif
  ~Sdk() {
    if (subscription) qar_cancel_token_cancel(subscription);
    if (sender) qar_render_stream_handle_destroy(sender);
    if (volume_created) qar_app_volumes_close_volume(session,&volume);
    if (session) {
#if QARVTK_MODERN_SDK
      qar_session_handle_destroy(session);
#else
      qar_session_destroy(session); // Leave and tear down callback workers.
#endif
    }
    // Release this sender's handle without shutting down the shared runtime.
    if (runtime) qar_runtime_handle_destroy(runtime);
    if (subscription) qar_cancel_token_handle_destroy(subscription);
    if (operation) qar_cancel_token_handle_destroy(operation);
    if (initialized) qar_library_destroy();
    if (loaded) qar_library_unload();
  }
  void deadline(int seconds) {
    if (operation) qar_cancel_token_handle_destroy(operation);
    operation=nullptr;
    checked(qar_cancel_token_create_with_timeout(&operation,static_cast<uint32_t>(seconds*1000)),"create timeout");
  }
  void open(const Options& o) {
#ifdef _WIN32
    auto library=o.sdk/"bin"/"qar-streaming-c.dll";
#else
    auto library=o.sdk/"bin"/"libqar-streaming-c.so";
#endif
    if (!fs::is_regular_file(library)) throw std::runtime_error("Missing runtime library: "+library.string());
    if (!qar_library_load(library.string().c_str())) throw std::runtime_error("Cannot load SDK; use the complete package matching the build header");
    loaded=true;
    auto init=qar_library_init_default(); init.enable_console_logging=true;
    checked(qar_library_init(&init),"library init"); initialized=true;
    const auto folder=(o.sdk/"bin").string();
    auto ri=qar_runtime_init_default(); ri.runtime_binaries_folder_path=folder.c_str();
    checked(qar_runtime_create(&ri,&runtime),"runtime create");
    if (!runtime) throw std::runtime_error("Runtime returned a null handle");
  }

  void connect(const Options& o, QarGraphicsDeviceId& adapter) {
    deadline(o.timeout);
#if QARVTK_MODERN_SDK
    auto identity=qar_onboarding_id_default();
    if (fs::exists(o.identity)) {
      std::ifstream file(o.identity); std::string text; std::getline(file,text);
      checked(qar_uuid_from_string(text.c_str(),identity.data),"read onboarding identity");
      auto init=qar_rejoin_init_default(); init.onboarding_id=identity;
      init.presentation.display_name="QAROS VTK"; init.header.next=&adapter.header;
      auto result=qar_runtime_rejoin(runtime,&init,nullptr,nullptr,operation,&session);
      if (qar_result_is_success(result)) return;
      if (!qar_result_has_code(result,QAR_STATUS_ONBOARDING_SESSION_NOT_FOUND)) checked(result,"rejoin Hub");
    }
    std::cout << "Hub pairing code: " << std::flush;
    std::string code; if (!std::getline(std::cin,code) || code.empty()) throw std::runtime_error("Pairing code is required on first run");
    deadline(o.timeout); // Human input time must not consume the SDK timeout.
    auto init=qar_onboard_init_default(); init.presentation.display_name="QAROS VTK";
    auto code_ext=qar_onboard_code_ext_default(); code_ext.code=code.c_str();
    auto host=qar_onboard_host_ext_default(); host.hostname=o.host.c_str();
    init.header.next=&code_ext.header; code_ext.header.next=&adapter.header;
    if (!o.host.empty()) { adapter.header.next=&host.header; }
    checked(qar_runtime_onboard(runtime,&init,nullptr,nullptr,operation,&identity,&session),"onboard Hub");
    adapter.header.next=nullptr;
    char text[64]{}; checked(qar_uuid_to_string(identity.data,text,sizeof(text)),"serialize identity");
    std::ofstream file(o.identity); file<<text<<'\n'; file.close();
    if (!file) throw std::runtime_error("Cannot persist onboarding identity: "+o.identity.string());
#else
    std::vector<uint8_t> bytes;
    if (!o.invite.empty()) {
      std::ifstream input(o.invite,std::ios::binary);
      if (!input) throw std::runtime_error("Cannot read invite file");
      if (fs::file_size(o.invite)>1024*1024) throw std::runtime_error("Invite file exceeds 1 MiB");
      bytes.assign(std::istreambuf_iterator<char>(input),{});
      QarSessionInvite* raw{};
      checked(qar_session_invite_deserialize(bytes.data(),bytes.size(),&raw),"read legacy invite");
      std::unique_ptr<QarSessionInvite,decltype(&qar_session_invite_handle_destroy)> invite(raw,qar_session_invite_handle_destroy);
      auto runtime_join=qar_runtime_join_init_default(); runtime_join.header.next=&adapter.header;
      checked(qar_runtime_join_session(runtime,&runtime_join,invite.get()),"attach runtime to invited session");
    } else {
      QarSessionInvite* raw{};
      auto init=qar_session_create_init_default(); init.header.next=&adapter.header;
      checked(qar_runtime_create_session(runtime,&init,&raw),"create legacy session");
      std::unique_ptr<QarSessionInvite,decltype(&qar_session_invite_handle_destroy)> invite(raw,qar_session_invite_handle_destroy);
      // Invites are small JSON blobs; a fixed bounded buffer avoids printing credentials.
      bytes.resize(1024*1024); size_t written{};
      checked(qar_session_invite_serialize(invite.get(),bytes.data(),bytes.size(),&written),"serialize invite");
      if (written>bytes.size()) throw std::runtime_error("Invalid invite size");
      bytes.resize(written);
    }
    if (bytes.empty()) throw std::runtime_error("Empty session invite");
    auto join=qar_session_join_init_default(); join.header.next=&adapter.header;
    join.invite_data=bytes.data(); join.invite_data_size=bytes.size(); join.peer_spec_init.display_name="QAROS VTK";
    checked(qar_session_join(&join,&session),"join legacy session");
#endif
    if (!session) throw std::runtime_error("SDK returned a null session");
  }
  void create_volume(const QarPeerId* peer=nullptr) {
    auto init=qar_app_volume_init_default(); init.display_name="VTK surface";
    init.pose.position.z=-1.5F; init.size={1.2F,1.2F,1.2F}; init.app_scale=1;
#if QARVTK_MODERN_SDK
    // Unique per process run; closing this volume cannot close another sender's volume.
    const auto name="vtk.prototype."+std::to_string(Clock::now().time_since_epoch().count());
    init.common_name=name.c_str();
    checked(qar_app_volumes_get_or_create(session,&init,&volume),"create app volume");
#else
    if (!peer) throw std::logic_error("Observe the invited viewer before creating a volume");
    const QarPeerId* initial_peers[]={peer};
    init.initial_peers=initial_peers; init.initial_peer_count=1;
    checked(qar_app_volumes_add_volume(session,&init,&volume),"create app volume");
#endif
    volume_created=true;
  }
  QarPeerId target(const Options& o, qarvtk::Renderer& renderer, const qarvtk::Shutdown& shutdown) {
#if QARVTK_MODERN_SDK
    checked(qar_cancel_token_create(&subscription),"create subscription token");
    checked(qar_render_sender_subscribe_requests(session,on_request,&requests,subscription),"subscribe render requests");
    std::cout << "Open this app's volume in a Hub viewer; waiting for its render request...\n";
    const auto end=Clock::now()+std::chrono::seconds(o.timeout);
    while (!stop_requested && !shutdown.requested() && !renderer.closing() && Clock::now()<end) {
      { std::lock_guard<std::mutex> lock(requests.mutex); if (requests.first) return *requests.first; }
      renderer.poll(); std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    throw std::runtime_error("No viewer request before timeout or shutdown");
#else
    checked(qar_cancel_token_create(&subscription),"create peer subscription token");
    checked(qar_peer_subscribe_updates(session,on_peer_update,&peers,subscription),"subscribe peer updates");
    char connection[1024]{};
    if (o.target=="visualizer") checked(qar_session_invite_connection_string_visualizer(connection,sizeof(connection)),"visualizer connection");
    else if (o.target=="quest") checked(qar_session_invite_connection_string_quest(connection,sizeof(connection)),"Quest connection");
    else if (o.target.rfind("hololens:",0)==0) checked(qar_session_invite_connection_string_hololens(o.target.substr(9).c_str(),connection,sizeof(connection)),"HoloLens connection");
    else throw std::invalid_argument("Target must be visualizer, quest or hololens:HOST");
    auto init=qar_session_invite_peer_init_default(); init.connection_string=connection;
    QarPeerId peer=qar_peer_id_default(); deadline(o.timeout);
    std::cout << "Inviting " << o.target << " viewer...\n";
    checked(qar_session_invite_peer(session,&init,&peer,operation),"invite target");
    char id[64]{}; checked(qar_uuid_to_string(peer.data,id,sizeof(id)),"format invited peer id");
    std::cout << "Invited viewer " << id << "; waiting for its peer update (UNKNOWN is accepted)...\n";
    const auto stopping=[&] { return stop_requested || shutdown.requested() || renderer.closing(); };
    const auto end=Clock::now()+std::chrono::seconds(o.timeout);
    while (!stopping() && Clock::now()<end) {
      if (const auto state=peers.state(peer)) {
        if (*state==QAR_APP_STATE_SHUTTING_DOWN) throw std::runtime_error("Invited viewer is shutting down");
        std::cout << "Invited viewer observed: " << peer_state_name(*state)
                  << "; allowing 2 seconds for receiver subscriptions...\n";
        // This local session has no persistent storage. Match the working BANC
        // bridge: observe the exact invited id, let subscriptions settle, then
        // publish the volume with that peer assigned in its initial update.
        const auto settled=Clock::now()+std::chrono::seconds(2);
        while (!stopping() && Clock::now()<settled) {
          renderer.poll(); std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (stopping()) throw std::runtime_error("Viewer startup interrupted");
        if (peers.state(peer)==QAR_APP_STATE_SHUTTING_DOWN)
          throw std::runtime_error("Invited viewer shut down during startup");
        return peer;
      }
      renderer.poll(); std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (stopping()) throw std::runtime_error("Viewer startup interrupted");
    throw std::runtime_error(std::string("Invited viewer ")+id+" was not observed before timeout; no volume was published");
#endif
  }
};

qarvtk::Frame frame_layout(const QarVideoFrameOpenGl& frame) {
  if (frame.textures_count>QAR_MAX_FRAME_TEXTURES || frame.texture_views_count>QAR_MAX_FRAME_VIEWS)
    throw std::runtime_error("Invalid SDK frame array sizes");
  qarvtk::Frame f;
  for (size_t i=0;i<frame.textures_count;++i) {
    const auto& t=frame.textures[i]; f.textures.push_back({t.texture,t.size.width,t.size.height,t.size.array_layers});
  }
  for (size_t i=0;i<frame.texture_views_count;++i) {
    const auto& v=frame.texture_views[i];
    if (v.eye!=QAR_VIDEO_FRAME_VIEW_EYE_LEFT && v.eye!=QAR_VIDEO_FRAME_VIEW_EYE_RIGHT)
      throw std::runtime_error("Only stereo LEFT/RIGHT views supported");
    if (v.data_type!=QAR_VIDEO_FRAME_VIEW_TYPE_COLOR && v.data_type!=QAR_VIDEO_FRAME_VIEW_TYPE_DEPTH)
      throw std::runtime_error("Unsupported frame plane");
    const bool depth=v.data_type==QAR_VIDEO_FRAME_VIEW_TYPE_DEPTH;
    if ((!depth && v.texture_format!=QAR_PIXEL_FORMAT_R8G8B8A8) ||
        (depth && v.texture_format!=QAR_PIXEL_FORMAT_D32_FLOAT && v.texture_format!=QAR_PIXEL_FORMAT_R32_FLOAT))
      throw std::runtime_error("Unsupported negotiated pixel format");
    f.views.push_back({v.eye==QAR_VIDEO_FRAME_VIEW_EYE_LEFT?qarvtk::Eye::left:qarvtk::Eye::right,
       depth?qarvtk::Plane::depth:qarvtk::Plane::color,v.texture_index,v.start_x,v.start_y,v.end_x,v.end_y,v.array_layer_index});
  }
  return f;
}
bool transient(QarResult result) {
  return qar_result_has_code(result,QAR_STATUS_MEDIA_STREAMING_STREAM_IS_RECONNECTING) ||
      qar_result_has_code(result,QAR_STATUS_MEDIA_STREAMING_NO_ACTIVE_TRANSFERS)
#if QARVTK_MODERN_SDK
      || qar_result_has_code(result,QAR_STATUS_RENDERING_PRODUCER_APP_VOLUME_UNRESOLVED)
#endif
      ;
}
}

int main(int argc,char** argv) { try {
  for (int i=1;i<argc;++i) {
    if (std::string(argv[i])=="--help") { usage(); return 0; }
    if (std::string(argv[i])=="--sdk-info") {
      std::cout << "{\"header_sha256\":\"" << QARVTK_SDK_SHA256 << "\",\"modern_api\":" << QARVTK_MODERN_SDK << ",\"plugin_protocol\":2}\n"; return 0;
    }
  }
  const auto options=parse(argc,argv);
  std::cout.setf(std::ios::unitbuf);
  const qarvtk::Shutdown shutdown(options.stop_file,options.parent_pid);
  if (shutdown.requested()) return 0;
  std::signal(SIGINT,on_signal); std::signal(SIGTERM,on_signal);
  qarvtk::Renderer renderer(options.preview); std::cout<<renderer.capabilities()<<'\n';
  if (!options.scene.empty()) renderer.load_scene(options.scene.string(),options.scale,options.ras);
  auto adapter=graphics_device();
  qarvtk::GlCopy copy;
  NativeContext native(renderer); // Outlives SDK; shared context belongs to worker threads.
  Sdk sdk; sdk.open(options); sdk.connect(options,adapter);
#if QARVTK_MODERN_SDK
  sdk.create_volume(); // Hub viewers request an existing volume.
#endif
  if (shutdown.requested()) return 0;
  const auto peer=sdk.target(options,renderer,shutdown);
#if QARVTK_MODERN_SDK
  checked(qar_app_volumes_update_used_by_peers(sdk.session,&sdk.volume,&peer,1,nullptr,0),"attach viewer to volume");
#else
  std::cout << "Creating app volume for the observed viewer...\n";
  sdk.create_volume(&peer);
#endif
  auto init=qar_render_sender_init_default(); init.graphics_api=QAR_GRAPHICS_API_OPENGL;
  init.header.next=&native.params.header; init.peer_id=peer; init.app_volume_id=&sdk.volume;
  init.enable_auto_reconnects=true; init.texture_layout=QAR_FRAME_LAYOUT_SEPARATED_TEXTURES;
  init.color_format=QAR_PIXEL_FORMAT_R8G8B8A8; init.depth_format=QAR_PIXEL_FORMAT_D32_FLOAT;
  init.frame_views_count=4;
  for (unsigned i=0;i<4;++i) {
    init.frame_views[i]=qar_render_frame_view_default();
    init.frame_views[i].eye=i<2?QAR_VIDEO_FRAME_VIEW_EYE_LEFT:QAR_VIDEO_FRAME_VIEW_EYE_RIGHT;
    init.frame_views[i].data_type=i%2?QAR_VIDEO_FRAME_VIEW_TYPE_DEPTH:QAR_VIDEO_FRAME_VIEW_TYPE_COLOR;
    init.frame_views[i].texture_index=i;
  }
  sdk.deadline(options.timeout);
  std::cout << "Creating OpenGL sender...\n";
  const auto created=qar_render_sender_create(sdk.session,&init,sdk.operation,&sdk.sender);
  native.report_callbacks();
  checked(created,"create OpenGL sender");
  if (!sdk.sender) throw std::runtime_error("SDK returned a null sender");
  std::cout << "OpenGL sender created; waiting for the viewer's first frame request...\n";
  const qarvtk::Clip clip{.05,20};
  auto last_frame=Clock::now(),last_watch=last_frame;
#if !QARVTK_MODERN_SDK
  // Match the working BANC bridge: creation precedes primary-transfer readiness.
  // Keep this wait interruptible and inside the existing first-frame timeout.
  std::cout << "Allowing 1.2 seconds for the primary transfer to settle...\n";
  const auto settle_until=last_frame+std::chrono::milliseconds(1200);
  while (!stop_requested && !shutdown.requested() && !renderer.closing() &&
         Clock::now()<settle_until && Clock::now()-last_frame<std::chrono::seconds(options.timeout)) {
    renderer.poll(); std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  auto reported_wait=qarvtk::LegacyFrameWait::none;
  auto last_wait_report=last_frame;
#endif
  auto scene_time=options.scene.empty()?fs::file_time_type{}:fs::last_write_time(options.scene);
  Performance performance;
  std::cout<<"Performance logging every 5 seconds after warm-up; CPU wall times per stereo frame. "
      "PERF phases are exclusive; PACING acquire_wall overlaps polling/sleep/housekeeping. "
      "GPU stalls can appear in render/copy/finish/frame_access; asynchronous encoding is not timed here.\n";
  int sent=0;
  while (!options.frames || sent<options.frames) {
    const auto housekeeping_started=Clock::now();
    if(stop_requested || shutdown.requested() || renderer.closing())break;
    renderer.poll();
    if (options.watch && Clock::now()-last_watch>std::chrono::milliseconds(250)) {
      last_watch=Clock::now(); std::error_code ec; const auto changed=fs::last_write_time(options.scene,ec);
      if (!ec && changed!=scene_time) {
        try { renderer.load_scene(options.scene.string(),options.scale,options.ras); scene_time=changed; ++performance.scene_reloads; std::cout<<"Scene reloaded\n"; }
        catch (const std::exception& e) { std::cerr<<"Keeping previous scene: "<<e.what()<<'\n'; }
      }
    }
    if (Clock::now()-last_frame>std::chrono::seconds(options.timeout)) {
#if !QARVTK_MODERN_SDK
      if (reported_wait==qarvtk::LegacyFrameWait::first_pose)
        throw std::runtime_error("No first frame before timeout; observed SDK condition: RenderingProducerMissingPoseData (801). No stereo frame was submitted.");
#endif
      throw std::runtime_error("No frame received before timeout; check viewer and GPU adapter");
    }
    performance.housekeeping+=elapsed_ms(housekeeping_started);
    QarRenderFrameInfo* raw{}; bool ready=false;
    const auto begin_started=Clock::now();
    if(!performance.acquire_started)performance.acquire_started=begin_started;
    const auto result=qar_render_sender_try_begin_frame(sdk.sender,&raw,&ready);
    performance.begin+=elapsed_ms(begin_started);
    std::unique_ptr<QarRenderFrameInfo,FrameInfoDeleter> info(raw,FrameInfoDeleter{&performance});
#if !QARVTK_MODERN_SDK
    const auto wait=qarvtk::legacy_begin_frame_wait(result,sent==0);
    if (wait!=qarvtk::LegacyFrameWait::none) {
      const auto now=Clock::now();
      if (wait!=reported_wait || now-last_wait_report>=std::chrono::seconds(5)) {
        const char* reason=wait==qarvtk::LegacyFrameWait::first_pose
            ? "Waiting for first viewer pose (RenderingProducerMissingPoseData, 801)"
            : "Primary transfer is paused";
        std::cout << reason << "; retrying every 100 ms, "
                  << std::chrono::duration_cast<std::chrono::seconds>(now-last_frame).count()
                  << "/" << options.timeout << " seconds since last submitted frame/start...\n";
        reported_wait=wait; last_wait_report=now;
      }
      performance.sleep(std::chrono::milliseconds(100),true); continue;
    }
#endif
    const bool retry=transient(result);
    if (retry || (qar_result_is_success(result) && !ready)) {
      performance.sleep(std::chrono::milliseconds(2),retry); continue;
    }
    checked(result,"begin frame"); if (!info) throw std::runtime_error("Ready frame has no view metadata");
    performance.ready();
#if !QARVTK_MODERN_SDK
    if (reported_wait!=qarvtk::LegacyFrameWait::none) {
      std::cout << "Viewer frame data is ready; continuing.\n";
      reported_wait=qarvtk::LegacyFrameWait::none;
    }
#endif
    if (!sent) std::cout << "First frame request received; reading OpenGL frame layout...\n";
    const auto access_started=Clock::now();
    auto access=qar_video_frame_opengl_default(); checked(qar_render_sender_frame_opengl(sdk.sender,&access),"get OpenGL frame");
    const double access_ms=elapsed_ms(access_started);
    performance.frame_access+=access_ms;performance.frame_access_max=std::max(performance.frame_access_max,access_ms);
    const auto layout_started=Clock::now();
    const auto frame=frame_layout(access); const auto views=qarvtk::validate_stereo(frame);
    performance.metadata+=elapsed_ms(layout_started);
    if (!sent) std::cout << "First frame layout validated: " << frame.textures.size()
                         << " textures, " << frame.views.size() << " views.\n";
    for (const auto& eye:views) {
      const auto pose_started=Clock::now();
      QarPose pose=qar_pose_default(); QarFov fov=qar_fov_default();
      checked(qar_render_frame_info_get_view_pose(info.get(),eye.color,&pose),"get view pose");
      checked(qar_render_frame_info_get_view_fov(info.get(),eye.color,&fov),"get view FOV");
      if (!sent) std::cout << "First frame color view " << eye.color
                           << ": pose/FOV received; rendering and copying color/depth...\n";
      const qarvtk::Pose camera{{pose.position.x,pose.position.y,pose.position.z},
          {pose.orientation.x,pose.orientation.y,pose.orientation.z,pose.orientation.w}};
      const qarvtk::Fov angles{fov.angle_left,fov.angle_right,fov.angle_up,fov.angle_down};
      const auto& view=frame.views[eye.color];
      performance.metadata+=elapsed_ms(pose_started);
      const auto render_started=Clock::now();
      auto source=renderer.render(camera,angles,clip,qarvtk::width(view),qarvtk::height(view));
      performance.render+=elapsed_ms(render_started);
      const auto copy_started=Clock::now();
      copy.copy(source,frame,eye,!sent);
      performance.copy+=elapsed_ms(copy_started);
    }
    const auto finish_started=Clock::now();
    copy.finish(); // glFinish: correctness baseline before SDK's graphics thread consumes textures.
    performance.finish+=elapsed_ms(finish_started);
    if (!sent) std::cout << "First stereo frame rendered and copied; submitting to SDK...\n";
    auto show=qar_render_frame_show_default();
    show.rendered_near_far={static_cast<float>(clip.near_plane),static_cast<float>(clip.far_plane)}; show.depth_scale=1;
    const auto show_started=Clock::now();
    checked(qar_render_sender_show_frame(sdk.sender,&show),"show frame");
    performance.show+=elapsed_ms(show_started);
    last_frame=Clock::now(); ++sent;
    // Include release and optional presentation in the same measurement window
    // as their frame. The SDK has already accepted show_frame at this point.
    info.reset();
    if (options.preview) {
      const auto present_started=Clock::now();renderer.present();
      performance.present+=elapsed_ms(present_started);
    }
    if(sent==1)performance.reset(); // Exclude context/shader/encoder startup.
    else {++performance.frames;const auto& view=frame.views[views[0].color];
      performance.report(qarvtk::width(view),qarvtk::height(view));}
    if (sent==1 || sent%300==0) std::cout<<"Submitted "<<sent<<" stereo frames\n";
  }
  std::cout<<"Stopped after "<<sent<<" stereo frames\n";
  return sent>0?0:2;
} catch (const std::exception& e) { std::cerr<<"ERROR: "<<e.what()<<'\n'; return 1; } }
