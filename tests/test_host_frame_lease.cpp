#include "qarvtk/host_frame_lease.hpp"
#include <iostream>
#include <stdexcept>

void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> void rejected(F action){
  bool threw=false;
  try{action();}catch(const std::runtime_error&){threw=true;}
  require(threw,"Invalid lease operation was accepted");
}
int main(){try{
  qarvtk::HostFrameLease lease;
  require(!lease.acquire(),"Unposted frame acquired");
  for(std::uint64_t serial=1;serial<=1000;++serial){
    lease.post(serial);
    require(lease.acquire(),"Posted frame unavailable");
    rejected([&]{lease.acquire();});
    rejected([&]{lease.finish(serial);});
    rejected([&]{lease.check_plane(serial+1,0,false);});
    rejected([&]{lease.check_plane(serial,2,false);});
    for(unsigned eye=0;eye<2;++eye)for(bool depth:{false,true}){
      lease.complete_plane(serial,eye,depth);
      rejected([&]{lease.check_plane(serial,eye,depth);});
    }
    lease.finish(serial);
    // The exact r10 failure order: GUI finishes and polls again BEFORE the
    // SDK worker consumes the completed frame. No scheduling luck required.
    for(int repeat=0;repeat<20;++repeat)
      require(!lease.acquire(),"Completed frame leased twice before worker wakeup");
    require(lease.submitted()&&!lease.acquired(),"GUI changed submitted ownership");
    rejected([&]{lease.finish(serial);});
    rejected([&]{lease.post(serial+1);});
    lease.consume();
    require(!lease.acquire(),"Consumed frame acquired");
    rejected([&]{lease.check_plane(serial,0,false);});
  }
  for(int stage=0;stage<3;++stage){
    qarvtk::HostFrameLease stopped;
    stopped.post(1);
    if(stage>0)stopped.acquire();
    if(stage==2){for(unsigned e=0;e<2;++e)for(bool d:{false,true})stopped.complete_plane(1,e,d);stopped.finish(1);}
    stopped.cancel();
    require(!stopped.acquire()&&!stopped.acquired()&&!stopped.submitted(),"Cancelled frame remains live");
    rejected([&]{stopped.check_plane(1,0,false);});
  }
  std::cout<<"PASS: 1000 deterministic finish/re-poll races, plane ownership and stop states\n";
  return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
