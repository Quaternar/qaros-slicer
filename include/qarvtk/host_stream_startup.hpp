#pragma once
#include <atomic>
#include <cstddef>
#include <exception>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace qarvtk {
// Negotiation must not run on the frame-pump thread: a later receiver may wait
// for the first stream to deliver video before it answers another request.
// Publishing ready is a release/acquire handoff of the sender and its layout.
class HostStreamStartup {
 public:
  enum class State { pending, connecting, ready, failed };
  explicit HostStreamStartup(std::size_t count){
    for(std::size_t i=0;i<count;++i)results_.push_back(std::make_unique<Result>());
  }
  ~HostStreamStartup(){request_stop();join();}
  template<class Connect,class Stopped> void start(Connect connect,Stopped stopped){
    worker_=std::thread([this,connect,stopped]{
      for(std::size_t i=0;i<results_.size()&&!stop_&&!stopped();++i){
        auto& result=*results_[i];result.state=State::connecting;
        try {connect(i);result.state.store(State::ready,std::memory_order_release);}
        catch(const std::exception& e){result.error=e.what();result.state.store(State::failed,std::memory_order_release);}
        catch(...){result.error="Unknown sender startup failure";result.state.store(State::failed,std::memory_order_release);}
      }
      done_.store(true,std::memory_order_release);
    });
  }
  State state(std::size_t i) const {return results_.at(i)->state.load(std::memory_order_acquire);}
  bool ready(std::size_t i) const {return state(i)==State::ready;}
  std::string error(std::size_t i) const {return state(i)==State::failed?results_.at(i)->error:std::string{};}
  bool done() const {return done_.load(std::memory_order_acquire);}
  bool stopping() const {return stop_.load();}
  void request_stop(){stop_=true;}
  // Caller cancels any blocking SDK operation before joining. Neither the
  // session nor callback/graphics state may be destroyed before join returns.
  void join(){if(worker_.joinable())worker_.join();}
 private:
  struct Result {std::atomic<State> state{State::pending};std::string error;};
  std::vector<std::unique_ptr<Result>> results_;
  std::atomic<bool> stop_{false},done_{false};
  std::thread worker_;
};
}
