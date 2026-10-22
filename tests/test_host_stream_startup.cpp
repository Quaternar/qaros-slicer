#include "qarvtk/host_stream_startup.hpp"
#include "qarvtk/host_stream_scheduler.hpp"
#include "qarvtk/host_stream_routes.hpp"
#include <array>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>

using namespace std::chrono_literals;
using Startup=qarvtk::HostStreamStartup;
void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
template<class Predicate> void until(Predicate ready){
  const auto deadline=std::chrono::steady_clock::now()+3s;
  while(!ready()){
    require(std::chrono::steady_clock::now()<deadline,"Startup test deadline exceeded");
    std::this_thread::sleep_for(1ms);
  }
}
int main(){
  // Regression: receiver refuses to negotiate stream 2 until stream 1 has
  // delivered video. A synchronous all-senders-before-frames startup deadlocks.
  {
    Startup startup(2);qarvtk::HostStreamScheduler scheduler;
    std::mutex mutex;std::condition_variable condition;bool first_frame=false;
    int published[2]{};
    startup.start([&](std::size_t i){
      if(i==1){
        std::unique_lock<std::mutex> lock(mutex);
        require(condition.wait_for(lock,2s,[&]{return first_frame;}),"Second sender starved the first frame");
      }
      published[i]=42+static_cast<int>(i); // sender/layout publication surrogate
    },[]{return false;});
    until([&]{return startup.state(1)==Startup::State::connecting;});
    auto selected=scheduler.select(2,[&](std::size_t i){return startup.ready(i);});
    require(selected==0&&published[0]==42,"First sender unavailable during second negotiation");
    {std::lock_guard<std::mutex> lock(mutex);first_frame=true;}condition.notify_one();
    until([&]{return startup.done();});startup.join();
    require(startup.ready(1)&&published[1]==43,"Second sender did not recover after first video");
  }
  // A second-sender timeout must not destroy the first or prevent later slots.
  {
    Startup startup(3);
    startup.start([](std::size_t i){if(i==1)throw std::runtime_error("QueryMatchingStream timeout");},[]{return false;});
    until([&]{return startup.done();});startup.join();
    require(startup.ready(0)&&startup.ready(2),"Connection failure lost a healthy stream");
    require(startup.error(1)=="QueryMatchingStream timeout","Per-volume failure detail lost");
    qarvtk::HostStreamScheduler scheduler;
    for(int n=0;n<12;++n)require(scheduler.select(3,[&](std::size_t i){return startup.ready(i);})==(n%2?2:0),"Failed stream entered render loop");
  }
  // Stop cancels a pending create, joins it, and prevents another create before
  // the caller destroys its SDK session/graphics state.
  {
    Startup startup(3);std::mutex mutex;std::condition_variable condition;
    bool cancelled=false;int attempts=0;std::atomic<bool> exited{false};
    startup.start([&](std::size_t){
      ++attempts;std::unique_lock<std::mutex> lock(mutex);
      condition.wait_for(lock,2s,[&]{return cancelled;});exited=true;
      throw std::runtime_error("cancelled");
    },[]{return false;});
    until([&]{return startup.state(0)==Startup::State::connecting;});
    startup.request_stop();
    {std::lock_guard<std::mutex> lock(mutex);cancelled=true;}condition.notify_one();
    startup.join();
    require(exited&&attempts==1&&startup.done(),"Pending connection outlived shutdown");
    require(startup.state(1)==Startup::State::pending,"Stop started another SDK connection");
  }
  // Two targets share two source workspaces, but own independent senders and
  // poses. A blocked Visualizer negotiation must not stop headset frames.
  {
    qarvtk::HostStreamRoutes routes(2,2);qarvtk::HostStreamScheduler scheduler;
    std::array<Startup,2> targets{Startup(2),Startup(2)};
    std::array<int,4> camera{};
    std::mutex mutex;std::condition_variable condition;bool release_visualizer=false;
    targets[0].start([&](std::size_t volume){
      std::unique_lock<std::mutex> lock(mutex);
      require(condition.wait_for(lock,2s,[&]{return release_visualizer;}),"Headset did not run independently");
      camera[routes.index(0,volume)]=10+int(volume);
    },[]{return false;});
    until([&]{return targets[0].state(0)==Startup::State::connecting;});
    // Headset can join after the first target's startup has begun.
    targets[1].start([&](std::size_t volume){camera[routes.index(1,volume)]=20+int(volume);},[]{return false;});
    until([&]{return targets[1].done();});
    auto poll=[&](std::size_t index){const auto r=routes.address(index);return targets[r.target].ready(r.volume);};
    for(std::size_t n=0;n<4;++n){
      const auto stream=scheduler.select(routes.size(),poll);require(stream.has_value(),"No headset frame");
      const auto r=routes.address(*stream);
      require(r.target==1&&r.volume==n%2&&camera[*stream]==20+int(r.volume),"Headset routed to another volume/camera");
    }
    {std::lock_guard<std::mutex> lock(mutex);release_visualizer=true;}condition.notify_all();
    until([&]{return targets[0].done();});
    for(auto& target:targets)target.join();
    std::array<unsigned,4> visits{};
    for(unsigned n=0;n<12;++n){
      const auto stream=scheduler.select(routes.size(),poll);require(stream.has_value(),"Ready sender missing");
      const auto r=routes.address(*stream);++visits[*stream];
      require(camera[*stream]==int((r.target+1)*10+r.volume),"Viewer camera crossed targets");
    }
    for(auto visits_to_sender:visits)require(visits_to_sender==3,"Target/workspace starved by another sender");
    bool rejected=false;try{routes.address(4);}catch(const std::out_of_range&){rejected=true;}
    require(rejected,"Invalid stream routed into a workspace");
  }
  // Both target negotiations can be outstanding when GUI Stop is requested.
  {
    std::array<Startup,2> targets{Startup(2),Startup(2)};
    std::mutex mutex;std::condition_variable condition;bool cancelled=false;
    std::array<std::atomic<int>,2> attempts{};
    for(std::size_t t=0;t<2;++t)targets[t].start([&,t](std::size_t){
      ++attempts[t];std::unique_lock<std::mutex> lock(mutex);
      require(condition.wait_for(lock,2s,[&]{return cancelled;}),"Target cancellation not delivered");
      throw std::runtime_error("cancelled");
    },[]{return false;});
    until([&]{return attempts[0]==1&&attempts[1]==1;});
    for(auto& target:targets)target.request_stop();
    {std::lock_guard<std::mutex> lock(mutex);cancelled=true;}condition.notify_all();
    for(std::size_t t=0;t<2;++t){targets[t].join();require(attempts[t]==1&&targets[t].done(),"Target worker survived shutdown");}
  }
  std::cout<<"PASS: nonblocking volume/target startup; isolated target poses and shared source routes; cancellation and join of both targets\n";
}
