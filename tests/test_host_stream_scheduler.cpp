#include "qarvtk/host_stream_scheduler.hpp"
#include <array>
#include <iostream>
#include <stdexcept>

void require(bool value){if(!value)throw std::runtime_error("Unexpected workspace scheduling order");}

int main(){
  qarvtk::HostStreamScheduler scheduler;
  std::array<bool,3> ready{true,true,true};
  auto next=[&]{return scheduler.select(ready.size(),[&](std::size_t i){return ready[i];});};
  for(unsigned i=0;i<30;++i)require(next()==i%3);
  // Stream 1 reconnects: 0 and 2 keep sending; recovery takes the next turn.
  ready[1]=false;
  for(unsigned i=0;i<20;++i)require(next()==(i%2?2:0));
  ready={false,false,false};require(!next());
  ready={false,true,false};require(next()==1);
  ready={true,true,true};require(next()==2);require(next()==0);require(next()==1);
  require(!scheduler.select(0,[](std::size_t){return true;}));
  require(scheduler.select(1,[](std::size_t){return true;})==0);
  std::cout<<"PASS: fair scheduling, idle/reconnecting streams and recovery\n";
}
