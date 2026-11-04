#pragma once
#include <atomic>
#include <exception>
#include <string>
#include <thread>

namespace qarvtk {
// Receiver negotiation/destruction may block. Only join from the SDK owner at
// shutdown, or after ready() on the frame pump; never wait for it on the GUI.
class HostTargetRetirement {
public:
  ~HostTargetRetirement(){join();}
  template<class Cleanup> void start(Cleanup cleanup){
    worker_=std::thread([this,cleanup]{
      try{cleanup();}
      catch(const std::exception& e){error_=e.what();}
      catch(...){error_="Receiver cleanup failed";}
      done_.store(true,std::memory_order_release);
    });
  }
  bool ready()const{return done_.load(std::memory_order_acquire);}
  bool started()const{return worker_.joinable();}
  void join(){if(worker_.joinable())worker_.join();}
  const std::string& error()const{return error_;} // Read after ready/join only.
private:
  std::thread worker_;
  std::atomic<bool> done_{false};
  std::string error_;
};
}
