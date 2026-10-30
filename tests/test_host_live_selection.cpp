#include "qarvtk/host_live_selection.hpp"
#include "qarvtk/host_target_retirement.hpp"
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>

using namespace std::chrono_literals;
void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
int main(){
  using Selection=qarvtk::HostLiveSelection;
  Selection selected;
  const std::string a(32,'a'),b(32,'b'),c(32,'c'),d(32,'d');
  std::vector<std::size_t> prepared;
  auto prepare=[&](std::size_t slot){prepared.push_back(slot);};
  selected.replace({{a,"Headset A"}},prepare);
  selected.replace({{b,"Headset B"},{a,"Headset A"}},prepare);
  auto slots=selected.snapshot();
  require(slots[0].id==a&&slots[1].id==b&&slots[0].wanted&&slots[1].wanted,"Adding a headset moved the existing route");
  require(prepared==std::vector<std::size_t>({0,1}),"An existing graphics context was recreated");
  selected.replace({{b,"Headset B"},{c,"Visualizer"}},prepare);
  slots=selected.snapshot();
  require(!slots[0].wanted&&slots[0].id==a&&slots[1].id==b&&slots[2].id==c,"A retiring route was reused too early");
  selected.release_unwanted(0,a);
  selected.replace({{d,"Headset D"},{c,"Visualizer"},{b,"Headset B"}},prepare);
  slots=selected.snapshot();
  require(slots[0].id==d&&slots[1].id==b&&slots[2].id==c,"Retirement changed unrelated routes");
  selected.replace({{b,"B"}},prepare);
  selected.replace({{d,"D"},{b,"B"}},prepare);
  selected.release_unwanted(0,d);
  require(selected.snapshot()[0].wanted,"Rechecking a retiring peer lost its reservation");
  bool rejected=false;
  try{selected.replace({{a,"New"}},[](std::size_t){throw std::runtime_error("WGL failure");});}
  catch(const std::runtime_error&){rejected=true;}
  require(rejected&&selected.snapshot()[0].wanted&&selected.snapshot()[1].wanted,"Failed preparation changed live routes");
  selected.replace({},prepare);
  for(const auto& slot:selected.snapshot())require(!slot.wanted,"Clearing selection did not pause all routes");
  // A blocked SDK teardown runs independently; the previous receiver can
  // continue delivering frames and the caller can process more commands.
  qarvtk::HostTargetRetirement retirement;
  std::mutex mutex;std::condition_variable condition;bool release=false,entered=false;
  retirement.start([&]{
    std::unique_lock<std::mutex> lock(mutex);entered=true;condition.notify_all();
    require(condition.wait_for(lock,2s,[&]{return release;}),"Cleanup deadline");
  });
  {std::unique_lock<std::mutex> lock(mutex);require(condition.wait_for(lock,2s,[&]{return entered;}),"Cleanup did not start");}
  unsigned frames=0;
  for(unsigned i=0;i<20;++i){require(!retirement.ready(),"Unexpected teardown completion");++frames;}
  require(frames==20,"Unrelated frames stopped during receiver cleanup");
  {std::lock_guard<std::mutex> lock(mutex);release=true;}condition.notify_all();
  retirement.join();require(retirement.ready()&&retirement.error().empty(),"Cleanup did not complete");
  std::cout<<"PASS: stable live routes, lazy preparation, retirement/recheck, transactional updates, empty pause and nonblocking cleanup\n";
}
