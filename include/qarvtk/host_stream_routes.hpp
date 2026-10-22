#pragma once
#include <cstddef>
#include <stdexcept>

namespace qarvtk {
// The native sender/context index covers (target, volume). The GUI ABI keeps
// the original volume index, so both viewers render the same source workspace
// with the pose/FOV belonging to their own sender.
class HostStreamRoutes {
  std::size_t volumes_,targets_;
public:
  struct Address {std::size_t target,volume;};
  HostStreamRoutes(std::size_t volumes,std::size_t targets):volumes_(volumes),targets_(targets){
    if(!volumes||volumes>32||!targets||targets>2)throw std::invalid_argument("Invalid host stream routes");
  }
  std::size_t size()const{return volumes_*targets_;}
  std::size_t index(std::size_t target,std::size_t volume)const{
    if(target>=targets_||volume>=volumes_)throw std::out_of_range("Invalid target/workspace");
    return target*volumes_+volume;
  }
  Address address(std::size_t index)const{
    if(index>=size())throw std::out_of_range("Invalid sender index");
    return {index/volumes_,index%volumes_};
  }
};
}
