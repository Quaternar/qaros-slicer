#pragma once
#include <cstddef>
#include <optional>

namespace qarvtk {
// Poll every stream without blocking on an idle/reconnecting receiver. Rotate
// after each selected frame so a continuously ready stream cannot starve peers.
class HostStreamScheduler {
 public:
  template<class Ready> std::optional<std::size_t> select(std::size_t count,Ready ready){
    for(std::size_t n=0;n<count;++n){
      const auto i=(next_+n)%count;
      if(ready(i)){next_=(i+1)%count;return i;}
    }
    return std::nullopt;
  }
 private:
  std::size_t next_{};
};
}
