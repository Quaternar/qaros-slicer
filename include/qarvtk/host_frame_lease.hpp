#pragma once
#include <cstdint>
#include <stdexcept>

namespace qarvtk {
// Protected by the bridge mutex. A completed frame cannot be acquired again,
// even if the GUI polls before the SDK worker wakes to consume it.
class HostFrameLease {
  enum class State { idle, posted, acquired, submitted, cancelled };
  State state_=State::idle;
  std::uint64_t serial_{};
  unsigned planes_{};
public:
  void post(std::uint64_t serial) {
    if(state_!=State::idle)throw std::runtime_error("Previous host frame is still pending");
    serial_=serial;planes_=0;state_=State::posted;
  }
  bool acquire() {
    if(state_==State::acquired)throw std::runtime_error("Previous host frame has not been finished");
    if(state_!=State::posted)return false;
    state_=State::acquired;return true;
  }
  bool acquired() const {return state_==State::acquired;}
  bool submitted() const {return state_==State::submitted;}
  unsigned check_plane(std::uint64_t serial,unsigned eye,bool depth) const {
    if(state_!=State::acquired||serial!=serial_||eye>1)
      throw std::runtime_error("Invalid or expired host frame lease");
    const unsigned bit=1u<<(eye*2+(depth?1:0));
    if(planes_&bit)throw std::runtime_error("Duplicate host frame plane");
    return bit;
  }
  void complete_plane(std::uint64_t serial,unsigned eye,bool depth) {
    planes_|=check_plane(serial,eye,depth);
  }
  void finish(std::uint64_t serial) {
    if(state_!=State::acquired||serial!=serial_||planes_!=15)
      throw std::runtime_error("Incomplete host stereo frame");
    state_=State::submitted;
  }
  void consume() {
    if(state_!=State::submitted)throw std::runtime_error("Host frame is not ready for submission");
    state_=State::idle;
  }
  void cancel() {state_=State::cancelled;}
};
}
