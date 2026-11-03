#pragma once
#include <chrono>

namespace qarvtk {
// Independent source cadence. Late frames skip missed deadlines rather than
// producing a burst. Wait before SDK begin so the requested pose stays fresh.
class HostFramePacer {
public:
  using Clock=std::chrono::steady_clock;
  Clock::time_point next(Clock::time_point now,unsigned fps){
    if(!fps){fps_=0;return now;}
    if(fps!=fps_){fps_=fps;next_=now;}
    if(next_<now)next_=now;
    const auto due=next_;
    next_+=std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1./fps));
    return due;
  }
private:
  unsigned fps_{};
  Clock::time_point next_{};
};
}
