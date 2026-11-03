#include "qarvtk/host_frame_pacer.hpp"
#include <iostream>
#include <stdexcept>

using namespace std::chrono;
void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
int main(){try{
  using Clock=qarvtk::HostFramePacer::Clock;
  const auto start=Clock::time_point(seconds(1));
  qarvtk::HostFramePacer a,b;
  require(a.next(start,10)==start,"First receiver must begin without delay");
  require(b.next(start+milliseconds(20),10)==start+milliseconds(20),"Receiver phases must be independent");
  require(a.next(start+milliseconds(35),10)==start+milliseconds(100),"Fast frame must wait for its own deadline");
  require(b.next(start+milliseconds(55),10)==start+milliseconds(120),"Second receiver cadence changed");
  require(a.next(start+milliseconds(450),10)==start+milliseconds(450),"Late receiver must skip missed deadlines");
  require(a.next(start+milliseconds(460),10)==start+milliseconds(550),"Late receiver must not emit catch-up frames");
  require(b.next(start+milliseconds(130),10)==start+milliseconds(220),"Slow first receiver altered the second receiver");
  require(a.next(start+milliseconds(470),20)==start+milliseconds(470),"Rate change must reset the cadence");
  require(a.next(start+milliseconds(480),20)==start+milliseconds(520),"New cadence is incorrect");
  require(a.next(start+milliseconds(490),0)==start+milliseconds(490),"Uncapped mode must not wait");
  std::cout<<"PASS: independent deadlines, no catch-up bursts, live rate changes and uncapped mode\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
