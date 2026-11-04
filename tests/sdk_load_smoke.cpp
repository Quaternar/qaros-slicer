// Resolve the actual installed SDK and read its identity without initialization,
// joining a session, starting the Hub or touching persistent QAROS data.
#include <qar_streaming.h>
#include <cstdio>

QAR_IMPLEMENT_DYNAMIC_LOADING()

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "Usage: qarvtk-sdk-load-smoke <absolute SDK DLL path>\n");
    return 2;
  }
  if (!qar_library_load(argv[1])) {
    std::fprintf(stderr, "Cannot resolve the installed SDK with this header\n");
    return 1;
  }
  QarRuntimeInfo info{};
  const auto result = qar_runtime_info(&info);
  const bool ok = qar_result_is_success(result) && info.version && *info.version;
  if (ok) std::printf("PASS: installed SDK loaded; runtime %s\n", info.version);
  else std::fprintf(stderr, "Cannot read installed SDK identity (status %d)\n", int(result.code));
  qar_library_unload();
  return ok ? 0 : 1;
}
