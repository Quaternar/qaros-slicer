// Test double for process/host integration tests. Does not use QAROS or a GPU.
#include "qarvtk/shutdown.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
int main(int argc,char** argv) {
  std::string stop_file, identity;
  unsigned long parent=0;
  for(int i=1;i<argc;++i) {
    const std::string arg=argv[i];
    if(arg=="--sdk-info") {
      std::cout << "{\"header_sha256\":\"f756a760e5658cbc187a5572c246521344a909c60bb58dff621fe307f3b2ea69\",\"modern_api\":1,\"plugin_protocol\":2}\n";
      return 0;
    }
    if(arg=="--stop-file" && i+1<argc) stop_file=argv[++i];
    else if(arg=="--parent-pid" && i+1<argc) parent=std::stoul(argv[++i]);
    else if(arg=="--identity" && i+1<argc) identity=argv[++i];
  }
  if(stop_file.empty()) return 2;
  std::string code; std::getline(std::cin,code);
  if(!identity.empty() && !code.empty()) { std::ofstream file(identity); file << "fixture-id\n"; }
  qarvtk::Shutdown shutdown(stop_file,parent);
  std::cout << "FIXTURE_READY\n" << std::flush;
  const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(20);
  while(!shutdown.requested() && std::chrono::steady_clock::now()<end)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  std::cout << "FIXTURE_STOPPED\n";
  return shutdown.requested()?0:3;
}
