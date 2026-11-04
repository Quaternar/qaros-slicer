#pragma once
#include "host_frame_lease.hpp"
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace qarvtk {
// Independent SDK cycles meet ONE existing GUI renderer here. Each route may
// own at most one frame; finishing A releases the GUI before A's SDK show ends.
// Cancellation never releases an acquired frame while the GUI could copy it.
template<class Payload> class HostRenderQueue {
public:
  struct Job {
    std::size_t route;
    std::uint64_t serial;
    Payload payload;
  private:
    friend class HostRenderQueue;
    HostFrameLease lease;
    bool cancel_requested=false;
    Job(std::size_t r,std::uint64_t s,Payload p):route(r),serial(s),payload(std::move(p)){lease.post(s);}
  };
  using Handle=std::shared_ptr<Job>;
  explicit HostRenderQueue(std::size_t routes):slots_(routes),enabled_(routes,false){
    if(!routes||routes>256)throw std::invalid_argument("Invalid render route count");
  }
  void enable(std::size_t route){
    std::lock_guard<std::mutex> lock(mutex_);
    if(closed_||slots_.at(route))throw std::runtime_error("Render route is still retiring");
    enabled_.at(route)=true;
  }
  Handle post(std::size_t route,Payload payload){
    std::lock_guard<std::mutex> lock(mutex_);
    if(closed_||!enabled_.at(route))return {};
    if(slots_.at(route))throw std::runtime_error("A receiver already owns a frame");
    Handle job(new Job(route,++serial_,std::move(payload)));
    slots_[route]=job;pending_.push_back(job);condition_.notify_all();return job;
  }
  Handle acquire(){
    std::lock_guard<std::mutex> lock(mutex_);
    if(active_)throw std::runtime_error("Previous GUI frame has not been finished");
    while(!pending_.empty()){
      auto job=pending_.front();pending_.pop_front();
      if(job->cancel_requested)continue;
      if(!job->lease.acquire())throw std::runtime_error("Invalid queued frame lease");
      active_=job;return job;
    }
    return {};
  }
  Handle check_plane(std::uint64_t serial,unsigned eye,bool depth){
    std::lock_guard<std::mutex> lock(mutex_);
    auto job=current(serial);job->lease.check_plane(serial,eye,depth);return job;
  }
  Handle active_frame(std::uint64_t serial){
    std::lock_guard<std::mutex> lock(mutex_);return current(serial);
  }
  Handle active_frame_if(std::uint64_t serial){
    std::lock_guard<std::mutex> lock(mutex_);
    return active_&&active_->serial==serial?active_:Handle{};
  }
  void complete_plane(std::uint64_t serial,unsigned eye,bool depth){
    std::lock_guard<std::mutex> lock(mutex_);
    current(serial)->lease.complete_plane(serial,eye,depth);
  }
  void finish(std::uint64_t serial){
    std::lock_guard<std::mutex> lock(mutex_);
    current(serial)->lease.finish(serial);active_.reset();condition_.notify_all();
  }
  void abort(std::uint64_t serial){
    std::lock_guard<std::mutex> lock(mutex_);
    if(!active_||active_->serial!=serial)return;
    active_->cancel_requested=true;active_->lease.cancel();active_.reset();condition_.notify_all();
  }
  bool wait(const Handle& job){
    if(!job)return false;
    std::unique_lock<std::mutex> lock(mutex_);
    condition_.wait(lock,[&]{return job->lease.submitted()||
      (job->cancel_requested&&!job->lease.acquired());});
    return job->lease.submitted()&&!job->cancel_requested;
  }
  void consume(const Handle& job){
    std::lock_guard<std::mutex> lock(mutex_);
    if(!job||slots_.at(job->route)!=job)throw std::runtime_error("Expired render job");
    if(job->lease.acquired())throw std::runtime_error("GUI still owns the frame");
    if(job->lease.submitted())job->lease.consume();
    else if(!job->cancel_requested)throw std::runtime_error("Unfinished render job");
    slots_[job->route].reset();
  }
  void disable(std::size_t route){
    std::lock_guard<std::mutex> lock(mutex_);
    enabled_.at(route)=false;cancel(slots_.at(route));condition_.notify_all();
  }
  void close(){
    std::lock_guard<std::mutex> lock(mutex_);
    closed_=true;
    for(std::size_t i=0;i<slots_.size();++i){enabled_[i]=false;cancel(slots_[i]);}
    condition_.notify_all();
  }
private:
  Handle current(std::uint64_t serial){
    if(!active_||active_->serial!=serial)throw std::runtime_error("Invalid or expired GUI frame");
    return active_;
  }
  void cancel(const Handle& job){
    if(!job)return;
    job->cancel_requested=true;
    if(!job->lease.acquired())job->lease.cancel();
  }
  std::mutex mutex_;
  std::condition_variable condition_;
  std::vector<Handle> slots_;
  std::vector<bool> enabled_;
  std::deque<Handle> pending_;
  Handle active_;
  std::uint64_t serial_{};
  bool closed_=false;
};
}
