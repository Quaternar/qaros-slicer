#pragma once
#include <array>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace qarvtk {
// Fixed routes keep live senders and their WGL callback addresses stable.
// GUI publication happens only after new slots have their graphics prepared.
class HostLiveSelection {
public:
  static constexpr std::size_t capacity=8;
  struct Slot {std::string id,name;bool wanted=false;};
  using Snapshot=std::array<Slot,capacity>;
  using Choice=std::pair<std::string,std::string>;
  template<class Prepare> void replace(const std::vector<Choice>& choices,Prepare prepare){
    if(choices.size()>capacity)throw std::invalid_argument("Select at most eight receivers");
    std::lock_guard<std::mutex> lock(mutex_);
    auto next=slots_;
    for(auto& slot:next)slot.wanted=false;
    for(const auto& choice:choices){
      if(choice.first.size()!=32||choice.first.find_first_not_of("0123456789abcdef")!=std::string::npos)
        throw std::invalid_argument("Invalid receiver identity");
      std::size_t index=capacity;
      for(std::size_t i=0;i<capacity;++i)if(next[i].id==choice.first){index=i;break;}
      if(index==capacity)for(std::size_t i=0;i<capacity;++i)if(next[i].id.empty()){
        index=i;next[i].id=choice.first;break;
      }
      if(index==capacity)throw std::runtime_error("A receiver is still disconnecting. Retry the selection shortly.");
      if(next[index].wanted)throw std::invalid_argument("Receiver selected more than once");
      next[index].name=choice.second;next[index].wanted=true;
    }
    for(std::size_t i=0;i<capacity;++i)if(next[i].wanted&&slots_[i].id.empty())prepare(i);
    slots_=std::move(next);++generation_;
  }
  bool snapshot(Snapshot& out,std::uint64_t& seen)const{
    std::lock_guard<std::mutex> lock(mutex_);
    if(seen==generation_)return false;
    out=slots_;seen=generation_;return true;
  }
  Snapshot snapshot()const{std::lock_guard<std::mutex> lock(mutex_);return slots_;}
  // A retiring slot is reusable only after SDK teardown. A rapid recheck of
  // that exact peer retains its reservation and reconnects after retirement.
  void release_unwanted(std::size_t index,const std::string& id){
    std::lock_guard<std::mutex> lock(mutex_);
    auto& slot=slots_.at(index);
    if(!id.empty()&&slot.id==id&&!slot.wanted){slot={};++generation_;}
  }
private:
  mutable std::mutex mutex_;
  Snapshot slots_{};
  std::uint64_t generation_=1;
};
}
